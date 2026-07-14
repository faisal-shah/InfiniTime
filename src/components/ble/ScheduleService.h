#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

int ScheduleServiceCallback(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class ScheduleController;

    // Schedule Service: companion-driven full-replace sync of recurrence rules.
    // Protocol: doc/ScheduleService.md. Runs on the BLE task. Staging records
    // go straight to the flash staging file (FS is mutex-protected), so
    // BeginSync first takes a wake lock via StartFileTransfer to keep the SPI
    // flash powered for the whole transaction; SystemTask does the commit.
    class ScheduleService {
    public:
      ScheduleService(System::SystemTask& systemTask, ScheduleController& scheduleController);

      void Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);
      void OnDisconnect();

    private:
      enum class MessageType : uint8_t { BeginSync = 0, EventRecord = 1, CommitSync = 2, AbortSync = 3 };
      static constexpr uint8_t messageVersion = 0;     // BeginSync / CommitSync / AbortSync
      static constexpr uint8_t eventRecordVersion = 1; // EventRecord (39-byte records)

      int OnSyncCommandWrite(struct ble_gatt_access_ctxt* ctxt);
      int OnDigestRead(struct ble_gatt_access_ctxt* ctxt);
      int OnEventReadAccess(struct ble_gatt_access_ctxt* ctxt);
      bool WaitUntilAwake();
      bool AcquireSyncWakeLock();
      void ReleaseSyncWakeLock();

      // 0006yyxx-78fc-48fe-8e23-433b3a1942d0
      static constexpr ble_uuid128_t CharUuid(uint8_t x, uint8_t y) {
        return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                              .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, y, x, 0x06, 0x00}};
      }

      ble_uuid128_t scheduleUuid {CharUuid(0x00, 0x00)};
      ble_uuid128_t syncCommandCharUuid {CharUuid(0x00, 0x01)};
      ble_uuid128_t digestCharUuid {CharUuid(0x00, 0x02)};
      ble_uuid128_t eventReadCharUuid {CharUuid(0x00, 0x03)};

      const struct ble_gatt_chr_def characteristicDefinition[4];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      ScheduleController& scheduleController;
      uint8_t selectedReadIndex = 0;
      bool syncWakeLockHeld = false;
    };
  }
}
