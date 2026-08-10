#include "components/heartrate/HeartRateController.h"

#include "components/ble/HeartRateService.h"
#include "heartratetask/HeartRateTask.h"

#include <cstdio>

namespace {
  int failures;

  void Check(bool condition, const char* description) {
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }
}

int main() {
  using Pinetime::Controllers::HeartRateController;
  using State = HeartRateController::States;

  HeartRateController controller;
  Check(!controller.Enable(), "enable fails without a registered task");
  Check(controller.State() == State::Stopped, "missing task leaves controller stopped");

  Pinetime::Applications::HeartRateTask task;
  controller.SetHeartRateTask(&task);
  task.sendResult = false;
  Check(!controller.Enable(), "enable reports queue or lazy-start failure");
  Check(controller.State() == State::Stopped, "failed enable rolls state back");

  task.sendResult = true;
  Check(controller.Enable(), "successful enable is reported");
  Check(controller.State() == State::NotEnoughData, "successful enable enters measuring state");

  task.sendResult = false;
  controller.Disable();
  Check(controller.State() == State::NotEnoughData, "failed disable does not claim the sensor stopped");
  task.sendResult = true;
  controller.Disable();
  Check(controller.State() == State::Stopped, "successful disable stops the controller");

  controller.Update(State::Running, 71);
  Check(controller.State() == State::Stopped && controller.HeartRate() == 0,
        "in-flight samples cannot overwrite a queued stop");
  controller.Update(State::Stopped, 0); // sensor task consumed Disable

  controller.Update(State::Running, 72);
  Check(controller.HeartRate() == 72, "updates are safe before BLE service registration");

  Pinetime::Controllers::HeartRateService service;
  controller.SetService(&service);
  controller.Update(State::Running, 73);
  Check(service.notifications == 1 && service.lastValue == 73, "changed value notifies a registered service");
  controller.Update(State::Running, 73);
  Check(service.notifications == 1, "unchanged value is not notified twice");

  std::printf("%d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
