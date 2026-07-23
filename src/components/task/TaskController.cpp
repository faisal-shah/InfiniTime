#include "components/task/TaskController.h"
#include "components/fs/FS.h"
#include "systemtask/SystemTask.h"
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

TaskController::TaskController(Controllers::DateTime& dateTimeController, Controllers::FS& fs)
  : dateTimeController {dateTimeController}, fs {fs} {
}

void TaskController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  fs.FileDelete(stagePath); // leftover staging from a power loss mid-sync
  LoadFromFile();
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

// ---- definition staging (mirrors ScheduleController) ----

bool TaskController::BeginStaging(uint8_t newCount, uint32_t version) {
  if (newCount > MaxTasks) {
    ClearStagingState();
    return false;
  }

  FS::Lock lock(fs);
  lfs_dir systemDir;
  if (fs.DirOpen("/.system", &systemDir) != LFS_ERR_OK) {
    fs.DirCreate("/.system");
  } else {
    fs.DirClose(&systemDir);
  }

  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != LFS_ERR_OK) {
    ClearStagingState();
    return false;
  }
  const FileHeader header {formatVersion, newCount, version};
  const bool ok = fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  fs.FileClose(&file);
  if (!ok) {
    fs.FileDelete(stagePath);
    ClearStagingState();
    return false;
  }

  stagingOpen = true;
  stagedCount = newCount;
  stagedVersion = version;
  stagedReceived = 0;
  return true;
}

bool TaskController::StageTask(uint8_t index, const Task& task) {
  if (!stagingOpen || index >= stagedCount) {
    return false;
  }
  const uint64_t bit = 1ull << index;
  if ((stagedReceived & bit) != 0) {
    return false;
  }

  Task record = task;
  record.title[TitleSize - 1] = '\0';

  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, stagePath, LFS_O_WRONLY) != LFS_ERR_OK) {
    return false;
  }
  const uint32_t offset = sizeof(FileHeader) + static_cast<uint32_t>(index) * sizeof(Task);
  const bool ok =
    fs.FileSeek(&file, offset) >= 0 && fs.FileWrite(&file, reinterpret_cast<const uint8_t*>(&record), sizeof(record)) == sizeof(record);
  fs.FileClose(&file);
  if (!ok) {
    return false;
  }

  stagedReceived |= bit;
  return true;
}

bool TaskController::StagingComplete() const {
  if (!stagingOpen) {
    return false;
  }
  const uint64_t all = stagedCount >= 64 ? ~0ull : (1ull << stagedCount) - 1;
  return stagedReceived == all;
}

void TaskController::ClearStagingState() {
  stagingOpen = false;
  stagedReceived = 0;
  stagedCount = 0;
}

void TaskController::DiscardStaging() {
  FS::Lock lock(fs);
  if (stagingOpen) {
    fs.FileDelete(stagePath);
  }
  ClearStagingState();
}

void TaskController::CommitStaged() {
  {
    FS::Lock lock(fs);
    if (!StagingComplete()) {
      DiscardStaging();
      return;
    }
    if (fs.Rename(stagePath, datPath) != LFS_ERR_OK) {
      NRF_LOG_WARNING("[TaskController] Commit rename failed, keeping previous tasks");
      DiscardStaging();
      return;
    }
    count = stagedCount;
    taskVersion = stagedVersion;
    ClearStagingState();
  }
  NRF_LOG_INFO("[TaskController] Committed %u tasks, version %u", count, taskVersion);

  // Drop completion for tasks that no longer exist (ids not in the new list),
  // so stale ticks can't count toward "all done".
  uint8_t kept = 0;
  for (uint8_t i = 0; i < doneCount; i++) {
    Task t;
    bool present = false;
    for (uint8_t j = 0; j < count; j++) {
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
  if (index >= count) {
    return false;
  }
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    return false;
  }
  const uint32_t offset = sizeof(FileHeader) + static_cast<uint32_t>(index) * sizeof(Task);
  const bool ok = fs.FileSeek(&file, offset) >= 0 && fs.FileRead(&file, reinterpret_cast<uint8_t*>(&out), sizeof(out)) == sizeof(out);
  fs.FileClose(&file);
  out.title[TitleSize - 1] = '\0';
  return ok;
}

void TaskController::LoadFromFile() {
  FS::Lock lock(fs);
  lfs_file_t file;
  if (fs.FileOpen(&file, datPath, LFS_O_RDONLY) != LFS_ERR_OK) {
    NRF_LOG_WARNING("[TaskController] No task file");
    return;
  }
  FileHeader header {};
  const bool headerOk = fs.FileRead(&file, reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header) &&
                        header.version == formatVersion && header.count <= MaxTasks;
  fs.FileClose(&file);
  if (!headerOk) {
    NRF_LOG_WARNING("[TaskController] Invalid task file, discarding");
    return;
  }
  lfs_info info {};
  if (fs.Stat(datPath, &info) != LFS_ERR_OK || info.size < sizeof(FileHeader) + static_cast<uint32_t>(header.count) * sizeof(Task)) {
    NRF_LOG_WARNING("[TaskController] Truncated task file, discarding");
    return;
  }
  count = header.count;
  taskVersion = header.taskVersion;
  NRF_LOG_INFO("[TaskController] Loaded %u tasks, version %u", count, taskVersion);
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
  for (uint8_t i = 0; i < count; i++) {
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
  // Evaluate the day that is ending: a full-completion day extends the streak,
  // any incomplete day breaks it. A day with no tasks leaves the streak alone.
  if (count > 0) {
    streak = (CompletedCount() == count) ? static_cast<uint16_t>(streak + 1) : 0;
  }
  doneCount = 0;
  stateDateKey = TodayKey();
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
  // Best-effort: skip when the SPI flash is asleep (e.g. a midnight rollover
  // with the screen off). RAM stays correct and the on-load date check
  // re-derives it after the next boot.
  if (systemTask != nullptr && systemTask->IsSleeping()) {
    return;
  }
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
