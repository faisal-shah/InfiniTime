#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

int BeaconServiceCallback(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class BeaconController;

    // Beacon (Find My) Service: provisions the 28-byte advertisement key and
    // optionally enables beacon mode. Used only in normal/connectable mode; once
    // beacon mode is on the watch is non-connectable and this service cannot be
    // reached (turning it off is a watch-side action). doc/BeaconService.md.
    class BeaconService {
    public:
      BeaconService(System::SystemTask& systemTask, BeaconController& beaconController);

      void Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);

    private:
      // 0008yyxx-78fc-48fe-8e23-433b3a1942d0
      static constexpr ble_uuid128_t CharUuid(uint8_t x, uint8_t y) {
        return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                              .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, y, x, 0x08, 0x00}};
      }

      ble_uuid128_t beaconUuid {CharUuid(0x00, 0x00)};
      ble_uuid128_t keyCharUuid {CharUuid(0x00, 0x01)};
      ble_uuid128_t controlCharUuid {CharUuid(0x00, 0x02)};

      const struct ble_gatt_chr_def characteristicDefinition[3];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      BeaconController& beaconController;
    };
  }
}
