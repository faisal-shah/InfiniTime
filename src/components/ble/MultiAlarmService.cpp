#include "components/ble/MultiAlarmService.h"
#include "components/multialarm/MultiAlarmController.h"
#include "systemtask/SystemTask.h"
#include <nrf_log.h>

using namespace Pinetime::Controllers;

int MultiAlarmServiceCallback(uint16_t /*connHandle*/, uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<MultiAlarmService*>(arg)->OnCommand(ctxt);
}

MultiAlarmService::MultiAlarmService(Pinetime::System::SystemTask& systemTask, MultiAlarmController& multiAlarmController)
  // *_AUTHEN: same trust model as the Schedule/Prayer Services — only a
  // passkey-paired (authenticated, encrypted) central may read or change alarms.
  : characteristicDefinition {{.uuid = &alarmsCharUuid.u,
                               .access_cb = MultiAlarmServiceCallback,
                               .arg = this,
                               .flags =
                                 BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &multiAlarmUuid.u, .characteristics = characteristicDefinition},
                       {0}},
    systemTask {systemTask},
    multiAlarmController {multiAlarmController} {
}

void MultiAlarmService::Init() {
  ble_gatts_count_cfg(serviceDefinition);
  ble_gatts_add_svcs(serviceDefinition);
}

int MultiAlarmService::OnCommand(struct ble_gatt_access_ctxt* ctxt) {
  if (ble_uuid_cmp(ctxt->chr->uuid, &alarmsCharUuid.u) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t wire[MultiAlarmController::WireSize];
    if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(wire)) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(wire), wire) != 0) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    // Stage in RAM (validates + compare-and-swap); reject synchronously on an
    // invalid field or a version mismatch so the phone re-reads and retries.
    if (!multiAlarmController.StageWire(wire)) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    // SystemTask commits with the flash awake.
    systemTask.PushMessage(System::Messages::MultiAlarmSettingsReceived);
    return 0;
  }

  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    uint8_t wire[MultiAlarmController::WireSize];
    multiAlarmController.Serialize(wire);
    const int res = os_mbuf_append(ctxt->om, wire, sizeof(wire));
    return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  return BLE_ATT_ERR_UNLIKELY;
}
