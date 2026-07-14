#include "components/ble/PrayerService.h"
#include "components/prayer/PrayerController.h"
#include "systemtask/SystemTask.h"
#include <cstring>
#include <nrf_log.h>

using namespace Pinetime::Controllers;

int PrayerServiceCallback(uint16_t /*connHandle*/, uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<PrayerService*>(arg)->OnCommand(ctxt);
}

PrayerService::PrayerService(Pinetime::System::SystemTask& systemTask, PrayerController& prayerController)
  // *_AUTHEN: same trust model as the Schedule Service - only a passkey-paired
  // (authenticated, encrypted) central can read or change prayer settings.
  : characteristicDefinition {{.uuid = &settingsCharUuid.u,
                               .access_cb = PrayerServiceCallback,
                               .arg = this,
                               .flags =
                                 BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &prayerUuid.u, .characteristics = characteristicDefinition}, {0}},
    systemTask {systemTask},
    prayerController {prayerController} {
}

void PrayerService::Init() {
  ble_gatts_count_cfg(serviceDefinition);
  ble_gatts_add_svcs(serviceDefinition);
}

int PrayerService::OnCommand(struct ble_gatt_access_ctxt* ctxt) {
  if (ble_uuid_cmp(ctxt->chr->uuid, &settingsCharUuid.u) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    PrayerController::Settings settings;
    if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(settings)) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(settings), &settings) != 0) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    if (!PrayerController::Validate(settings)) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    // Stage in RAM here; SystemTask commits with the flash awake. The
    // companion confirms by reading the value back.
    prayerController.StageSettings(settings);
    systemTask.PushMessage(System::Messages::PrayerSettingsReceived);
    return 0;
  }

  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    const PrayerController::Settings& settings = prayerController.GetSettings();
    const int res = os_mbuf_append(ctxt->om, &settings, sizeof(settings));
    return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  return BLE_ATT_ERR_UNLIKELY;
}
