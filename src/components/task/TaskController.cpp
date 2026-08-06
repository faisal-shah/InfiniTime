#include "components/task/TaskController.h"

#include "components/task/TaskRules.h"
#include "storagetask/StorageTask.h"

#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

TaskController::TaskController(Controllers::DateTime& dateTimeController,
                               System::StorageTask& storageTask)
  : dateTimeController {dateTimeController}, storageTask {storageTask} {
}

const FamilyState& TaskController::Active() const {
  return storageTask.ActiveState();
}

FamilyState* TaskController::Candidate(CompanionProtocol::FamilyStateOperation operation,
                                       uint32_t token) {
  return storageTask.MutableCandidate(operation, token);
}

void TaskController::Init() {
  if (Active().taskRolloverDate != TodayKey()) {
    RollOverDay();
  }
}

void TaskController::Process() {
  if (rolloverRetryPending &&
      static_cast<int32_t>(xTaskGetTickCount() - nextRolloverAttempt) >= 0) {
    rolloverRetryPending = false;
    RollOverDay();
  }
}

uint32_t TaskController::TodayKey() const {
  return static_cast<uint32_t>(dateTimeController.Year()) * 10000 +
         static_cast<uint32_t>(dateTimeController.Month()) * 100 +
         dateTimeController.Day();
}

bool TaskController::BeginStaging(uint8_t count, uint32_t version) {
  if (count > MaxTasks ||
      !storageTask.BeginFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::Tasks,
        version)) {
    return false;
  }
  stagedVersion = version;
  stagedCount = count;
  stagedReceived = 0;
  staging = true;
  awaitingDefinitions = false;
  auto* candidate = Candidate(CompanionProtocol::FamilyStateOperation::Tasks,
                              version);
  if (candidate == nullptr) {
    DiscardStaging();
    return false;
  }
  candidate->tasks.fill({});
  candidate->taskCount = count;
  candidate->taskVersion = version;
  return true;
}

bool TaskController::StageTask(uint8_t index, const Task& task) {
  if (!staging || awaitingDefinitions || index >= stagedCount) {
    return false;
  }
  const uint64_t bit = uint64_t {1} << index;
  if ((stagedReceived & bit) != 0) {
    return false;
  }
  auto* candidate = Candidate(CompanionProtocol::FamilyStateOperation::Tasks,
                              stagedVersion);
  if (candidate == nullptr) {
    return false;
  }
  auto& output = candidate->tasks[index];
  output.id = task.id;
  output.order = task.order;
  std::memcpy(output.title.data(), task.title, TitleSize);
  output.title.back() = '\0';
  output.lastModified = task.lastModified;
  stagedReceived |= bit;
  return true;
}

bool TaskController::StagingComplete() const {
  if (!staging || awaitingDefinitions) {
    return false;
  }
  const uint64_t expected = stagedCount == 64
                              ? ~uint64_t {0}
                              : (uint64_t {1} << stagedCount) - 1;
  return stagedReceived == expected;
}

void TaskController::DiscardStaging() {
  if (commitAccepted || awaitingDefinitions) {
    return;
  }
  if (staging && !awaitingDefinitions) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::Tasks,
      stagedVersion);
  }
  staging = false;
  commitAccepted = false;
  awaitingDefinitions = false;
  stagedReceived = 0;
  stagedVersion = 0;
  stagedCount = 0;
}

bool TaskController::AcceptCommit() {
  if (!StagingComplete()) {
    return false;
  }
  commitAccepted = true;
  return true;
}

void TaskController::CommitStaged() {
  if (!commitAccepted ||
      !storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::Tasks,
        stagedVersion)) {
    DiscardStaging();
    return;
  }
  awaitingDefinitions = true;
  commitAccepted = false;
}

void TaskController::OnPersisted(CompanionProtocol::FamilyStateOperation operation,
                                 uint32_t token,
                                 bool success) {
  if (operation == CompanionProtocol::FamilyStateOperation::Tasks) {
    if (!awaitingDefinitions || token != stagedVersion) {
      return;
    }
    awaitingDefinitions = false;
    staging = false;
    commitAccepted = false;
    stagedReceived = 0;
    stagedVersion = 0;
    stagedCount = 0;
    if (success) {
      PruneTicks();
      NRF_LOG_INFO("[TaskController] Committed %u tasks, version %u",
                   GetCount(),
                   GetVersion());
    }
    return;
  }
  if (operation == CompanionProtocol::FamilyStateOperation::TaskStreak &&
      token == pendingStatsToken) {
    pendingStatsToken = 0;
    if (pendingRollover) {
      pendingRollover = false;
      if (success) {
        doneCount = 0;
      } else {
        rolloverRetryPending = true;
        nextRolloverAttempt =
          xTaskGetTickCount() + pdMS_TO_TICKS(1000);
      }
    }
  }
}

bool TaskController::ReadTask(uint8_t index, Task& output) const {
  const auto& active = Active();
  if (index >= active.taskCount) {
    return false;
  }
  const auto& input = active.tasks[index];
  output.id = input.id;
  output.order = input.order;
  std::memcpy(output.title, input.title.data(), TitleSize);
  output.title[TitleSize - 1] = '\0';
  output.lastModified = input.lastModified;
  return true;
}

bool TaskController::IdDone(uint16_t id) const {
  for (uint8_t index = 0; index < doneCount; index++) {
    if (doneIds[index] == id) {
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
    for (uint8_t index = 0; index < doneCount; index++) {
      if (doneIds[index] == id) {
        doneIds[index] = doneIds[--doneCount];
        break;
      }
    }
  }
}

bool TaskController::IsDoneAt(uint8_t index) const {
  Task task;
  return ReadTask(index, task) && IdDone(task.id);
}

void TaskController::ToggleAt(uint8_t index) {
  Task task;
  if (ReadTask(index, task)) {
    SetIdDone(task.id, !IdDone(task.id));
  }
}

uint8_t TaskController::CompletedCount() const {
  uint8_t count = 0;
  Task task;
  for (uint8_t index = 0; index < GetCount(); index++) {
    if (ReadTask(index, task) && IdDone(task.id)) {
      count++;
    }
  }
  return count;
}

bool TaskController::SetStreak(uint16_t value, uint32_t token) {
  if (!storageTask.BeginFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::TaskStreak,
        token)) {
    return false;
  }
  auto* candidate = Candidate(
    CompanionProtocol::FamilyStateOperation::TaskStreak,
    token);
  if (candidate == nullptr) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::TaskStreak,
      token);
    return false;
  }
  candidate->taskStreak = value;
  pendingStatsToken = token;
  if (!storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::TaskStreak,
        token)) {
    pendingStatsToken = 0;
    return false;
  }
  return true;
}

void TaskController::RollOverDay() {
  const uint32_t today = TodayKey();
  const auto& active = Active();
  const auto gap = TaskRules::Classify(active.taskRolloverDate,
                                       today,
                                       TaskRules::PreviousDay(today));
  if (!TaskRules::ShouldReanchor(gap)) {
    return;
  }
  const bool hadTasks =
    gap == TaskRules::DayGap::Contiguous && GetCount() > 0;
  const bool allCompleted =
    hadTasks && CompletedCount() == GetCount();
  const uint32_t token = today;
  if (!storageTask.BeginFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::TaskStreak,
        token)) {
    rolloverRetryPending = true;
    nextRolloverAttempt = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    return;
  }
  auto* candidate = Candidate(
    CompanionProtocol::FamilyStateOperation::TaskStreak,
    token);
  if (candidate == nullptr) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::TaskStreak,
      token);
    rolloverRetryPending = true;
    nextRolloverAttempt = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    return;
  }
  candidate->taskStreak =
    TaskRules::NextStreak(active.taskStreak, gap, hadTasks, allCompleted);
  candidate->taskRolloverDate = today;
  pendingStatsToken = token;
  pendingRollover = true;
  if (!storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::TaskStreak,
        token)) {
    pendingStatsToken = 0;
    pendingRollover = false;
    rolloverRetryPending = true;
    nextRolloverAttempt = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
  }
}

void TaskController::PruneTicks() {
  uint8_t kept = 0;
  for (uint8_t index = 0; index < doneCount; index++) {
    Task task;
    bool present = false;
    for (uint8_t taskIndex = 0; taskIndex < GetCount(); taskIndex++) {
      if (ReadTask(taskIndex, task) && task.id == doneIds[index]) {
        present = true;
        break;
      }
    }
    if (present) {
      doneIds[kept++] = doneIds[index];
    }
  }
  doneCount = kept;
}
