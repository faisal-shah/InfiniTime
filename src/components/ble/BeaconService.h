#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

#include "components/ble/CustomServiceUuid.h"

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

      int Init();
      int OnCommand(struct ble_gatt_access_ctxt* ctxt);

    private:
      static constexpr uint8_t serviceByte = 0x08;
      ble_uuid128_t beaconUuid {CustomCharUuid(serviceByte, 0x00, 0x00)};
      ble_uuid128_t keyCharUuid {CustomCharUuid(serviceByte, 0x00, 0x01)};
      ble_uuid128_t controlCharUuid {CustomCharUuid(serviceByte, 0x00, 0x02)};

      const struct ble_gatt_chr_def characteristicDefinition[3];
      const struct ble_gatt_svc_def serviceDefinition[2];

      System::SystemTask& systemTask;
      BeaconController& beaconController;
    };
  }
}
