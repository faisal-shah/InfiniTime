#include "components/ble/FamilyStateService.h"

#include "storagetask/StorageTask.h"

using Pinetime::Controllers::FamilyStateService;

int FamilyStateServiceCallback(uint16_t /*connHandle*/,
                               uint16_t /*attrHandle*/,
                               struct ble_gatt_access_ctxt* ctxt,
                               void* arg) {
  return static_cast<FamilyStateService*>(arg)->OnAccess(ctxt);
}

FamilyStateService::FamilyStateService(const System::StorageTask& storageTask)
  : characteristicDefinition {{.uuid = &statusCharUuid.u,
                               .access_cb = FamilyStateServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY,
                        .uuid = &serviceUuid.u,
                        .characteristics = characteristicDefinition},
                       {0}},
    storageTask {storageTask} {
}

void FamilyStateService::Init() {
  int result = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(result == 0);
  result = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(result == 0);
}

int FamilyStateService::OnAccess(struct ble_gatt_access_ctxt* ctxt) const {
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR ||
      ble_uuid_cmp(ctxt->chr->uuid, &statusCharUuid.u) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  const auto payload = storageTask.Status().Encode();
  const int result = os_mbuf_append(ctxt->om, payload.data(), payload.size());
  return result == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
