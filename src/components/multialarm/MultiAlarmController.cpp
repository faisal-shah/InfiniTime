#include "components/multialarm/MultiAlarmController.h"

#include "components/datetime/DateTimeController.h"
#include "storagetask/StorageTask.h"
#include "systemtask/SystemTask.h"

#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  void AlarmTimerCallback(TimerHandle_t timer) {
    static_cast<MultiAlarmController*>(pvTimerGetTimerID(timer))->TimerFired();
  }
}

MultiAlarmController::MultiAlarmController(Controllers::DateTime& dateTimeController,
                                           System::StorageTask& storageTask)
  : dateTimeController {dateTimeController}, storageTask {storageTask} {
}

const FamilyState& MultiAlarmController::Active() const {
  return storageTask.ActiveState();
}

void MultiAlarmController::RefreshCache() {
  const auto& active = Active();
  for (uint8_t index = 0; index < MaxAlarms; index++) {
    const auto& input = active.alarms[index];
    alarmCache[index] = {
      input.hour,
      input.minute,
      input.mode == 1 ? Mode::Daily : Mode::Once,
      input.enabled,
    };
  }
}

void MultiAlarmController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  alarmTimer = xTimerCreate("MultiAlarm", 1, pdFALSE, this, AlarmTimerCallback);
  RefreshCache();
  Reschedule();
}

time_t MultiAlarmController::Now() const {
  auto now = dateTimeController.CurrentDateTime();
  return std::chrono::system_clock::to_time_t(
    std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
}

bool MultiAlarmController::AnyEnabled() const {
  for (const auto& alarm : alarmCache) {
    if (alarm.enabled) {
      return true;
    }
  }
  return false;
}

bool MultiAlarmController::BeginCandidate(uint32_t token) {
  return pendingToken == 0 &&
         storageTask.BeginFamilyStateMutation(
           CompanionProtocol::FamilyStateOperation::MultiAlarm,
           token);
}

bool MultiAlarmController::SetAlarm(uint8_t index, const Alarm& alarm) {
  if (index >= MaxAlarms) {
    return false;
  }
  const uint32_t token =
    Active().alarmVersion == UINT32_MAX ? 1 : Active().alarmVersion + 1;
  if (!BeginCandidate(token)) {
    return false;
  }
  auto* candidate = storageTask.MutableCandidate(
    CompanionProtocol::FamilyStateOperation::MultiAlarm,
    token);
  if (candidate == nullptr) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::MultiAlarm,
      token);
    return false;
  }
  candidate->alarms[index] = {
    alarm.hour,
    alarm.minute,
    static_cast<uint8_t>(alarm.mode),
    alarm.enabled,
  };
  candidate->alarmVersion = token;
  pendingToken = token;
  if (!storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::MultiAlarm,
        token)) {
    pendingToken = 0;
    return false;
  }
  return true;
}

bool MultiAlarmController::SetEnabled(uint8_t index, bool enabled) {
  if (index >= MaxAlarms) {
    return false;
  }
  Alarm alarm = alarmCache[index];
  alarm.enabled = enabled;
  return SetAlarm(index, alarm);
}

MultiAlarmController::StageResult MultiAlarmController::StageWire(
  const uint8_t (&wire)[WireSize]) {
  uint32_t expectedVersion;
  std::memcpy(&expectedVersion, &wire[0], sizeof(expectedVersion));
  if (expectedVersion != Active().alarmVersion) {
    return StageResult::Invalid;
  }
  const uint32_t token =
    expectedVersion == UINT32_MAX ? 1 : expectedVersion + 1;
  if (!BeginCandidate(token)) {
    return StageResult::Busy;
  }
  auto* candidate = storageTask.MutableCandidate(
    CompanionProtocol::FamilyStateOperation::MultiAlarm,
    token);
  if (candidate == nullptr) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::MultiAlarm,
      token);
    return StageResult::Invalid;
  }
  for (uint8_t index = 0; index < MaxAlarms; index++) {
    const size_t offset = 4 + index * 4;
    const uint8_t hour = wire[offset];
    const uint8_t minute = wire[offset + 1];
    const uint8_t mode = wire[offset + 2];
    if (hour > 23 || minute > 59 || mode > 1) {
      storageTask.CancelFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::MultiAlarm,
        token);
      return StageResult::Invalid;
    }
    candidate->alarms[index] = {
      hour,
      minute,
      mode,
      wire[offset + 3] != 0,
    };
  }
  candidate->alarmVersion = token;
  stagedExpectedVersion = expectedVersion;
  stagedValid = true;
  pendingToken = token;
  return StageResult::Accepted;
}

void MultiAlarmController::CommitStagedFromCompanion() {
  if (!stagedValid || stagedExpectedVersion != Active().alarmVersion) {
    if (pendingToken != 0) {
      storageTask.CancelFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::MultiAlarm,
        pendingToken);
    }
    stagedValid = false;
    pendingToken = 0;
    return;
  }
  stagedValid = false;
  if (!storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::MultiAlarm,
        pendingToken)) {
    pendingToken = 0;
  }
}

void MultiAlarmController::OnPersisted(uint32_t token, bool success) {
  if (token != pendingToken) {
    return;
  }
  pendingToken = 0;
  lastCommitSucceeded = success;
  completionCount++;
  if (success) {
    RefreshCache();
    Reschedule();
  }
}

void MultiAlarmController::Serialize(uint8_t (&output)[WireSize]) const {
  const uint32_t version = Active().alarmVersion;
  std::memcpy(&output[0], &version, sizeof(version));
  for (uint8_t index = 0; index < MaxAlarms; index++) {
    const size_t offset = 4 + index * 4;
    const auto& alarm = alarmCache[index];
    output[offset] = alarm.hour;
    output[offset + 1] = alarm.minute;
    output[offset + 2] = static_cast<uint8_t>(alarm.mode);
    output[offset + 3] = alarm.enabled ? 1 : 0;
  }
}

void MultiAlarmController::Reschedule() {
  xTimerStop(alarmTimer, 0);
  hasNext = false;
  const time_t now = Now();
  std::optional<time_t> best;
  for (uint8_t index = 0; index < MaxAlarms; index++) {
    const auto occurrence = MultiAlarmRules::NextOccurrence(alarmCache[index], now);
    if (occurrence && (!best || *occurrence < *best)) {
      best = *occurrence;
      nextIndex = index;
    }
  }
  if (!best) {
    return;
  }
  hasNext = true;
  nextDueTime = *best;
  ArmTimer(*best - now);
}

void MultiAlarmController::ArmTimer(int64_t seconds) {
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > maxTimerSeconds) {
    seconds = maxTimerSeconds;
  }
  xTimerChangePeriod(alarmTimer,
                     static_cast<TickType_t>(seconds) * configTICK_RATE_HZ,
                     0);
  xTimerStart(alarmTimer, 0);
}

void MultiAlarmController::TimerFired() {
  if (!hasNext) {
    return;
  }
  const time_t now = Now();
  if (nextDueTime - now > 60) {
    ArmTimer(nextDueTime - now);
    return;
  }
  lastFiredDue = nextDueTime;
  lastFiredIndex = nextIndex;
  systemTask->PushMessage(System::Messages::SetOffMultiAlarm);
  if (alarmCache[nextIndex].mode == Mode::Daily) {
    Reschedule();
  } else {
    hasNext = false;
  }
}
