#include "components/schedule/ScheduleController.h"
#include "components/fs/FS.h"
#include "systemtask/SystemTask.h"
#include <algorithm>
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  void ReminderTimerCallback(TimerHandle_t xTimer) {
    auto* controller = static_cast<ScheduleController*>(pvTimerGetTimerID(xTimer));
    controller->TimerFired();
  }

}

ScheduleController::ScheduleController(Controllers::DateTime& dateTimeController, Controllers::FS& fs)
  : dateTimeController {dateTimeController}, fs {fs} {
}

void ScheduleController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  reminderTimer = xTimerCreate("Schedule", 1, pdFALSE, this, ReminderTimerCallback);
  fs.FileDelete(stagePath); // leftover staging from a power loss mid-sync
  LoadFromFile();
  Reschedule();
}

time_t ScheduleController::Now() const {
  auto now = dateTimeController.CurrentDateTime();
  return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
}

bool ScheduleController::BeginStaging(uint8_t newCount, uint32_t version) {
  if (newCount > MaxEvents) {
    ClearStagingState();
    return false;
  }

  FS::Lock lock(fs);
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }

  // A re-Begin while a transaction is open is an idempotent restart: the
  // truncate discards whatever the previous attempt staged.
  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    ClearStagingState();
    return false;
  }
  const FileHeader header {scheduleFormatVersion, newCount, version};
  const bool ok = fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  fs.FileClose(&file);
  if (!ok) {
    fs.FileDelete(stagePath);
    ClearStagingState();
    return false;
  }

  stagingOpen = true;
  stagedCount = newCount;
  stagedVersion = version;
  stagedReceived = 0;
  return true;
}

bool ScheduleController::StageEvent(uint8_t index, const Event& event) {
  if (!stagingOpen || index >= stagedCount) {
    return false;
  }
  const uint64_t bit = 1ull << index;
  if ((stagedReceived & bit) != 0) {
    return false;
  }

  Event record = event;
  record.title[TitleSize - 1] = '\0';

  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY) != LFS_ERR_OK) {
    return false;
  }
  // Records may arrive in any order; littlefs zero-fills the gap on a seek
  // past EOF and the receive bitmask guarantees every slot is written before
  // commit.
  const uint32_t offset = sizeof(FileHeader) + static_cast<uint32_t>(index) * sizeof(Event);
  const bool ok =
    fs.FileSeek(&file, offset) >= 0 && fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  fs.FileClose(&file);
  if (!ok) {
    return false;
  }

  stagedReceived |= bit;
  return true;
}

bool ScheduleController::StagingComplete() const {
  if (!stagingOpen) {
    return false;
  }
  const uint64_t all = stagedCount >= 64 ? ~0ull : (1ull << stagedCount) - 1;
  return stagedReceived == all;
}

void ScheduleController::ClearStagingState() {
  stagingOpen = false;
  stagedReceived = 0;
  stagedCount = 0;
}

void ScheduleController::DiscardStaging() {
  FS::Lock lock(fs);
  if (stagingOpen) {
    fs.FileDelete(stagePath);
  }
  ClearStagingState();
}

void ScheduleController::CommitStaged() {
  {
    FS::Lock lock(fs);
    // Re-check under the lock: a disconnect on the BLE task may have discarded
    // the transaction after the commit message was queued.
    if (!StagingComplete()) {
      DiscardStaging();
      return;
    }
    if (fs.Rename(stagePath, datPath) != LFS_ERR_OK) {
      NRF_LOG_WARNING("[ScheduleController] Commit rename failed, keeping previous schedule");
      DiscardStaging();
      return;
    }
    count = stagedCount;
    scheduleVersion = stagedVersion;
    ClearStagingState(); // the staging file is now the live file; nothing to delete
  }
  NRF_LOG_INFO("[ScheduleController] Committed %u events, version %u", count, scheduleVersion);
  Reschedule();
}

bool ScheduleController::OpenForScan(lfs_file_t& file) const {
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return false;
  }
  FileHeader header {};
  if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header) ||
      header.version != scheduleFormatVersion || header.count != count) {
    fs.FileClose(&file);
    return false;
  }
  return true;
}

bool ScheduleController::ReadRecord(lfs_file_t& file, Event& event) const {
  if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&event), sizeof(event)) != sizeof(event)) {
    return false;
  }
  event.title[TitleSize - 1] = '\0';
  return true;
}

void ScheduleController::Reschedule() {
  xTimerStop(reminderTimer, 0);
  hasNext = false;

  const time_t now = Now();
  const time_t from = now - graceSeconds;

  std::optional<time_t> best;
  {
    FS::Lock lock(fs);
    lfs_file_t file;
    if (!OpenForScan(file)) {
      return;
    }
    Event event;
    for (uint8_t i = 0; i < count; i++) {
      if (!ReadRecord(file, event)) {
        break;
      }
      const auto t = ScheduleRules::NextOccurrenceFrom(event, from);
      if (!t) {
        continue;
      }
      // Anything at or before the last alert has already been shown (same-second
      // events alerted together with combined titles).
      if (*t <= lastFiredDue) {
        continue;
      }
      if (!best || *t < *best) {
        best = *t;
        nextHour = event.hour;
        nextMinute = event.minute;
        std::memcpy(nextTitle.data(), event.title, TitleSize);
      }
    }
    fs.FileClose(&file);
  }

  if (!best) {
    return;
  }

  hasNext = true;
  nextDueTime = *best;
  int64_t seconds = *best - now;
  ArmTimer(seconds);
}

void ScheduleController::ArmTimer(int64_t seconds) {
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > maxTimerSeconds) {
    seconds = maxTimerSeconds; // TimerFired() re-checks and re-arms
  }
  xTimerChangePeriod(reminderTimer, static_cast<TickType_t>(seconds) * configTICK_RATE_HZ, 0);
  xTimerStart(reminderTimer, 0);
}


void ScheduleController::TimerFired() {
  // Runs on the FreeRTOS timer daemon task, possibly with the SPI flash in
  // deep power-down: everything here must stay in RAM. The flash scan that
  // builds the combined title happens later, via DescribeFiring() on the
  // display task, once GoToRunning() has powered the flash back up.
  if (!hasNext) {
    return;
  }
  const time_t now = Now();
  if (nextDueTime - now > graceSeconds) {
    // Armed at the cap (occurrence still far away): re-arm from the cache.
    ArmTimer(nextDueTime - now);
    return;
  }

  lastFiredDue = nextDueTime;
  systemTask->PushMessage(System::Messages::SetOffScheduleReminder);
}

bool ScheduleController::DescribeFiring(time_t due, char* buf, size_t bufSize) {
  // Combine all events due at exactly `due` into buf (newline-joined). Pull
  // model: the pending-alerts screen calls this at render time, so the queue
  // never stores text. Returns false when nothing matches (schedule was
  // re-synced since the firing) - caller shows a generic fallback.
  if (bufSize == 0) {
    return false;
  }
  size_t used = 0;
  buf[0] = '\0';

  FS::Lock lock(fs);
  lfs_file_t file;
  if (!OpenForScan(file)) {
    return false;
  }
  Event event;
  for (uint8_t i = 0; i < count && used + 1 < bufSize; i++) {
    if (!ReadRecord(file, event)) {
      break;
    }
    const auto t = ScheduleRules::NextOccurrenceFrom(event, due);
    if (!t || *t != due) {
      continue;
    }
    if (used != 0) {
      buf[used++] = '\n';
    }
    const size_t maxCopy = std::min(std::strlen(event.title), bufSize - used - 1);
    std::memcpy(&buf[used], event.title, maxCopy);
    used += maxCopy;
  }
  fs.FileClose(&file);
  buf[used] = '\0';
  return used > 0;
}


bool ScheduleController::ReadEvent(uint8_t index, Event& out) const {
  if (index >= count) {
    return false;
  }
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return false;
  }
  const uint32_t offset = sizeof(FileHeader) + static_cast<uint32_t>(index) * sizeof(Event);
  const bool ok = fs.FileSeek(&file, offset) >= 0 && fs.FileRead(&file, reinterpret_cast<uint8_t*>(&out), sizeof(out)) == sizeof(out);
  fs.FileClose(&file);
  out.title[TitleSize - 1] = '\0';
  return ok;
}

uint8_t ScheduleController::ComputeUpcoming(Occurrence* out, uint8_t max, uint16_t horizonDays) const {
  const time_t now = Now();
  const time_t horizon = now + static_cast<time_t>(horizonDays) * 86400;
  uint8_t n = 0;

  FS::Lock lock(fs);
  lfs_file_t file;
  if (!OpenForScan(file)) {
    return 0;
  }
  Event event;
  for (uint8_t i = 0; i < count; i++) {
    if (!ReadRecord(file, event)) {
      break;
    }
    time_t from = now;
    while (true) {
      const auto t = ScheduleRules::NextOccurrenceFrom(event, from);
      if (!t || *t > horizon) {
        break;
      }
      if (n < max || *t < out[n - 1].when) {
        // Insertion sort, dropping the latest occurrence when full.
        uint8_t pos = std::min<uint8_t>(n, max - 1);
        while (pos > 0 && out[pos - 1].when > *t) {
          out[pos] = out[pos - 1];
          pos--;
        }
        out[pos].when = *t;
        std::memcpy(out[pos].title, event.title, TitleSize);
        if (n < max) {
          n++;
        }
      }
      from = *t + 1;
    }
  }
  fs.FileClose(&file);
  return n;
}

void ScheduleController::LoadFromFile() {
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[ScheduleController] No schedule file");
    return;
  }

  FileHeader header {};
  const bool headerOk = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                        header.version == scheduleFormatVersion && header.count <= MaxEvents;
  fs.FileClose(&file);
  if (!headerOk) {
    NRF_LOG_WARNING("[ScheduleController] Invalid schedule file, discarding");
    return;
  }

  // Validate length so scans can trust the header count.
  lfs_info info {};
  if (fs.Stat(datPath, &info) != LFS_ERR_OK || info.size < sizeof(FileHeader) + static_cast<uint32_t>(header.count) * sizeof(Event)) {
    NRF_LOG_WARNING("[ScheduleController] Truncated schedule file, discarding");
    return;
  }

  count = header.count;
  scheduleVersion = header.scheduleVersion;
  NRF_LOG_INFO("[ScheduleController] Loaded %u events, version %u", count, scheduleVersion);
}
