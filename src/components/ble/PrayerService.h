#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

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

      void Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);

    private:
      // 0007yyxx-78fc-48fe-8e23-433b3a1942d0
      static constexpr ble_uuid128_t CharUuid(uint8_t x, uint8_t y) {
        return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                              .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, y, x, 0x07, 0x00}};
      }

      ble_uuid128_t prayerUuid {CharUuid(0x00, 0x00)};
      ble_uuid128_t settingsCharUuid {CharUuid(0x00, 0x01)};

      const struct ble_gatt_chr_def characteristicDefinition[2];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      PrayerController& prayerController;
    };
  }
}
