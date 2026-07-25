#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

#include "components/ble/CustomServiceUuid.h"
#include "components/ble/SyncWakeLock.h"

int TaskServiceCallback(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class TaskController;

    // Task Service: companion-driven full-replace sync of the daily task list
    // (the twin of ScheduleService). Same staging/commit + wake-lock model; the
    // digest additionally reports the completion STREAK, and a SetStreak message
    // lets the companion override it.
    class TaskService {
    public:
      TaskService(System::SystemTask& systemTask, TaskController& taskController);

      void Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);
      void OnDisconnect();

    private:
      enum class MessageType : uint8_t { BeginSync = 0, TaskRecord = 1, CommitSync = 2, AbortSync = 3, SetStreak = 4 };
      static constexpr uint8_t messageVersion = 0;    // Begin/Commit/Abort/SetStreak
      static constexpr uint8_t taskRecordVersion = 1; // TaskRecord (31-byte records)

      int OnSyncCommandWrite(struct ble_gatt_access_ctxt* ctxt);
      int OnDigestRead(struct ble_gatt_access_ctxt* ctxt);
      int OnTaskReadAccess(struct ble_gatt_access_ctxt* ctxt);

      static constexpr uint8_t serviceByte = 0x0a; // 0x07 is Prayer
      ble_uuid128_t taskUuid {CustomCharUuid(serviceByte, 0x00, 0x00)};
      ble_uuid128_t syncCommandCharUuid {CustomCharUuid(serviceByte, 0x00, 0x01)};
      ble_uuid128_t digestCharUuid {CustomCharUuid(serviceByte, 0x00, 0x02)};
      ble_uuid128_t taskReadCharUuid {CustomCharUuid(serviceByte, 0x00, 0x03)};

      const struct ble_gatt_chr_def characteristicDefinition[4];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      TaskController& taskController;
      uint8_t selectedReadIndex = 0;
      SyncWakeLock wakeLock;
    };
  }
}
