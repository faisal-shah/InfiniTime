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
  LoadFromFile();
  Reschedule();
}

time_t ScheduleController::Now() const {
  auto now = dateTimeController.CurrentDateTime();
  return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
}

void ScheduleController::BeginStaging(uint8_t newCount, uint32_t version) {
  stagingOpen = newCount <= MaxEvents;
  stagedCount = stagingOpen ? newCount : 0;
  stagedVersion = version;
  stagedReceived = 0;
}

bool ScheduleController::StageEvent(uint8_t index, const Event& event) {
  if (!stagingOpen || index >= stagedCount) {
    return false;
  }
  const uint32_t bit = 1u << index;
  if ((stagedReceived & bit) != 0) {
    return false;
  }
  staged[index] = event;
  staged[index].title[TitleSize - 1] = '\0';
  stagedReceived |= bit;
  return true;
}

bool ScheduleController::StagingComplete() const {
  if (!stagingOpen) {
    return false;
  }
  static_assert(MaxEvents < 32, "stagedReceived bitmask math requires MaxEvents < 32");
  return stagedReceived == (1u << stagedCount) - 1;
}

void ScheduleController::DiscardStaging() {
  stagingOpen = false;
  stagedReceived = 0;
  stagedCount = 0;
}

void ScheduleController::CommitStaged() {
  if (!StagingComplete()) {
    DiscardStaging();
    return;
  }
  count = stagedCount;
  scheduleVersion = stagedVersion;
  std::copy_n(staged.begin(), count, events.begin());
  DiscardStaging();
  SaveToFile();
  NRF_LOG_INFO("[ScheduleController] Committed %u events, version %u", count, scheduleVersion);
  Reschedule();
}

void ScheduleController::Reschedule() {
  xTimerStop(reminderTimer, 0);
  nextIndex = -1;

  const time_t now = Now();
  const time_t from = now - graceSeconds;

  std::optional<time_t> best;
  for (uint8_t i = 0; i < count; i++) {
    const auto t = ScheduleRules::NextOccurrenceFrom(events[i], from);
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
      nextIndex = i;
    }
  }

  if (!best) {
    return;
  }

  nextDueTime = *best;
  int64_t seconds = *best - now;
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > maxTimerSeconds) {
    seconds = maxTimerSeconds; // TimerFired() re-checks and re-arms
  }
  xTimerChangePeriod(reminderTimer, static_cast<TickType_t>(seconds) * configTICK_RATE_HZ, 0);
  xTimerStart(reminderTimer, 0);
}

void ScheduleController::DeferReminder(uint32_t seconds) {
  isAlerting = false;
  xTimerChangePeriod(reminderTimer, static_cast<TickType_t>(seconds) * configTICK_RATE_HZ, 0);
  xTimerStart(reminderTimer, 0);
}

void ScheduleController::TimerFired() {
  const time_t now = Now();
  if (nextIndex < 0 || nextDueTime - now > graceSeconds) {
    // Armed at the cap (occurrence still far away), or state went stale: re-arm.
    Reschedule();
    return;
  }

  // Combine all events due at this exact second into one alert.
  const time_t from = nextDueTime;
  size_t used = 0;
  for (uint8_t i = 0; i < count && used + 1 < firingTitle.size(); i++) {
    const auto t = ScheduleRules::NextOccurrenceFrom(events[i], from);
    if (!t || *t != nextDueTime) {
      continue;
    }
    if (used != 0) {
      firingTitle[used++] = '\n';
    }
    const size_t maxCopy = std::min(std::strlen(events[i].title), firingTitle.size() - used - 1);
    std::memcpy(&firingTitle[used], events[i].title, maxCopy);
    used += maxCopy;
  }
  firingTitle[used] = '\0';
  firingHour = events[nextIndex].hour;
  firingMinute = events[nextIndex].minute;
  lastFiredDue = nextDueTime;
  isAlerting = true;
  systemTask->PushMessage(System::Messages::SetOffScheduleReminder);
}

void ScheduleController::StopAlerting() {
  isAlerting = false;
  Reschedule();
}

uint8_t ScheduleController::ComputeUpcoming(Occurrence* out, uint8_t max, uint16_t horizonDays) const {
  const time_t now = Now();
  const time_t horizon = now + static_cast<time_t>(horizonDays) * 86400;
  uint8_t n = 0;

  for (uint8_t i = 0; i < count; i++) {
    time_t from = now;
    while (true) {
      const auto t = ScheduleRules::NextOccurrenceFrom(events[i], from);
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
        out[pos] = Occurrence {*t, i};
        if (n < max) {
          n++;
        }
      }
      from = *t + 1;
    }
  }
  return n;
}

void ScheduleController::LoadFromFile() {
  lfs_file_t file;
  if (fs.FileOpen(&file, "/.system/schedule.dat", LFS_O_RDONLY) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[ScheduleController] No schedule file");
    return;
  }

  FileHeader header {};
  if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header) ||
      header.version != scheduleFormatVersion || header.count > MaxEvents) {
    NRF_LOG_WARNING("[ScheduleController] Invalid schedule file, discarding");
    fs.FileClose(&file);
    return;
  }

  const uint32_t recordBytes = header.count * sizeof(Event);
  if (fs.FileRead(&file, reinterpret_cast<uint8_t*>(events.data()), recordBytes) != static_cast<int>(recordBytes)) {
    NRF_LOG_WARNING("[ScheduleController] Truncated schedule file, discarding");
    fs.FileClose(&file);
    return;
  }
  fs.FileClose(&file);

  count = header.count;
  scheduleVersion = header.scheduleVersion;
  for (uint8_t i = 0; i < count; i++) {
    events[i].title[TitleSize - 1] = '\0';
  }
  NRF_LOG_INFO("[ScheduleController] Loaded %u events, version %u", count, scheduleVersion);
}

void ScheduleController::SaveToFile() const {
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }

  lfs_file_t file;
  if (fs.FileOpen(&file, "/.system/schedule.dat", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[ScheduleController] Failed to open schedule file for writing");
    return;
  }

  const FileHeader header {scheduleFormatVersion, count, scheduleVersion};
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(events.data()), count * sizeof(Event));
  fs.FileClose(&file);
  NRF_LOG_INFO("[ScheduleController] Saved %u events", count);
}
