#include "components/heartrate/HeartRateController.h"
#include <heartratetask/HeartRateTask.h>
#include <systemtask/SystemTask.h>

using namespace Pinetime::Controllers;

void HeartRateController::Update(HeartRateController::States newState, uint8_t heartRate) {
  if (newState != States::Stopped &&
      stopRequested.load(std::memory_order_acquire)) {
    return;
  }
  this->state.store(newState, std::memory_order_relaxed);
  if (this->heartRate.load(std::memory_order_relaxed) != heartRate) {
    this->heartRate.store(heartRate, std::memory_order_relaxed);
    if (service != nullptr) {
      service->OnNewHeartRateValue(heartRate);
    }
  }
  if (newState == States::Stopped) {
    stopRequested.store(false, std::memory_order_release);
  }
}

bool HeartRateController::Enable() {
  if (task == nullptr) {
    return false;
  }
  stopRequested.store(false, std::memory_order_release);
  state.store(States::NotEnoughData, std::memory_order_relaxed);
  if (task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::Enable)) {
    return true;
  }
  state.store(States::Stopped, std::memory_order_relaxed);
  return false;
}

void HeartRateController::Disable() {
  if (task == nullptr) {
    return;
  }
  stopRequested.store(true, std::memory_order_release);
  if (task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::Disable)) {
    state.store(States::Stopped, std::memory_order_relaxed);
  } else {
    stopRequested.store(false, std::memory_order_release);
  }
}

void HeartRateController::SetHeartRateTask(Pinetime::Applications::HeartRateTask* task) {
  this->task = task;
}

void HeartRateController::SetService(Pinetime::Controllers::HeartRateService* service) {
  this->service = service;
}
