#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include "components/datetime/DateTimeController.h"
#include "components/schedule/ScheduleRules.h"
#include "components/fs/StagedList.h"

// littlefs forward types for private helpers
#include <littlefs/lfs.h>

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class FS;

    // Events live in littlefs (/.system/schedule.dat), not RAM. RAM holds only
    // the digest fields (count, version), staging bookkeeping, and a cache of
    // the next occurrence so the timer callback never touches the filesystem.
    // Sync staging goes to /.system/schedule.stg and commit is an atomic
    // rename, so a power loss at any instant leaves the previous schedule
    // intact.
    class ScheduleController {
    public:
      // 64 recurrence rules (39 B each) cost ~2.5 KB of flash and no RAM.
      static constexpr uint8_t MaxEvents = 64;
      static constexpr uint8_t ProtocolVersion = 1;
      static constexpr size_t TitleSize = ScheduleRules::TitleSize;

      using RuleKind = ScheduleRules::RuleKind;
      using Event = ScheduleRules::Event;

      struct Occurrence {
        time_t when;
        char title[TitleSize];
      };

      ScheduleController(Controllers::DateTime& dateTimeController, Controllers::FS& fs);

      void Init(System::SystemTask* systemTask);

      // Staging: called from the BLE task; writes the staging file. The caller
      // must hold the system awake (flash powered) for the whole transaction -
      // ScheduleService takes a wake lock in BeginSync.
      bool BeginStaging(uint8_t count, uint32_t version);
      bool StageEvent(uint8_t index, const Event& event);
      bool StagingComplete() const {
        return staged.Complete();
      }
      void DiscardStaging() {
        staged.Discard();
      }

      uint8_t GetStagedCount() const {
        return staged.StagedCount();
      }

      // Must only be called from the SystemTask (renames flash files, re-arms
      // the timer). Flash must be awake (the sync wake lock guarantees it).
      void CommitStaged();

      // Recomputes the next-occurrence cache from the schedule file and arms
      // the timer. SystemTask or DisplayApp task only, flash awake.
      void Reschedule();
      // Timer-daemon callback: RAM only. Flash may be asleep here.
      void TimerFired();

      // Pull-model text for the pending-alerts queue: rebuild the combined
      // title of all events due at exactly `due` into `buf` (flash must be
      // awake — display/system task only). Returns false when no event
      // matches (schedule re-synced since the firing); caller shows a generic
      // fallback. Replaces the old PrepareFiring/FiringTitle firing state.
      bool DescribeFiring(time_t due, char* buf, size_t bufSize);

      // The due time of the most recent TimerFired(), for SystemTask to stamp
      // the queue entry with.
      time_t LastFiredDue() const {
        return lastFiredDue;
      }

      uint8_t GetCount() const {
        return staged.Count();
      }

      uint32_t GetVersion() const {
        return staged.Version();
      }

      // Random-access read of one record from the schedule file (BLE event
      // read characteristic). Any task, flash awake.
      bool ReadEvent(uint8_t index, Event& out) const;

      // Fills `out` with the next occurrences (sorted by time) within
      // horizonDays. Returns the number written (<= max). No heap allocation;
      // titles are copied into the occurrences so callers never need the
      // events again.
      uint8_t ComputeUpcoming(Occurrence* out, uint8_t max, uint16_t horizonDays = 14) const;

    private:
      static constexpr uint8_t scheduleFormatVersion = 1;
      static constexpr int graceSeconds = 60;
      // FreeRTOS timer periods are 32-bit ticks; cap each arm and re-check on expiry
      // so occurrences further out than one day can't overflow the period.
      static constexpr uint32_t maxTimerSeconds = 24 * 60 * 60;
      static constexpr const char* datPath = "/.system/schedule.dat";
      static constexpr const char* stagePath = "/.system/schedule.stg";
      static_assert(MaxEvents <= 64, "StagedList uses a uint64_t received-bitmask");

      time_t Now() const;
      void ArmTimer(int64_t seconds);
      // Sequential-scan wrappers over `staged`; the caller holds one FS::Lock
      // across OpenForScan..close (see StagedList).
      bool OpenForScan(lfs_file_t& file) const;
      bool ReadRecord(lfs_file_t& file, Event& event) const;

      Controllers::DateTime& dateTimeController;
      Controllers::FS& fs;
      StagedList staged;
      System::SystemTask* systemTask = nullptr;
      TimerHandle_t reminderTimer {};

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
    };
  }
}
