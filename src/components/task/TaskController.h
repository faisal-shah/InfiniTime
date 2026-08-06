#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include "components/datetime/DateTimeController.h"
#include "components/ble/generated/CompanionProtocol.h"
#include "components/fs/FamilyState.h"

namespace Pinetime {
  namespace System {
    class StorageTask;
  }

  namespace Controllers {
    // The daily task checklist. Task DEFINITIONS (id/order/title) live in
    // littlefs (/.system/tasks.dat) and sync from the phone by full-replace,
    // complete-list staging in the inactive family-state RAM bank.
    //
    // COMPLETION is watch-only: which tasks are ticked today, plus a
    // consecutive-all-done streak, live in a tiny separate file
    // (/.system/tasks.state) so they never enter the definition sync. Ticks are
    // keyed by task id (stable across a re-sync/reorder). At local midnight
    // RollOverDay() evaluates the day (streak +1 if every task was done, else 0)
    // and clears the ticks; the on-load date check is the backstop when the
    // watch was off across midnight.
    class TaskController {
    public:
      static constexpr uint8_t MaxTasks = CompanionProtocol::TaskCapacity;
      static constexpr uint8_t ProtocolVersion = CompanionProtocol::TaskRecordVersion;
      static constexpr size_t TitleSize = 24;

      // On-wire / on-flash record. Field order + packing are part of the BLE
      // protocol and the persistence format (see doc/TaskService.md).
      struct __attribute__((packed)) Task {
        uint16_t id;
        uint8_t order;
        char title[TitleSize];
        uint32_t lastModified;
      };

      static_assert(sizeof(Task) == CompanionProtocol::TaskRecordSize, "Task layout is part of the BLE protocol");
      static_assert(MaxTasks <= 64, "received tracking uses a uint64_t bitmask");

      TaskController(Controllers::DateTime& dateTimeController, System::StorageTask& storageTask);

      void Init();
      void Process();

      // --- definition staging (BLE task; TaskService holds the wake lock) ---
      bool BeginStaging(uint8_t count, uint32_t version);
      bool StageTask(uint8_t index, const Task& task);

      bool StagingComplete() const;
      void DiscardStaging();

      uint8_t GetStagedCount() const {
        return staging ? stagedCount : 0xff;
      }

      // SystemTask only, flash awake (renames the staging file to live).
      bool AcceptCommit();
      void CommitStaged();
      void OnPersisted(CompanionProtocol::FamilyStateOperation operation,
                       uint32_t token,
                       bool success);

      uint8_t GetCount() const {
        return Active().taskCount;
      }

      uint32_t GetVersion() const {
        return Active().taskVersion;
      }

      bool ReadTask(uint8_t index, Task& out) const;

      // --- completion + streak (watch-only) ---
      /** Whether the task currently at list index is ticked today. */
      bool IsDoneAt(uint8_t index) const;
      /** Toggle the tick of the task at list index; persists (flash awake). */
      void ToggleAt(uint8_t index);
      /** Number of current tasks ticked today. */
      uint8_t CompletedCount() const;

      /** Completed count without touching the filesystem. CommitStaged()
          prunes doneIds to the ids in the live list and RollOverDay() clears
          it, so doneCount is exactly CompletedCount() -- but in RAM. Watch
          faces must use this: the display task keeps rendering in always-on
          mode with the SPI flash powered down, so a read there would come
          back as garbage. */
      uint8_t CompletedCountCached() const {
        return doneCount;
      }

      uint16_t GetStreak() const {
        return Active().taskStreak;
      }

      /** Phone override of the streak (parent forgives a day / sets a reward). */
      bool SetStreak(uint16_t value, uint32_t token);
      /** Evaluate the day that just ended into the streak, then clear the ticks. */
      void RollOverDay();

    private:
      uint32_t TodayKey() const; // YYYYMMDD from the local clock
      bool IdDone(uint16_t id) const;
      void SetIdDone(uint16_t id, bool done);
      const FamilyState& Active() const;
      FamilyState* Candidate(CompanionProtocol::FamilyStateOperation operation,
                             uint32_t token);
      void PruneTicks();

      Controllers::DateTime& dateTimeController;
      System::StorageTask& storageTask;
      uint8_t doneCount = 0;
      uint16_t doneIds[MaxTasks] {};
      uint64_t stagedReceived = 0;
      uint32_t stagedVersion = 0;
      uint8_t stagedCount = 0;
      bool staging = false;
      bool commitAccepted = false;
      bool awaitingDefinitions = false;
      uint32_t pendingStatsToken = 0;
      bool pendingRollover = false;
      bool rolloverRetryPending = false;
      TickType_t nextRolloverAttempt = 0;
    };
  }
}
