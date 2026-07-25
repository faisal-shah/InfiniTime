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
  : dateTimeController {dateTimeController}, fs {fs}, staged {fs, datPath, stagePath, sizeof(Event), MaxEvents, scheduleFormatVersion} {
}

void ScheduleController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  reminderTimer = xTimerCreate("Schedule", 1, pdFALSE, this, ReminderTimerCallback);
  fs.FileDelete(stagePath); // leftover staging from a power loss mid-sync
  staged.Load();
  Reschedule();
}

time_t ScheduleController::Now() const {
  auto now = dateTimeController.CurrentDateTime();
  return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
}

bool ScheduleController::BeginStaging(uint8_t count, uint32_t version) {
  return staged.Begin(count, version);
}

bool ScheduleController::StageEvent(uint8_t index, const Event& event) {
  Event record = event;
  record.title[TitleSize - 1] = '\0';
  return staged.Stage(index, &record);
}

void ScheduleController::CommitStaged() {
  if (!staged.Commit()) {
    return;
  }
  NRF_LOG_INFO("[ScheduleController] Committed %u events, version %u", staged.Count(), staged.Version());
  Reschedule();
}

bool ScheduleController::OpenForScan(lfs_file_t& file) const {
  return staged.OpenForScan(file);
}

bool ScheduleController::ReadRecord(lfs_file_t& file, Event& event) const {
  if (!staged.ReadNext(file, &event)) {
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
    for (uint8_t i = 0; i < GetCount(); i++) {
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
  for (uint8_t i = 0; i < GetCount() && used + 1 < bufSize; i++) {
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
  if (!staged.Read(index, &out)) {
    return false;
  }
  out.title[TitleSize - 1] = '\0';
  return true;
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
  for (uint8_t i = 0; i < GetCount(); i++) {
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
