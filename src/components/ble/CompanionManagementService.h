#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

#include "components/ble/CompanionManagementStatus.h"
#include "components/ble/CustomServiceUuid.h"

int CompanionManagementServiceCallback(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg);

namespace Pinetime {
  namespace Controllers {
    // Companion Management service (0x0b): a read-only diagnostic window onto the
    // bond store the phone owns. The status characteristic (0x0b, 0x00, 0x01) is
    // public read; the verify characteristic (0x0b, 0x00, 0x02) additionally
    // requires an authenticated link. Both return the identical 20-byte payload,
    // so a companion can read it before pairing and re-read it over an
    // authenticated link to prove the same watch answered. The payload carries
    // counts and coarse flags only -- never an identity, key, or name -- and the
    // service exposes no write, so there is no BLE path to forget a peer.
    class CompanionManagementService {
    public:
      explicit CompanionManagementService(const CompanionStatusProvider& statusProvider);

      void Init();
      int OnAccess(uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt);

    private:
      int WriteStatusPayload(struct ble_gatt_access_ctxt* ctxt) const;

      static constexpr uint8_t serviceByte = 0x0b;
      ble_uuid128_t serviceUuid {CustomCharUuid(serviceByte, 0x00, 0x00)};
      ble_uuid128_t statusCharUuid {CustomCharUuid(serviceByte, 0x00, 0x01)};
      ble_uuid128_t verifyCharUuid {CustomCharUuid(serviceByte, 0x00, 0x02)};

      const struct ble_gatt_chr_def characteristicDefinition[3];
      const struct ble_gatt_svc_def serviceDefinition[2];

      uint16_t statusHandle = 0;
      uint16_t verifyHandle = 0;

      const CompanionStatusProvider& statusProvider;
    };
  }
}
