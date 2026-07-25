#include "components/ble/SyncWakeLock.h"
#include "systemtask/SystemTask.h"
#include <FreeRTOS.h>
#include <task.h>

using namespace Pinetime::Controllers;

SyncWakeLock::SyncWakeLock(System::SystemTask& systemTask) : systemTask {systemTask} {
}

bool SyncWakeLock::WaitUntilAwake() {
  for (uint8_t attempt = 0; systemTask.IsSleeping(); attempt++) {
    if (attempt >= 30) {
      return false;
    }
    vTaskDelay(100);
  }
  return true;
}

bool SyncWakeLock::Acquire() {
  if (held) {
    return true;
  }
  systemTask.PushMessage(System::Messages::StartFileTransfer);
  held = true;
  if (!WaitUntilAwake()) {
    Release();
    return false;
  }
  return true;
}

void SyncWakeLock::Release() {
  if (held) {
    systemTask.PushMessage(System::Messages::StopFileTransfer);
    held = false;
  }
}
