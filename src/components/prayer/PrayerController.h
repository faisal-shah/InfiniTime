#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <cstdint>
#include <ctime>
#include "components/datetime/DateTimeController.h"
#include "components/prayer/PrayerRules.h"

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class FS;

    // Prayer times: settings live in RAM (9 bytes, persisted to
    // /.system/prayer.dat) and the daily times are pure math over them, so
    // unlike the schedule the whole alert path never touches the filesystem -
    // only Init() (load) and a settings change (save) do. The timer callback
    // is therefore trivially safe on the timer-daemon task with the flash
    // asleep.
    class PrayerController {
    public:
      // Wire/persist blob, byte-identical to the BLE characteristic value and
      // the file content. doc/PrayerService.md.
      struct __attribute__((packed)) Settings {
        uint8_t version = formatVersion;
        uint8_t method = 0;    // PrayerRules::Method
        uint8_t asrMadhab = 0; // PrayerRules::Madhab
        uint8_t flags = 0;     // bit0: alerts enabled; bit1: skip Fajr; rest reserved 0
        int16_t latE2 = 0;     // degrees x100, north positive
        int16_t lonE2 = 0;     // degrees x100, east positive
        int8_t utcOffsetQuarters = 0;

        bool AlertsEnabled() const {
          return (flags & 0x01) != 0;
        }

        // "Vibrate all but Fajr" — the pre-dawn one is the one people most
        // often want to opt out of. Only meaningful while alerts are enabled.
        bool SkipFajr() const {
          return (flags & 0x02) != 0;
        }
      };

      static_assert(sizeof(Settings) == 9, "Settings layout is part of the BLE protocol");

      static constexpr uint8_t formatVersion = 1;

      static bool Validate(const Settings& s) {
        return s.version == formatVersion && s.method <= 4 && s.asrMadhab <= 1 && (s.flags == 0x00 || s.flags == 0x01 || s.flags == 0x03) &&
               s.latE2 >= -9000 && s.latE2 <= 9000 && s.lonE2 >= -18000 && s.lonE2 <= 18000 && s.utcOffsetQuarters >= -48 &&
               s.utcOffsetQuarters <= 56;
      }

      PrayerController(Controllers::DateTime& dateTimeController, Controllers::FS& fs);

      // SystemTask, boot (flash awake): load settings, arm the alert timer.
      void Init(System::SystemTask* systemTask);

      // Any task; RAM only.
      const Settings& GetSettings() const {
        return settings;
      }

      // Today's times from the RAM settings; pure math, any task.
      PrayerRules::Times ComputeToday() const;

      // Watch settings screens (DisplayApp task; screen on so flash is
      // awake): persist + re-arm.
      void SetSettings(const Settings& newSettings);

      // BLE write path: stage on the BLE task (RAM only), commit on the
      // SystemTask with the flash awake (the caller brackets the wake).
      void StageSettings(const Settings& newSettings);
      void CommitStaged();

      // Recompute the next alert and re-arm the timer. RAM + pure math; no
      // filesystem, safe wherever the settings are current.
      void Reschedule();
      // Timer-daemon callback: RAM only.
      void TimerFired();

      // Pull-model text for the pending-alerts queue: the prayer enum rides in
      // the queue entry's detail field; the name is a compile-time string.
      static const char* PrayerName(uint16_t detail) {
        return PrayerRules::Name(static_cast<PrayerRules::Prayer>(detail));
      }

      // Which prayer the most recent TimerFired() was for (queue entry detail)
      // and when it was due (queue entry timestamp).
      uint16_t LastFiredPrayer() const {
        return lastFiredPrayer;
      }

      time_t LastFiredDue() const {
        return lastFiredDue;
      }

      // Which prayer's window contains "now", and when the next prayer starts.
      // For watch faces, so deliberately unlike DueTimesFor(): it counts
      // Sunrise as a window boundary and ignores both AlertsEnabled() and
      // SkipFajr(), because what is displayed must not depend on what vibrates.
      struct Window {
        // Name of the window we are inside, or nullptr between Sunrise and
        // Dhuhr — that stretch belongs to no prayer.
        const char* name;
        // Start of the next prayer, local time of day. Sunrise is never it.
        uint8_t nextHour;
        uint8_t nextMinute;
      };

      // False when there is no window to show: no location set, or the day's
      // times are not computable (polar cases leave all but Dhuhr invalid).
      // Pure math; no filesystem. Cheap enough for once-a-minute.
      bool CurrentWindow(Window& out) const;

    private:
      static constexpr int graceSeconds = 60;
      // FreeRTOS timer periods are 32-bit ticks; cap each arm and re-check on
      // expiry (the next prayer is always <24h away, this is cheap insurance).
      static constexpr uint32_t maxTimerSeconds = 24 * 60 * 60;
      static constexpr const char* datPath = "/.system/prayer.dat";
      static constexpr const char* stagePath = "/.system/prayer.stg";

      time_t Now() const;
      // Prayer times for the civil day containing `dayAnchor`.
      PrayerRules::Times ComputeFor(time_t dayAnchor) const;
      void LoadFromFile();
      void SaveToFile();
      // Due instants (local epoch) of the five alerting prayers for the civil
      // day containing `dayAnchor`, honoring the past-midnight wrap.
      uint8_t DueTimesFor(time_t dayAnchor, time_t (&due)[5], uint8_t (&prayer)[5]) const;
      void ArmTimer(int64_t seconds);

      Controllers::DateTime& dateTimeController;
      Controllers::FS& fs;
      System::SystemTask* systemTask = nullptr;
      TimerHandle_t alertTimer {};

      Settings settings {};
      Settings staged {};
      bool stagedValid = false;

      // Next-alert cache so TimerFired never computes or reads anything.
      bool hasNext = false;
      time_t nextDueTime = 0;
      uint8_t nextPrayer = 0;
      uint8_t nextHour = 0;
      uint8_t nextMinute = 0;

      // Alerting state lives in the AlertQueue; these stamp the queue entry.
      time_t lastFiredDue = 0;
      uint16_t lastFiredPrayer = 0;
    };
  }
}
