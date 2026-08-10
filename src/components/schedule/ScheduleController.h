#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include "components/datetime/DateTimeController.h"
#include "components/fs/FamilyState.h"
#include "components/schedule/ScheduleRules.h"
#include "components/ble/generated/CompanionProtocol.h"
#include "components/timer/StaticTimer.h"

namespace Pinetime {
  namespace System {
    class SystemTask;
    class StorageTask;
  }

  namespace Controllers {
    // Events live in StorageTask's active RAM snapshot. A companion fills the
    // inactive snapshot bank, and only a durable atomic write publishes it.
    // Runtime UI, BLE reads and recurrence scans never touch littlefs.
    class ScheduleController {
    public:
      // 32 recurrence rules live in the active family-state RAM bank.
      static constexpr uint8_t MaxEvents = CompanionProtocol::ScheduleCapacity;
      static constexpr uint8_t ProtocolVersion = CompanionProtocol::ScheduleProtocolVersion;
      static constexpr size_t TitleSize = ScheduleRules::TitleSize;

      using RuleKind = ScheduleRules::RuleKind;
      using Event = ScheduleRules::Event;

      struct Occurrence {
        time_t when;
        char title[TitleSize];
      };

      ScheduleController(Controllers::DateTime& dateTimeController, System::StorageTask& storageTask);

      void Init(System::SystemTask* systemTask);

      // Staging is RAM-only in the inactive family-state bank.
      bool BeginStaging(uint8_t count, uint32_t version);
      bool StageEvent(uint8_t index, const Event& event);
      bool StagingComplete() const;
      void DiscardStaging();

      uint8_t GetStagedCount() const {
        return staging ? stagedCount : 0xff;
      }

      // Queues the complete candidate to StorageTask.
      bool AcceptCommit();
      void CommitStaged();
      void OnPersisted(uint32_t token, bool success);

      // Recomputes the next-occurrence cache from active RAM and arms the timer.
      void Reschedule();
      // Timer-daemon callback: RAM only. Flash may be asleep here.
      void TimerFired();

      // Pull-model text for the pending-alerts queue from active RAM. Returns
      // false when no event
      // matches (schedule re-synced since the firing); caller shows a generic
      // fallback. Replaces the old PrepareFiring/FiringTitle firing state.
      bool DescribeFiring(time_t due, char* buf, size_t bufSize);

      // The due time of the most recent TimerFired(), for SystemTask to stamp
      // the queue entry with.
      time_t LastFiredDue() const {
        return lastFiredDue;
      }

      uint8_t GetCount() const {
        return Active().scheduleCount;
      }

      uint32_t GetVersion() const {
        return Active().scheduleVersion;
      }

      // Random-access read of one active RAM record.
      bool ReadEvent(uint8_t index, Event& out) const;

      // Fills `out` with the next occurrences (sorted by time) within
      // horizonDays. Returns the number written (<= max). No heap allocation;
      // titles are copied into the occurrences so callers never need the
      // events again.
      uint8_t ComputeUpcoming(Occurrence* out, uint8_t max, uint16_t horizonDays = 14) const;

      /**
       * The clock the occurrences above were computed against.
       *
       * Public so a screen asking "is this one today?" compares against the very
       * same instant, rather than reading the clock again and risking a skew
       * across midnight between the list and the highlighting of it.
       */
      time_t Now() const;

    private:
      static constexpr int graceSeconds = 60;
      // FreeRTOS timer periods are 32-bit ticks; cap each arm and re-check on expiry
      // so occurrences further out than one day can't overflow the period.
      static constexpr uint32_t maxTimerSeconds = 24 * 60 * 60;
      static_assert(MaxEvents <= 64, "schedule staging uses a uint64_t received-bitmask");

      bool ArmTimer(int64_t seconds);
      const FamilyState& Active() const;
      FamilyState* Candidate();

      Controllers::DateTime& dateTimeController;
      System::StorageTask& storageTask;
      System::SystemTask* systemTask = nullptr;
      StaticTimer reminderTimer;

      // Next-occurrence cache, so TimerFired never reads flash. Refreshed by
      // Reschedule() on every mutation (commit, fire, dismiss, time change).
      bool hasNext = false;
      time_t nextDueTime = 0;
      uint8_t nextHour = 0;
      uint8_t nextMinute = 0;
      std::array<char, TitleSize> nextTitle {};

      // Everything at or before this instant has already alerted; only strictly
      // later occurrences may fire. Events due at the same second alert together
      // (their titles are combined at display time), so no per-event tie-breaking
      // is needed. Alerting state now lives in the AlertQueue, not here.
      time_t lastFiredDue = 0;
      uint64_t stagedReceived = 0;
      uint32_t stagedVersion = 0;
      uint8_t stagedCount = 0;
      bool staging = false;
      bool commitAccepted = false;
      bool awaitingPersistence = false;
    };
  }
}
