#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include "components/datetime/DateTimeController.h"
#include "components/fs/StagedList.h"

#include <littlefs/lfs.h>

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class FS;

    // The daily task checklist. Task DEFINITIONS (id/order/title) live in
    // littlefs (/.system/tasks.dat) and sync from the phone by full-replace,
    // atomic-rename staging — the shared StagedList, exactly like the Schedule.
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
      static constexpr uint8_t MaxTasks = 20;
      static constexpr uint8_t ProtocolVersion = 1;
      static constexpr size_t TitleSize = 24;

      // On-wire / on-flash record. Field order + packing are part of the BLE
      // protocol and the persistence format (companion: src/ble/tasksProtocol.ts).
      struct __attribute__((packed)) Task {
        uint16_t id;
        uint8_t order;
        char title[TitleSize];
        uint32_t lastModified;
      };
      static_assert(sizeof(Task) == 31, "Task layout is part of the BLE protocol");
      static_assert(MaxTasks <= 64, "StagedList uses a uint64_t received-bitmask");

      TaskController(Controllers::DateTime& dateTimeController, Controllers::FS& fs);

      void Init(System::SystemTask* systemTask);

      // --- definition staging (BLE task; TaskService holds the wake lock) ---
      bool BeginStaging(uint8_t count, uint32_t version);
      bool StageTask(uint8_t index, const Task& task);
      bool StagingComplete() const {
        return staged.Complete();
      }
      void DiscardStaging() {
        staged.Discard();
      }
      uint8_t GetStagedCount() const {
        return staged.StagedCount();
      }
      // SystemTask only, flash awake (renames the staging file to live).
      void CommitStaged();

      uint8_t GetCount() const {
        return staged.Count();
      }
      uint32_t GetVersion() const {
        return staged.Version();
      }
      bool ReadTask(uint8_t index, Task& out) const;

      // --- completion + streak (watch-only) ---
      /** Whether the task currently at list index is ticked today. */
      bool IsDoneAt(uint8_t index) const;
      /** Toggle the tick of the task at list index; persists (flash awake). */
      void ToggleAt(uint8_t index);
      /** Number of current tasks ticked today. */
      uint8_t CompletedCount() const;
      uint16_t GetStreak() const {
        return streak;
      }
      /** Phone override of the streak (parent forgives a day / sets a reward). */
      void SetStreak(uint16_t value);
      /** Evaluate the day that just ended into the streak, then clear the ticks. */
      void RollOverDay();

    private:
      static constexpr uint8_t formatVersion = 1;
      static constexpr const char* datPath = "/.system/tasks.dat";
      static constexpr const char* stagePath = "/.system/tasks.stg";
      static constexpr const char* statePath = "/.system/tasks.state";

      struct __attribute__((packed)) StateFile {
        uint8_t version;
        uint32_t dateKey; // YYYYMMDD local; 0 = none
        uint16_t streak;
        uint8_t doneCount;
        uint16_t doneIds[MaxTasks];
      };

      uint32_t TodayKey() const; // YYYYMMDD from the local clock
      void LoadState();
      void SaveState(); // best-effort: a no-op if flash is asleep
      bool IdDone(uint16_t id) const;
      void SetIdDone(uint16_t id, bool done);

      Controllers::DateTime& dateTimeController;
      Controllers::FS& fs;
      StagedList staged;
      System::SystemTask* systemTask = nullptr;

      // Completion state (mirrors statePath).
      uint32_t stateDateKey = 0;
      uint16_t streak = 0;
      uint8_t doneCount = 0;
      uint16_t doneIds[MaxTasks] {};
    };
  }
}
