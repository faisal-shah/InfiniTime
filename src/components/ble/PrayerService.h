#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

#include "components/ble/CustomServiceUuid.h"

int PrayerServiceCallback(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class PrayerController;

    // Prayer Service: one read/write characteristic carrying the 9-byte
    // settings blob (doc/PrayerService.md). Writes are validated and staged on
    // the BLE task (RAM only) and committed by SystemTask, which persists them
    // with the flash awake; reads serve the current RAM settings. The
    // companion verifies a write by reading the value back.
    class PrayerService {
    public:
      PrayerService(System::SystemTask& systemTask, PrayerController& prayerController);

      int Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);

    private:
      static constexpr uint8_t serviceByte = 0x07;
      ble_uuid128_t prayerUuid {CustomCharUuid(serviceByte, 0x00, 0x00)};
      ble_uuid128_t settingsCharUuid {CustomCharUuid(serviceByte, 0x00, 0x01)};

      const struct ble_gatt_chr_def characteristicDefinition[2];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      PrayerController& prayerController;
    };
  }
}
