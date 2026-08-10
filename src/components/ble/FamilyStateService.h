#pragma once

#include <cstdint>

#define min
#define max
#include <host/ble_gap.h>
#include <host/ble_uuid.h>
#undef max
#undef min

#include "components/ble/CustomServiceUuid.h"

int FamilyStateServiceCallback(uint16_t connHandle,
                               uint16_t attrHandle,
                               struct ble_gatt_access_ctxt* ctxt,
                               void* arg);

namespace Pinetime {
  namespace System {
    class StorageTask;
  }

  namespace Controllers {
    class FamilyStateService {
    public:
      explicit FamilyStateService(const System::StorageTask& storageTask);

      int Init();
      int OnAccess(struct ble_gatt_access_ctxt* ctxt) const;

    private:
      static constexpr uint8_t serviceByte = 0x0c;
      ble_uuid128_t serviceUuid {CustomCharUuid(serviceByte, 0x00, 0x00)};
      ble_uuid128_t statusCharUuid {CustomCharUuid(serviceByte, 0x00, 0x01)};

      const struct ble_gatt_chr_def characteristicDefinition[2];
      const struct ble_gatt_svc_def serviceDefinition[2];
      const System::StorageTask& storageTask;
    };
  }
}
