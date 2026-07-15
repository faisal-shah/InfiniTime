#include "components/ble/BeaconService.h"
#include "components/beacon/BeaconController.h"
#include "systemtask/SystemTask.h"
#include <cstring>
#include <nrf_log.h>

using namespace Pinetime::Controllers;

namespace {
  constexpr uint8_t ControlEnable = 0x01;
}

int BeaconServiceCallback(uint16_t /*connHandle*/, uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<BeaconService*>(arg)->OnCommand(ctxt);
}

BeaconService::BeaconService(Pinetime::System::SystemTask& systemTask, BeaconController& beaconController)
  // *_AUTHEN: only a passkey-paired (authenticated, encrypted) central may plant
  // a tracking key or enable beacon mode - same trust model as the other custom
  // services.
  : characteristicDefinition {{.uuid = &keyCharUuid.u,
                               .access_cb = BeaconServiceCallback,
                               .arg = this,
                               .flags =
                                 BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {.uuid = &controlCharUuid.u,
                               .access_cb = BeaconServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &beaconUuid.u, .characteristics = characteristicDefinition}, {0}},
    systemTask {systemTask},
    beaconController {beaconController} {
}

void BeaconService::Init() {
  ble_gatts_count_cfg(serviceDefinition);
  ble_gatts_add_svcs(serviceDefinition);
}

int BeaconService::OnCommand(struct ble_gatt_access_ctxt* ctxt) {
  // Key characteristic: WRITE a 28-byte advertisement key, READ the 1-byte
  // provisioning status.
  if (ble_uuid_cmp(ctxt->chr->uuid, &keyCharUuid.u) == 0) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      uint8_t key[BeaconController::KeySize];
      if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(key)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      if (os_mbuf_copydata(ctxt->om, 0, sizeof(key), key) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      // Stage in RAM; SystemTask commits with the flash awake.
      beaconController.StageKey(key);
      systemTask.PushMessage(System::Messages::BeaconKeyReceived);
      return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      const uint8_t hasKey = beaconController.HasKey() ? 1 : 0;
      const int res = os_mbuf_append(ctxt->om, &hasKey, sizeof(hasKey));
      return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
  }

  // Control characteristic: WRITE 0x01 to enable beacon mode now. Disable is
  // intentionally not offered over BLE - the watch is the escape hatch.
  if (ble_uuid_cmp(ctxt->chr->uuid, &controlCharUuid.u) == 0) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t cmd = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) != 1 || os_mbuf_copydata(ctxt->om, 0, 1, &cmd) != 0) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (cmd != ControlEnable || !beaconController.HasKey()) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    systemTask.PushMessage(System::Messages::BeaconEnable);
    return 0;
  }

  return BLE_ATT_ERR_UNLIKELY;
}
