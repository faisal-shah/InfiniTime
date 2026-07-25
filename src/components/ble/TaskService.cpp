#include "components/ble/TaskService.h"
#include "components/task/TaskController.h"
#include "systemtask/SystemTask.h"
#include <FreeRTOS.h>
#include <task.h>
#include <cstring>
#include <nrf_log.h>

using namespace Pinetime::Controllers;

int TaskServiceCallback(uint16_t /*connHandle*/, uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<TaskService*>(arg)->OnCommand(ctxt);
}

TaskService::TaskService(Pinetime::System::SystemTask& systemTask, TaskController& taskController)
  // *_AUTHEN requires an authenticated (passkey-paired) encrypted link — same
  // as ScheduleService. Not exercisable in the simulator (no SM), only on
  // hardware; the sim reaches OnCommand directly through the GATT bridge.
  : characteristicDefinition {{.uuid = &syncCommandCharUuid.u,
                               .access_cb = TaskServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {.uuid = &digestCharUuid.u,
                               .access_cb = TaskServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN},
                              {.uuid = &taskReadCharUuid.u,
                               .access_cb = TaskServiceCallback,
                               .arg = this,
                               .flags =
                                 BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &taskUuid.u, .characteristics = characteristicDefinition}, {0}},
    systemTask {systemTask},
    taskController {taskController},
    wakeLock {systemTask} {
}

void TaskService::Init() {
  ble_gatts_count_cfg(serviceDefinition);
  ble_gatts_add_svcs(serviceDefinition);
}

int TaskService::OnCommand(struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && ble_uuid_cmp(ctxt->chr->uuid, &syncCommandCharUuid.u) == 0) {
    return OnSyncCommandWrite(ctxt);
  }
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && ble_uuid_cmp(ctxt->chr->uuid, &digestCharUuid.u) == 0) {
    return OnDigestRead(ctxt);
  }
  if (ble_uuid_cmp(ctxt->chr->uuid, &taskReadCharUuid.u) == 0) {
    return OnTaskReadAccess(ctxt);
  }
  return BLE_ATT_ERR_UNLIKELY;
}

int TaskService::OnSyncCommandWrite(struct ble_gatt_access_ctxt* ctxt) {
  const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  uint8_t buffer[3 + sizeof(TaskController::Task)]; // largest: TaskRecord
  if (len < 2 || len > sizeof(buffer)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  if (os_mbuf_copydata(ctxt->om, 0, len, buffer) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (static_cast<MessageType>(buffer[0]) != MessageType::TaskRecord && buffer[1] != messageVersion) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  switch (static_cast<MessageType>(buffer[0])) {
    case MessageType::BeginSync: {
      if (len != 7) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      const uint8_t count = buffer[2];
      if (count > TaskController::MaxTasks) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      if (!wakeLock.Acquire()) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      uint32_t version;
      std::memcpy(&version, &buffer[3], sizeof(version));
      if (!taskController.BeginStaging(count, version)) {
        wakeLock.Release();
        return BLE_ATT_ERR_UNLIKELY;
      }
      return 0;
    }

    case MessageType::TaskRecord: {
      if (buffer[1] != taskRecordVersion) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      if (len != 3 + sizeof(TaskController::Task)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      TaskController::Task task;
      std::memcpy(&task, &buffer[3], sizeof(task));
      if (!taskController.StageTask(buffer[2], task)) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      return 0;
    }

    case MessageType::CommitSync: {
      if (len != 3) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      if (buffer[2] != taskController.GetStagedCount() || !taskController.StagingComplete()) {
        taskController.DiscardStaging();
        wakeLock.Release();
        return BLE_ATT_ERR_UNLIKELY;
      }
      systemTask.PushMessage(System::Messages::TaskSyncReceived);
      wakeLock.Release();
      return 0;
    }

    case MessageType::AbortSync:
      taskController.DiscardStaging();
      wakeLock.Release();
      return 0;

    case MessageType::SetStreak: {
      if (len != 4) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      uint16_t value;
      std::memcpy(&value, &buffer[2], sizeof(value));
      // SetStreak writes the state file; keep the flash powered for the write.
      const bool ownWake = !wakeLock.Held();
      if (ownWake && !wakeLock.Acquire()) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      taskController.SetStreak(value);
      if (ownWake) {
        wakeLock.Release();
      }
      return 0;
    }
  }
  return BLE_ATT_ERR_UNLIKELY;
}

int TaskService::OnDigestRead(struct ble_gatt_access_ctxt* ctxt) {
  uint8_t digest[9];
  digest[0] = TaskController::ProtocolVersion;
  digest[1] = TaskController::MaxTasks;
  digest[2] = taskController.GetCount();
  const uint32_t version = taskController.GetVersion();
  std::memcpy(&digest[3], &version, sizeof(version));
  const uint16_t streak = taskController.GetStreak();
  std::memcpy(&digest[7], &streak, sizeof(streak));
  const int res = os_mbuf_append(ctxt->om, digest, sizeof(digest));
  return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int TaskService::OnTaskReadAccess(struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t index;
    if (OS_MBUF_PKTLEN(ctxt->om) != 1 || os_mbuf_copydata(ctxt->om, 0, 1, &index) != 0) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (index >= taskController.GetCount()) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    selectedReadIndex = index;
    return 0;
  }

  if (selectedReadIndex >= taskController.GetCount()) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  const bool ownWake = !wakeLock.Held();
  if (ownWake) {
    systemTask.PushMessage(System::Messages::StartFileTransfer);
    if (!wakeLock.WaitUntilAwake()) {
      systemTask.PushMessage(System::Messages::StopFileTransfer);
      return BLE_ATT_ERR_UNLIKELY;
    }
  }
  TaskController::Task task;
  const bool ok = taskController.ReadTask(selectedReadIndex, task);
  if (ownWake) {
    systemTask.PushMessage(System::Messages::StopFileTransfer);
  }
  if (!ok) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  const int res = os_mbuf_append(ctxt->om, &task, sizeof(task));
  return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

void TaskService::OnDisconnect() {
  taskController.DiscardStaging();
  wakeLock.Release();
}
