#pragma once
// Multi-alarm controller: replaces the upstream single-alarm app. Up to 5
// alarms, each daily or one-shot, individually enable/disable. Own flash file
// (/.system/alarms.dat), separate from the phone-synced schedule. A monotonic
// version counter is bumped on every mutation (watch-side edit OR companion
// write) so the companion's compare-and-swap sync can detect divergence.
// Single FreeRTOS timer armed to the nearest enabled alarm — same cache-and-
// re-arm pattern as ScheduleController.

#include <array>
#include <cstdint>
#include <FreeRTOS.h>
#include <timers.h>
#include "components/multialarm/MultiAlarmRules.h"

namespace Pinetime {
  namespace System {
    class SystemTask;
  }
  namespace Controllers {
    class DateTime;
    class FS;

    class MultiAlarmController {
    public:
      static constexpr uint8_t MaxAlarms = 5;
      using Mode = MultiAlarmRules::Mode;
      using Alarm = MultiAlarmRules::Alarm;

      MultiAlarmController(Controllers::DateTime& dateTimeController, Controllers::FS& fs);

      void Init(System::SystemTask* systemTask);

      const Alarm& Get(uint8_t index) const {
        return alarms[index];
      }

      uint32_t Version() const {
        return version;
      }

      bool AnyEnabled() const;

      // Watch-side edit: replace one alarm, persist, bump version, re-arm.
      void SetAlarm(uint8_t index, const Alarm& alarm);
      void SetEnabled(uint8_t index, bool enabled);

      // Serialize the wire form shared with the companion + BLE service:
      // {version u32, MaxAlarms × {hour, minute, mode, enabled}}.
      static constexpr size_t WireSize = 4 + MaxAlarms * 4;
      void Serialize(uint8_t (&out)[WireSize]) const;

      // Companion WRITE, staged on the BLE task (RAM only, no flash). The leading
      // u32 is the EXPECTED prior version (compare-and-swap). Returns false —
      // rejecting the write synchronously — on an invalid field OR a version
      // mismatch (the phone re-reads, merges, retries). On true, SystemTask must
      // CommitStagedFromCompanion() with the flash awake.
      bool StageWire(const uint8_t (&wire)[WireSize]);

      // SystemTask, flash awake: apply the staged alarms if the CAS still holds
      // (a watch-side edit may have bumped the version since staging), persist,
      // re-arm. A stale stage is silently dropped.
      void CommitStagedFromCompanion();

      void Reschedule();
      void TimerFired();

      // Which alarm the most recent TimerFired() was for (queue entry detail).
      uint16_t LastFiredIndex() const {
        return lastFiredIndex;
      }

      time_t LastFiredDue() const {
        return lastFiredDue;
      }

    private:
      static constexpr uint8_t alarmsFormatVersion = 1;
      static constexpr const char* datPath = "/.system/alarms.dat";
      static constexpr uint32_t maxTimerSeconds = 24 * 60 * 60;

      struct __attribute__((packed)) FileHeader {
        uint8_t version;
        uint32_t alarmsVersion;
      };
      struct __attribute__((packed)) AlarmRecord {
        uint8_t hour;
        uint8_t minute;
        uint8_t mode;
        uint8_t enabled;
      };

      time_t Now() const;
      void LoadFromFile();
      void SaveToFile();
      void ArmTimer(int64_t seconds);

      Controllers::DateTime& dateTimeController;
      Controllers::FS& fs;
      System::SystemTask* systemTask = nullptr;
      TimerHandle_t alarmTimer {};

      std::array<Alarm, MaxAlarms> alarms {};
      uint32_t version = 0;

      // Companion write staged on the BLE task, committed on SystemTask.
      std::array<Alarm, MaxAlarms> staged {};
      uint32_t stagedExpectedVersion = 0;
      bool stagedValid = false;

      bool hasNext = false;
      time_t nextDueTime = 0;
      uint8_t nextIndex = 0;

      time_t lastFiredDue = 0;
      uint16_t lastFiredIndex = 0;
    };
  }
}
