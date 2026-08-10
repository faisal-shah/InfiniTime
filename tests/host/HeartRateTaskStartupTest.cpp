#include "heartratetask/HeartRateTask.h"

#include "components/heartrate/HeartRateController.h"
#include "drivers/Hrs3300.h"

#include <cstdio>

namespace {
  bool queueCreationFails;
  bool taskCreationFails;
  bool queueSendFails;
  int queueCreateCalls;
  int queueDeleteCalls;
  int taskCreateCalls;
  int queueSendCalls;
  int failures;
  UBaseType_t lastQueueLength;
  UBaseType_t lastItemSize;
  Pinetime::Applications::HeartRateTask::Messages lastMessage;

  void ResetFaults() {
    queueCreationFails = false;
    taskCreationFails = false;
    queueSendFails = false;
    queueCreateCalls = 0;
    queueDeleteCalls = 0;
    taskCreateCalls = 0;
    queueSendCalls = 0;
  }

  void Check(bool condition, const char* description) {
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
  queueCreateCalls++;
  lastQueueLength = length;
  lastItemSize = itemSize;
  return queueCreationFails ? nullptr : reinterpret_cast<QueueHandle_t>(uintptr_t {1});
}

void vQueueDelete(QueueHandle_t) {
  queueDeleteCalls++;
}

BaseType_t xQueueSend(QueueHandle_t, const void* item, TickType_t) {
  queueSendCalls++;
  lastMessage = *static_cast<const Pinetime::Applications::HeartRateTask::Messages*>(item);
  return queueSendFails ? pdFALSE : pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) {
  return pdFALSE;
}

BaseType_t xTaskCreate(TaskFunction_t, const char*, uint16_t, void*, UBaseType_t, TaskHandle_t* handle) {
  taskCreateCalls++;
  if (taskCreationFails) {
    return pdFAIL;
  }
  *handle = reinterpret_cast<TaskHandle_t>(uintptr_t {2});
  return pdPASS;
}

TickType_t xTaskGetTickCount() {
  return 0;
}

void vTaskDelay(TickType_t) {
}

int main() {
  using Pinetime::Applications::HeartRateTask;
  using Message = HeartRateTask::Messages;

  Pinetime::Drivers::Hrs3300 sensor;
  Pinetime::Controllers::Settings settings;

  {
    ResetFaults();
    Pinetime::Controllers::HeartRateController controller;
    HeartRateTask task {sensor, controller, settings};
    Check(!task.PushMessage(Message::WakeUp), "non-enable message cannot allocate the lazy task");
    Check(queueCreateCalls == 0 && taskCreateCalls == 0, "non-enable message performs no allocation");
  }

  {
    ResetFaults();
    Pinetime::Controllers::HeartRateController controller;
    HeartRateTask task {sensor, controller, settings};
    Check(!task.PushMessage(Message::Enable), "unavailable sensor rejects enable");
    Check(queueCreateCalls == 0, "unavailable sensor allocates nothing");
  }

  {
    ResetFaults();
    queueCreationFails = true;
    Pinetime::Controllers::HeartRateController controller;
    HeartRateTask task {sensor, controller, settings};
    task.SetSensorAvailable(true);
    Check(!task.PushMessage(Message::Enable), "queue allocation failure is reported");
    Check(taskCreateCalls == 0, "queue allocation failure creates no task");
  }

  {
    ResetFaults();
    taskCreationFails = true;
    Pinetime::Controllers::HeartRateController controller;
    HeartRateTask task {sensor, controller, settings};
    task.SetSensorAvailable(true);
    Check(!task.PushMessage(Message::Enable), "task allocation failure is reported");
    Check(queueDeleteCalls == 1, "task allocation failure releases its queue");
  }

  {
    ResetFaults();
    Pinetime::Controllers::HeartRateController controller;
    HeartRateTask task {sensor, controller, settings};
    task.SetSensorAvailable(true);
    Check(task.PushMessage(Message::Enable), "lazy enable starts and queues successfully");
    Check(task.Started(), "successful lazy enable records a task handle");
    Check(lastQueueLength == 10 && lastItemSize == sizeof(Message), "queue uses the message type size");
    Check(queueSendCalls == 1 && lastMessage == Message::Enable, "task-context queue API receives enable");

    queueSendFails = true;
    Check(!task.PushMessage(Message::WakeUp), "queue saturation is reported to the caller");
  }

  std::printf("%d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
