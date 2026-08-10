#include "components/ble/CompanionManagementService.h"

#include <array>

using namespace Pinetime::Controllers;

int CompanionManagementServiceCallback(uint16_t /*connHandle*/, uint16_t attrHandle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<CompanionManagementService*>(arg)->OnAccess(attrHandle, ctxt);
}

CompanionManagementService::CompanionManagementService(const CompanionStatusProvider& statusProvider)
  // The verify characteristic carries the same bytes as the public status one,
  // but *_READ_AUTHEN forces an authenticated (passkey-paired) link. That gives
  // a companion a way to confirm the same watch answered both reads without
  // exposing any additional data. Not exercisable in the simulator (no SM); the
  // sim reaches OnAccess directly through the GATT bridge.
  : characteristicDefinition {{.uuid = &statusCharUuid.u,
                               .access_cb = CompanionManagementServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ,
                               .val_handle = &statusHandle},
                              {.uuid = &verifyCharUuid.u,
                               .access_cb = CompanionManagementServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN,
                               .val_handle = &verifyHandle},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &serviceUuid.u, .characteristics = characteristicDefinition},
                       {0}},
    statusProvider {statusProvider} {
}

int CompanionManagementService::Init() {
  const int result = ble_gatts_count_cfg(serviceDefinition);
  return result == 0 ? ble_gatts_add_svcs(serviceDefinition) : result;
}

int CompanionManagementService::OnAccess(uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  // Route on the characteristic UUID rather than the runtime attribute handle so
  // the same code path serves the firmware GATT stack and the simulator's GATT
  // bridge, which synthesises the access context from the UUID. Both the public
  // status and the authenticated verify characteristic return the identical
  // payload, so they share one writer.
  if (ble_uuid_cmp(ctxt->chr->uuid, &statusCharUuid.u) == 0 || ble_uuid_cmp(ctxt->chr->uuid, &verifyCharUuid.u) == 0) {
    return WriteStatusPayload(ctxt);
  }
  return BLE_ATT_ERR_UNLIKELY;
}

int CompanionManagementService::WriteStatusPayload(struct ble_gatt_access_ctxt* ctxt) const {
  std::array<uint8_t, CompanionProtocol::CompanionManagementStatusSize> payload {};
  EncodeCompanionStatus(statusProvider.GetCompanionStatus(), payload);
  const int res = os_mbuf_append(ctxt->om, payload.data(), payload.size());
  return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
