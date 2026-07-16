#include "components/multialarm/MultiAlarmController.h"
#include "components/datetime/DateTimeController.h"
#include "components/fs/FS.h"
#include "systemtask/SystemTask.h"
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  void AlarmTimerCallback(TimerHandle_t xTimer) {
    static_cast<MultiAlarmController*>(pvTimerGetTimerID(xTimer))->TimerFired();
  }
}

MultiAlarmController::MultiAlarmController(Controllers::DateTime& dateTimeController, Controllers::FS& fs)
  : dateTimeController {dateTimeController}, fs {fs} {
}

void MultiAlarmController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  alarmTimer = xTimerCreate("MultiAlarm", 1, pdFALSE, this, AlarmTimerCallback);
  LoadFromFile();
  Reschedule();
}

time_t MultiAlarmController::Now() const {
  auto now = dateTimeController.CurrentDateTime();
  return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
}

bool MultiAlarmController::AnyEnabled() const {
  for (const auto& a : alarms) {
    if (a.enabled) {
      return true;
    }
  }
  return false;
}

void MultiAlarmController::SetAlarm(uint8_t index, const Alarm& alarm) {
  if (index >= MaxAlarms) {
    return;
  }
  alarms[index] = alarm;
  version++;
  SaveToFile();
  Reschedule();
}

void MultiAlarmController::SetEnabled(uint8_t index, bool enabled) {
  if (index >= MaxAlarms) {
    return;
  }
  alarms[index].enabled = enabled;
  version++;
  SaveToFile();
  Reschedule();
}

bool MultiAlarmController::ApplyFromCompanion(uint32_t expectedVersion, const Alarm (&next)[MaxAlarms]) {
  // Compare-and-swap: reject if the watch moved on since the phone last pulled.
  if (expectedVersion != version) {
    return false;
  }
  for (uint8_t i = 0; i < MaxAlarms; i++) {
    alarms[i] = next[i];
  }
  version++;
  SaveToFile();
  Reschedule();
  return true;
}

void MultiAlarmController::Serialize(uint8_t (&out)[WireSize]) const {
  std::memcpy(&out[0], &version, sizeof(version));
  for (uint8_t i = 0; i < MaxAlarms; i++) {
    const size_t o = 4 + i * 4;
    out[o + 0] = alarms[i].hour;
    out[o + 1] = alarms[i].minute;
    out[o + 2] = static_cast<uint8_t>(alarms[i].mode);
    out[o + 3] = alarms[i].enabled ? 1 : 0;
  }
}

void MultiAlarmController::Reschedule() {
  xTimerStop(alarmTimer, 0);
  hasNext = false;

  const time_t now = Now();
  std::optional<time_t> best;
  for (uint8_t i = 0; i < MaxAlarms; i++) {
    const auto t = MultiAlarmRules::NextOccurrence(alarms[i], now);
    if (t && (!best || *t < *best)) {
      best = *t;
      nextIndex = i;
    }
  }
  if (!best) {
    return;
  }
  hasNext = true;
  nextDueTime = *best;
  ArmTimer(*best - now);
}

void MultiAlarmController::ArmTimer(int64_t seconds) {
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > static_cast<int64_t>(maxTimerSeconds)) {
    seconds = maxTimerSeconds; // TimerFired re-checks and re-arms
  }
  xTimerChangePeriod(alarmTimer, static_cast<TickType_t>(seconds) * configTICK_RATE_HZ, 0);
  xTimerStart(alarmTimer, 0);
}

void MultiAlarmController::TimerFired() {
  // Timer daemon task, flash possibly asleep: RAM only. A one-shot alarm's
  // disable + persist happens on SystemTask, which powers the flash first.
  if (!hasNext) {
    return;
  }
  const time_t now = Now();
  if (nextDueTime - now > 60) {
    ArmTimer(nextDueTime - now); // armed at the cap; re-arm from cache
    return;
  }
  lastFiredDue = nextDueTime;
  lastFiredIndex = nextIndex;
  systemTask->PushMessage(System::Messages::SetOffMultiAlarm);
  // Re-arm for the next enabled alarm. A fired one-shot is disabled by the
  // SystemTask handler (which then calls Reschedule again with flash awake);
  // meanwhile advance past this instant so we don't re-fire the same one.
  if (alarms[nextIndex].mode == Mode::Daily) {
    Reschedule();
  } else {
    hasNext = false; // wait for SystemTask to disable + reschedule
  }
}

void MultiAlarmController::LoadFromFile() {
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return; // no alarms yet
  }
  FileHeader header {};
  AlarmRecord records[MaxAlarms] {};
  const bool ok = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                  header.version == alarmsFormatVersion &&
                  fs.FileRead(&file, reinterpret_cast<uint8_t*>(records), sizeof(records)) == sizeof(records);
  fs.FileClose(&file);
  if (!ok) {
    NRF_LOG_WARNING("[MultiAlarm] Invalid alarms file, discarding");
    return;
  }
  version = header.alarmsVersion;
  for (uint8_t i = 0; i < MaxAlarms; i++) {
    alarms[i] = {records[i].hour,
                 records[i].minute,
                 records[i].mode == static_cast<uint8_t>(Mode::Daily) ? Mode::Daily : Mode::Once,
                 records[i].enabled != 0};
  }
}

void MultiAlarmController::SaveToFile() {
  FS::Lock lock(fs);
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[MultiAlarm] Cannot open alarms file for write");
    return;
  }
  const FileHeader header {alarmsFormatVersion, version};
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header));
  AlarmRecord records[MaxAlarms] {};
  for (uint8_t i = 0; i < MaxAlarms; i++) {
    records[i] = {alarms[i].hour, alarms[i].minute, static_cast<uint8_t>(alarms[i].mode), static_cast<uint8_t>(alarms[i].enabled ? 1 : 0)};
  }
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(records), sizeof(records));
  fs.FileClose(&file);
}
