#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

#include "components/ble/CustomServiceUuid.h"
#include "components/ble/generated/CompanionProtocol.h"

int ScheduleServiceCallback(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class ScheduleController;

    // Schedule Service: companion-driven full-replace sync of recurrence rules.
    // Protocol: doc/ScheduleService.md. Runs on the BLE task. Staging is
    // RAM-only; SystemTask queues the complete candidate to StorageTask.
    class ScheduleService {
    public:
      ScheduleService(System::SystemTask& systemTask, ScheduleController& scheduleController);

      void Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);
      void OnDisconnect();

    private:
      enum class MessageType : uint8_t { BeginSync = 0, EventRecord = 1, CommitSync = 2, AbortSync = 3 };
      static constexpr uint8_t messageVersion = 0;     // BeginSync / CommitSync / AbortSync
      static constexpr uint8_t eventRecordVersion = CompanionProtocol::ScheduleRecordVersion;

      int OnSyncCommandWrite(struct ble_gatt_access_ctxt* ctxt);
      int OnDigestRead(struct ble_gatt_access_ctxt* ctxt);
      int OnEventReadAccess(struct ble_gatt_access_ctxt* ctxt);

      static constexpr uint8_t serviceByte = 0x06;
      ble_uuid128_t scheduleUuid {CustomCharUuid(serviceByte, 0x00, 0x00)};
      ble_uuid128_t syncCommandCharUuid {CustomCharUuid(serviceByte, 0x00, 0x01)};
      ble_uuid128_t digestCharUuid {CustomCharUuid(serviceByte, 0x00, 0x02)};
      ble_uuid128_t eventReadCharUuid {CustomCharUuid(serviceByte, 0x00, 0x03)};

      const struct ble_gatt_chr_def characteristicDefinition[4];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      ScheduleController& scheduleController;
      uint8_t selectedReadIndex = 0;
    };
  }
}
