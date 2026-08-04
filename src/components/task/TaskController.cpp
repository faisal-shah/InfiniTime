#include "components/task/TaskController.h"
#include "components/task/TaskRules.h"
#include "components/fs/FS.h"
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

TaskController::TaskController(Controllers::DateTime& dateTimeController, Controllers::FS& fs)
  : dateTimeController {dateTimeController}, fs {fs}, staged {fs, "TaskController", datPath, stagePath, sizeof(Task), MaxTasks, formatVersion} {
}

void TaskController::Init() {
  fs.FileDelete(stagePath); // leftover staging from a power loss mid-sync
  staged.Load();
  LoadState();
  // Watch was off / idle across a midnight -> settle the day that ended.
  if (stateDateKey != TodayKey()) {
    RollOverDay();
  }
}

uint32_t TaskController::TodayKey() const {
  return static_cast<uint32_t>(dateTimeController.Year()) * 10000 +
         static_cast<uint32_t>(dateTimeController.Month()) * 100 + dateTimeController.Day();
}

// ---- definition staging (delegated to the shared StagedList) ----

bool TaskController::BeginStaging(uint8_t count, uint32_t version) {
  return staged.Begin(count, version);
}

bool TaskController::StageTask(uint8_t index, const Task& task) {
  Task record = task;
  record.title[TitleSize - 1] = '\0';
  return staged.Stage(index, &record);
}

void TaskController::CommitStaged() {
  if (!staged.Commit()) {
    return;
  }
  NRF_LOG_INFO("[TaskController] Committed %u tasks, version %u", staged.Count(), staged.Version());

  // Drop completion for tasks that no longer exist (ids not in the new list),
  // so stale ticks can't count toward "all done".
  uint8_t kept = 0;
  for (uint8_t i = 0; i < doneCount; i++) {
    Task t;
    bool present = false;
    for (uint8_t j = 0; j < staged.Count(); j++) {
      if (ReadTask(j, t) && t.id == doneIds[i]) {
        present = true;
        break;
      }
    }
    if (present) {
      doneIds[kept++] = doneIds[i];
    }
  }
  if (kept != doneCount) {
    doneCount = kept;
    SaveState();
  }
}

bool TaskController::ReadTask(uint8_t index, Task& out) const {
  if (!staged.Read(index, &out)) {
    return false;
  }
  out.title[TitleSize - 1] = '\0';
  return true;
}

// ---- completion + streak ----

bool TaskController::IdDone(uint16_t id) const {
  for (uint8_t i = 0; i < doneCount; i++) {
    if (doneIds[i] == id) {
      return true;
    }
  }
  return false;
}

void TaskController::SetIdDone(uint16_t id, bool done) {
  const bool already = IdDone(id);
  if (done && !already && doneCount < MaxTasks) {
    doneIds[doneCount++] = id;
  } else if (!done && already) {
    for (uint8_t i = 0; i < doneCount; i++) {
      if (doneIds[i] == id) {
        doneIds[i] = doneIds[--doneCount];
        break;
      }
    }
  }
}

bool TaskController::IsDoneAt(uint8_t index) const {
  Task t;
  return ReadTask(index, t) && IdDone(t.id);
}

void TaskController::ToggleAt(uint8_t index) {
  Task t;
  if (!ReadTask(index, t)) {
    return;
  }
  SetIdDone(t.id, !IdDone(t.id));
  SaveState();
}

uint8_t TaskController::CompletedCount() const {
  uint8_t done = 0;
  Task t;
  for (uint8_t i = 0; i < GetCount(); i++) {
    if (ReadTask(i, t) && IdDone(t.id)) {
      done++;
    }
  }
  return done;
}

void TaskController::SetStreak(uint16_t value) {
  streak = value;
  SaveState();
}

void TaskController::RollOverDay() {
  const uint32_t today = TodayKey();
  const auto gap = TaskRules::Classify(stateDateKey, today, TaskRules::PreviousDay(today));

  // Nothing to settle against a clock that has gone backwards, and recording
  // that date would end the streak the moment a real one arrives.
  if (!TaskRules::ShouldReanchor(gap)) {
    return;
  }

  // Only a day that ended yesterday needs its completion count, and that count
  // costs a flash read per task; a skipped day is decided without touching the
  // filesystem.
  const bool hadTasks = gap == TaskRules::DayGap::Contiguous && GetCount() > 0;
  const bool allCompleted = hadTasks && CompletedCount() == GetCount();

  streak = TaskRules::NextStreak(streak, gap, hadTasks, allCompleted);
  doneCount = 0;
  stateDateKey = today;
  SaveState();
}

void TaskController::LoadState() {
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, statePath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return; // no state yet -> zeros (RollOverDay in Init sets today)
  }
  StateFile s {};
  const bool ok = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&s), sizeof(s)) == sizeof(s) && s.version == formatVersion;
  fs.FileClose(&file);
  if (!ok) {
    return;
  }
  stateDateKey = s.dateKey;
  streak = s.streak;
  doneCount = s.doneCount > MaxTasks ? MaxTasks : s.doneCount;
  std::memcpy(doneIds, s.doneIds, sizeof(doneIds));
}

void TaskController::SaveState() {
  // Every caller guarantees the flash is powered: CommitStaged and SetStreak
  // hold a SyncWakeLock (which waits for Running), ToggleAt runs from the UI
  // with the screen on, and RollOverDay runs either at boot or inside
  // SystemTask's FlashWakeScope.
  //
  // This used to skip the write whenever the *system* was sleeping, which is a
  // different question from whether the *flash* is powered -- FlashWakeScope
  // wakes the flash without leaving the sleep state -- so the midnight rollover
  // computed a new streak and then silently threw it away.
  FS::Lock lock(fs);
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }
  StateFile s {};
  s.version = formatVersion;
  s.dateKey = stateDateKey;
  s.streak = streak;
  s.doneCount = doneCount;
  std::memcpy(s.doneIds, doneIds, sizeof(s.doneIds));

  lfs_file_t file;
  if (fs.FileOpen(&file, statePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    return;
  }
  fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&s), sizeof(s));
  fs.FileClose(&file);
}
