#include "components/prayer/PrayerController.h"
#include "components/fs/FS.h"
#include "systemtask/SystemTask.h"
#include <algorithm>
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  void AlertTimerCallback(TimerHandle_t xTimer) {
    auto* controller = static_cast<PrayerController*>(pvTimerGetTimerID(xTimer));
    controller->TimerFired();
  }

  // The five prayers that alert, in day order (sunrise never alerts).
  constexpr Pinetime::Controllers::PrayerRules::Prayer alerting[5] = {
    Pinetime::Controllers::PrayerRules::Fajr,
    Pinetime::Controllers::PrayerRules::Dhuhr,
    Pinetime::Controllers::PrayerRules::Asr,
    Pinetime::Controllers::PrayerRules::Maghrib,
    Pinetime::Controllers::PrayerRules::Isha,
  };
}

PrayerController::PrayerController(Controllers::DateTime& dateTimeController, Controllers::FS& fs)
  : dateTimeController {dateTimeController}, fs {fs} {
}

void PrayerController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  alertTimer = xTimerCreate("Prayer", 1, pdFALSE, this, AlertTimerCallback);
  fs.FileDelete(stagePath); // leftover from a power loss mid-save
  LoadFromFile();
  Reschedule();
}

time_t PrayerController::Now() const {
  auto now = dateTimeController.CurrentDateTime();
  return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
}

PrayerRules::Times PrayerController::ComputeFor(time_t dayAnchor) const {
  tm local {};
  localtime_r(&dayAnchor, &local);
  return PrayerRules::Compute(static_cast<uint16_t>(local.tm_year + 1900),
                              static_cast<uint8_t>(local.tm_mon + 1),
                              static_cast<uint8_t>(local.tm_mday),
                              settings.latE2 / 100.0f,
                              settings.lonE2 / 100.0f,
                              settings.utcOffsetQuarters / 4.0f,
                              static_cast<PrayerRules::Method>(settings.method),
                              static_cast<PrayerRules::Madhab>(settings.asrMadhab));
}

PrayerRules::Times PrayerController::ComputeToday() const {
  return ComputeFor(Now());
}

bool PrayerController::CurrentWindow(Window& out) const {
  // No location configured. 0,0 is the struct default and open ocean, so
  // treating it as "unset" costs nothing real and keeps the face from
  // confidently displaying Null Island's prayer times.
  if (settings.latE2 == 0 && settings.lonE2 == 0) {
    return false;
  }

  const time_t now = Now();
  tm local {};
  localtime_r(&now, &local);
  const int32_t nowMinutes = local.tm_hour * 60 + local.tm_min;

  constexpr time_t day = 24 * 60 * 60;
  PrayerRules::Window window;
  if (!PrayerRules::SelectWindow(ComputeFor(now - day), ComputeFor(now), ComputeFor(now + day), nowMinutes, window)) {
    return false;
  }

  out.name = PrayerRules::WindowName(window.window);
  out.nextHour = static_cast<uint8_t>(window.nextHour);
  out.nextMinute = static_cast<uint8_t>(window.nextMinute);
  return true;
}

void PrayerController::SetSettings(const Settings& newSettings) {
  if (!Validate(newSettings)) {
    return;
  }
  settings = newSettings;
  SaveToFile();
  Reschedule();
}

void PrayerController::StageSettings(const Settings& newSettings) {
  staged = newSettings;
  stagedValid = true;
}

void PrayerController::CommitStaged() {
  if (!stagedValid || !Validate(staged)) {
    stagedValid = false;
    return;
  }
  settings = staged;
  stagedValid = false;
  SaveToFile();
  Reschedule();
  NRF_LOG_INFO("[PrayerController] Settings committed (method %u)", settings.method);
}

// Due instants of the five alerting prayers for the civil day containing
// dayAnchor. A time-of-day smaller than Dhuhr's belongs to the NEXT civil day
// (near-polar wrap, see PrayerRules.h).
uint8_t PrayerController::DueTimesFor(time_t dayAnchor, time_t (&due)[5], uint8_t (&prayer)[5]) const {
  const PrayerRules::Times times = ComputeFor(dayAnchor);
  tm local {};
  localtime_r(&dayAnchor, &local);
  tm midnight = local;
  midnight.tm_hour = 0;
  midnight.tm_min = 0;
  midnight.tm_sec = 0;
  const time_t dayStart = std::mktime(&midnight);

  uint8_t n = 0;
  for (const PrayerRules::Prayer p : alerting) {
    if ((times.validMask & (1u << p)) == 0) {
      continue;
    }
    if (p == PrayerRules::Fajr && settings.SkipFajr()) {
      continue;
    }
    time_t t = dayStart + static_cast<time_t>(times.minutes[p]) * 60;
    // Only the post-noon prayers can genuinely wrap past midnight (near-polar
    // summer maghrib/isha); fajr is always before dhuhr on the same civil day.
    if (p > PrayerRules::Dhuhr && times.minutes[p] < times.minutes[PrayerRules::Dhuhr]) {
      t += 24 * 60 * 60;
    }
    due[n] = t;
    prayer[n] = p;
    n++;
  }
  return n;
}

void PrayerController::Reschedule() {
  xTimerStop(alertTimer, 0);
  hasNext = false;

  if (!settings.AlertsEnabled()) {
    return; // the display path computes on open; no timer needed
  }

  const time_t now = Now();
  const time_t threshold = std::max(now - graceSeconds, lastFiredDue);

  time_t due[5];
  uint8_t prayer[5];
  // Today, then tomorrow (after Isha the next alert is tomorrow's Fajr).
  // Select the earliest eligible due; the array is not assumed ordered (a
  // wrapped isha lands on the next day).
  for (int dayOffset = 0; dayOffset < 2 && !hasNext; dayOffset++) {
    const uint8_t n = DueTimesFor(now + dayOffset * 24 * 60 * 60, due, prayer);
    for (uint8_t i = 0; i < n; i++) {
      if (due[i] > threshold && (!hasNext || due[i] < nextDueTime)) {
        hasNext = true;
        nextDueTime = due[i];
        nextPrayer = prayer[i];
      }
    }
  }

  if (!hasNext) {
    NRF_LOG_INFO("[PrayerController] Reschedule: nothing to arm");
    return;
  }
  tm dueLocal {};
  localtime_r(&nextDueTime, &dueLocal);
  nextHour = static_cast<uint8_t>(dueLocal.tm_hour);
  nextMinute = static_cast<uint8_t>(dueLocal.tm_min);
  NRF_LOG_INFO("[PrayerController] Next alert: prayer %u at %02u:%02u (in %d s)",
               nextPrayer,
               nextHour,
               nextMinute,
               static_cast<int>(nextDueTime - now));
  ArmTimer(nextDueTime - now);
}

void PrayerController::ArmTimer(int64_t seconds) {
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > maxTimerSeconds) {
    seconds = maxTimerSeconds; // TimerFired() re-checks and re-arms
  }
  xTimerChangePeriod(alertTimer, static_cast<TickType_t>(seconds) * configTICK_RATE_HZ, 0);
  xTimerStart(alertTimer, 0);
}

void PrayerController::TimerFired() {
  // Timer daemon task, flash possibly asleep: RAM only.
  if (!hasNext) {
    return;
  }
  const time_t now = Now();
  if (nextDueTime - now > graceSeconds) {
    ArmTimer(nextDueTime - now);
    return;
  }

  lastFiredDue = nextDueTime;
  lastFiredPrayer = nextPrayer;
  systemTask->PushMessage(System::Messages::SetOffPrayerAlert);
  // Immediately re-arm for the next prayer: alerting state lives in the
  // AlertQueue now, so nothing here waits for a dismissal.
  Reschedule();
}

void PrayerController::LoadFromFile() {
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[PrayerController] No settings file, using defaults");
    return;
  }
  Settings loaded {};
  const bool ok = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&loaded), sizeof(loaded)) == sizeof(loaded) && Validate(loaded);
  fs.FileClose(&file);
  if (!ok) {
    NRF_LOG_WARNING("[PrayerController] Invalid settings file, using defaults");
    return;
  }
  settings = loaded;
  NRF_LOG_INFO("[PrayerController] Loaded settings (method %u)", settings.method);
}

void PrayerController::SaveToFile() {
  FS::Lock lock(fs);
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }

  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[PrayerController] Failed to open settings file for writing");
    return;
  }
  const bool ok = fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&settings), sizeof(settings)) == sizeof(settings);
  fs.FileClose(&file);
  if (!ok) {
    fs.FileDelete(stagePath);
    return;
  }
  // Atomic in littlefs: a power cut leaves either the old or the new settings.
  fs.Rename(stagePath, datPath);
}
