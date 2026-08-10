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
#include "components/timer/StaticTimer.h"
#include "components/multialarm/MultiAlarmRules.h"
#include "components/fs/FamilyState.h"

namespace Pinetime {
  namespace System {
    class SystemTask;
    class StorageTask;
  }

  namespace Controllers {
    class DateTime;

    class MultiAlarmController {
    public:
      static constexpr uint8_t MaxAlarms = 5;
      using Mode = MultiAlarmRules::Mode;
      using Alarm = MultiAlarmRules::Alarm;

      MultiAlarmController(Controllers::DateTime& dateTimeController, System::StorageTask& storageTask);

      void Init(System::SystemTask* systemTask);

      const Alarm& Get(uint8_t index) const {
        return alarmCache[index];
      }

      uint32_t Version() const {
        return Active().alarmVersion;
      }

      bool AnyEnabled() const;

      // Watch-side edit: replace one alarm, persist, bump version, re-arm.
      bool SetAlarm(uint8_t index, const Alarm& alarm);
      bool SetEnabled(uint8_t index, bool enabled);

      // Serialize the wire form shared with the companion + BLE service:
      // {version u32, MaxAlarms × {hour, minute, mode, enabled}}.
      static constexpr size_t WireSize = 4 + MaxAlarms * 4;
      void Serialize(uint8_t (&out)[WireSize]) const;
      enum class StageResult : uint8_t { Accepted, Busy, Invalid };

      // Companion WRITE, staged on the BLE task (RAM only, no flash). The leading
      // u32 is the EXPECTED prior version (compare-and-swap). Returns false —
      // rejecting the write synchronously — on an invalid field OR a version
      // mismatch (the phone re-reads, merges, retries). On true, SystemTask must
      // CommitStagedFromCompanion() with the flash awake.
      StageResult StageWire(const uint8_t (&wire)[WireSize]);

      // SystemTask, flash awake: apply the staged alarms if the CAS still holds
      // (a watch-side edit may have bumped the version since staging), persist,
      // re-arm. A stale stage is silently dropped.
      void CommitStagedFromCompanion();
      void OnPersisted(uint32_t token, bool success);

      bool IsPending() const {
        return pendingToken != 0;
      }

      uint32_t CompletionCount() const {
        return completionCount;
      }

      bool LastCommitSucceeded() const {
        return lastCommitSucceeded;
      }

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
      static constexpr uint32_t maxTimerSeconds = 24 * 60 * 60;

      time_t Now() const;
      bool ArmTimer(int64_t seconds);
      const FamilyState& Active() const;
      bool BeginCandidate(uint32_t token);
      void RefreshCache();

      Controllers::DateTime& dateTimeController;
      System::StorageTask& storageTask;
      System::SystemTask* systemTask = nullptr;
      StaticTimer alarmTimer;

      std::array<Alarm, MaxAlarms> alarmCache {};

      // Companion write staged on the BLE task, committed on SystemTask.
      uint32_t stagedExpectedVersion = 0;
      bool stagedValid = false;
      uint32_t pendingToken = 0;
      uint32_t completionCount = 0;
      bool lastCommitSucceeded = true;

      bool hasNext = false;
      time_t nextDueTime = 0;
      uint8_t nextIndex = 0;

      time_t lastFiredDue = 0;
      uint16_t lastFiredIndex = 0;
    };
  }
}
