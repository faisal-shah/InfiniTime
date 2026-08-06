#include "components/schedule/ScheduleController.h"

#include "storagetask/StorageTask.h"
#include "systemtask/SystemTask.h"

#include <algorithm>
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  void ReminderTimerCallback(TimerHandle_t timer) {
    static_cast<ScheduleController*>(pvTimerGetTimerID(timer))->TimerFired();
  }
}

ScheduleController::ScheduleController(Controllers::DateTime& dateTimeController,
                                       System::StorageTask& storageTask)
  : dateTimeController {dateTimeController}, storageTask {storageTask} {
}

void ScheduleController::Init(System::SystemTask* systemTask) {
  this->systemTask = systemTask;
  reminderTimer = xTimerCreate("Schedule", 1, pdFALSE, this, ReminderTimerCallback);
  Reschedule();
}

const FamilyState& ScheduleController::Active() const {
  return storageTask.ActiveState();
}

FamilyState* ScheduleController::Candidate() {
  return storageTask.MutableCandidate(CompanionProtocol::FamilyStateOperation::Schedule,
                                      stagedVersion);
}

time_t ScheduleController::Now() const {
  auto now = dateTimeController.CurrentDateTime();
  return std::chrono::system_clock::to_time_t(
    std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
}

bool ScheduleController::BeginStaging(uint8_t count, uint32_t version) {
  if (count > MaxEvents ||
      !storageTask.BeginFamilyStateMutation(CompanionProtocol::FamilyStateOperation::Schedule,
                                            version)) {
    return false;
  }
  stagedVersion = version;
  stagedCount = count;
  stagedReceived = 0;
  staging = true;
  awaitingPersistence = false;

  auto* candidate = Candidate();
  if (candidate == nullptr) {
    DiscardStaging();
    return false;
  }
  candidate->schedules.fill({});
  candidate->scheduleCount = count;
  candidate->scheduleVersion = version;
  return true;
}

bool ScheduleController::StageEvent(uint8_t index, const Event& event) {
  if (!staging || awaitingPersistence || index >= stagedCount) {
    return false;
  }
  const uint64_t bit = uint64_t {1} << index;
  if ((stagedReceived & bit) != 0) {
    return false;
  }
  auto* candidate = Candidate();
  if (candidate == nullptr) {
    return false;
  }
  Event record = event;
  record.title[TitleSize - 1] = '\0';
  candidate->schedules[index] = record;
  stagedReceived |= bit;
  return true;
}

bool ScheduleController::StagingComplete() const {
  if (!staging || awaitingPersistence) {
    return false;
  }
  const uint64_t expected = stagedCount == 64
                              ? ~uint64_t {0}
                              : (uint64_t {1} << stagedCount) - 1;
  return stagedReceived == expected;
}

void ScheduleController::DiscardStaging() {
  if (commitAccepted || awaitingPersistence) {
    return;
  }
  if (staging && !awaitingPersistence) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::Schedule,
      stagedVersion);
  }
  staging = false;
  commitAccepted = false;
  awaitingPersistence = false;
  stagedReceived = 0;
  stagedVersion = 0;
  stagedCount = 0;
}

bool ScheduleController::AcceptCommit() {
  if (!StagingComplete()) {
    return false;
  }
  commitAccepted = true;
  return true;
}

void ScheduleController::CommitStaged() {
  if (!commitAccepted ||
      !storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::Schedule,
        stagedVersion)) {
    DiscardStaging();
    return;
  }
  awaitingPersistence = true;
  commitAccepted = false;
}

void ScheduleController::OnPersisted(uint32_t token, bool success) {
  if (!awaitingPersistence || token != stagedVersion) {
    return;
  }
  awaitingPersistence = false;
  staging = false;
  commitAccepted = false;
  stagedReceived = 0;
  stagedVersion = 0;
  stagedCount = 0;
  if (success) {
    NRF_LOG_INFO("[ScheduleController] Committed %u events, version %u",
                 GetCount(),
                 GetVersion());
    Reschedule();
  }
}

void ScheduleController::Reschedule() {
  xTimerStop(reminderTimer, 0);
  hasNext = false;

  const time_t now = Now();
  const time_t from = now - graceSeconds;
  std::optional<time_t> best;
  const auto& active = Active();
  for (uint8_t index = 0; index < active.scheduleCount; index++) {
    const auto& event = active.schedules[index];
    const auto occurrence = ScheduleRules::NextOccurrenceFrom(event, from);
    if (!occurrence || *occurrence <= lastFiredDue) {
      continue;
    }
    if (!best || *occurrence < *best) {
      best = *occurrence;
      nextHour = event.hour;
      nextMinute = event.minute;
      std::memcpy(nextTitle.data(), event.title, TitleSize);
    }
  }

  if (!best) {
    return;
  }
  hasNext = true;
  nextDueTime = *best;
  ArmTimer(*best - now);
}

void ScheduleController::ArmTimer(int64_t seconds) {
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > maxTimerSeconds) {
    seconds = maxTimerSeconds;
  }
  xTimerChangePeriod(reminderTimer,
                     static_cast<TickType_t>(seconds) * configTICK_RATE_HZ,
                     0);
  xTimerStart(reminderTimer, 0);
}

void ScheduleController::TimerFired() {
  if (!hasNext) {
    return;
  }
  const time_t now = Now();
  if (nextDueTime - now > graceSeconds) {
    ArmTimer(nextDueTime - now);
    return;
  }
  lastFiredDue = nextDueTime;
  systemTask->PushMessage(System::Messages::SetOffScheduleReminder);
}

bool ScheduleController::DescribeFiring(time_t due, char* buffer, size_t bufferSize) {
  if (bufferSize == 0) {
    return false;
  }
  size_t used = 0;
  buffer[0] = '\0';

  const auto& active = Active();
  for (uint8_t index = 0;
       index < active.scheduleCount && used + 1 < bufferSize;
       index++) {
    const auto& event = active.schedules[index];
    const auto occurrence = ScheduleRules::NextOccurrenceFrom(event, due);
    if (!occurrence || *occurrence != due) {
      continue;
    }
    if (used != 0) {
      buffer[used++] = '\n';
    }
    const size_t maxCopy =
      std::min(std::strlen(event.title), bufferSize - used - 1);
    std::memcpy(&buffer[used], event.title, maxCopy);
    used += maxCopy;
  }
  buffer[used] = '\0';
  return used > 0;
}

bool ScheduleController::ReadEvent(uint8_t index, Event& output) const {
  const auto& active = Active();
  if (index >= active.scheduleCount) {
    return false;
  }
  output = active.schedules[index];
  return true;
}

uint8_t ScheduleController::ComputeUpcoming(Occurrence* output,
                                            uint8_t maximum,
                                            uint16_t horizonDays) const {
  if (output == nullptr || maximum == 0) {
    return 0;
  }
  const time_t now = Now();
  const time_t horizon =
    now + static_cast<time_t>(horizonDays) * 24 * 60 * 60;
  uint8_t count = 0;

  const auto& active = Active();
  for (uint8_t index = 0; index < active.scheduleCount; index++) {
    const auto& event = active.schedules[index];
    time_t from = now;
    while (true) {
      const auto occurrence = ScheduleRules::NextOccurrenceFrom(event, from);
      if (!occurrence || *occurrence > horizon) {
        break;
      }
      if (count < maximum || *occurrence < output[count - 1].when) {
        uint8_t position = std::min<uint8_t>(count, maximum - 1);
        while (position > 0 && output[position - 1].when > *occurrence) {
          output[position] = output[position - 1];
          position--;
        }
        output[position].when = *occurrence;
        std::memcpy(output[position].title, event.title, TitleSize);
        if (count < maximum) {
          count++;
        }
      }
      from = *occurrence + 1;
    }
  }
  return count;
}
