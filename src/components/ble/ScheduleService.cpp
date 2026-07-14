#include "components/ble/ScheduleService.h"
#include "components/schedule/ScheduleController.h"
#include "systemtask/SystemTask.h"
#include <cstring>
#include <nrf_log.h>

using namespace Pinetime::Controllers;

int ScheduleServiceCallback(uint16_t /*connHandle*/, uint16_t /*attrHandle*/, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  return static_cast<ScheduleService*>(arg)->OnCommand(ctxt);
}

ScheduleService::ScheduleService(Pinetime::System::SystemTask& systemTask, ScheduleController& scheduleController)
  // *_AUTHEN requires an authenticated (passkey-paired) encrypted link for every
  // access. On an unpaired connection NimBLE returns "insufficient
  // authentication", which prompts the central to pair — the watch shows its
  // 6-digit passkey, and only a device that entered it can read or write the
  // schedule. This reuses InfiniTime's existing Security Manager; it cannot be
  // exercised in the simulator (no radio/SM), only on hardware.
  : characteristicDefinition {{.uuid = &syncCommandCharUuid.u,
                               .access_cb = ScheduleServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {.uuid = &digestCharUuid.u,
                               .access_cb = ScheduleServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_AUTHEN},
                              {.uuid = &eventReadCharUuid.u,
                               .access_cb = ScheduleServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                                        BLE_GATT_CHR_F_READ_AUTHEN | BLE_GATT_CHR_F_WRITE_AUTHEN},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &scheduleUuid.u, .characteristics = characteristicDefinition}, {0}},
    systemTask {systemTask},
    scheduleController {scheduleController} {
}

void ScheduleService::Init() {
  ble_gatts_count_cfg(serviceDefinition);
  ble_gatts_add_svcs(serviceDefinition);
}

int ScheduleService::OnCommand(struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && ble_uuid_cmp(ctxt->chr->uuid, &syncCommandCharUuid.u) == 0) {
    return OnSyncCommandWrite(ctxt);
  }
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && ble_uuid_cmp(ctxt->chr->uuid, &digestCharUuid.u) == 0) {
    return OnDigestRead(ctxt);
  }
  if (ble_uuid_cmp(ctxt->chr->uuid, &eventReadCharUuid.u) == 0) {
    return OnEventReadAccess(ctxt);
  }
  return BLE_ATT_ERR_UNLIKELY;
}

int ScheduleService::OnSyncCommandWrite(struct ble_gatt_access_ctxt* ctxt) {
  const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  // Largest message: EventRecord = 3-byte header + 35-byte record.
  uint8_t buffer[3 + sizeof(ScheduleController::Event)];
  if (len < 2 || len > sizeof(buffer)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  if (os_mbuf_copydata(ctxt->om, 0, len, buffer) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (static_cast<MessageType>(buffer[0]) != MessageType::EventRecord && buffer[1] != messageVersion) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  switch (static_cast<MessageType>(buffer[0])) {
    case MessageType::BeginSync: {
      if (len != 7) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      const uint8_t count = buffer[2];
      if (count > ScheduleController::MaxEvents) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      uint32_t version;
      std::memcpy(&version, &buffer[3], sizeof(version));
      scheduleController.BeginStaging(count, version);
      return 0;
    }

    case MessageType::EventRecord: {
      if (buffer[1] != eventRecordVersion) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      if (len != 3 + sizeof(ScheduleController::Event)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      ScheduleController::Event event;
      std::memcpy(&event, &buffer[3], sizeof(event));
      if (!scheduleController.StageEvent(buffer[2], event)) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      return 0;
    }

    case MessageType::CommitSync: {
      if (len != 3) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      if (buffer[2] != scheduleController.GetStagedCount() || !scheduleController.StagingComplete()) {
        scheduleController.DiscardStaging();
        return BLE_ATT_ERR_UNLIKELY;
      }
      // Commit (flash write + timer re-arm) must run on the SystemTask.
      systemTask.PushMessage(System::Messages::ScheduleSyncReceived);
      return 0;
    }

    case MessageType::AbortSync:
      scheduleController.DiscardStaging();
      return 0;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

int ScheduleService::OnDigestRead(struct ble_gatt_access_ctxt* ctxt) {
  uint8_t digest[7];
  digest[0] = ScheduleController::ProtocolVersion;
  digest[1] = ScheduleController::MaxEvents;
  digest[2] = scheduleController.GetCount();
  const uint32_t version = scheduleController.GetVersion();
  std::memcpy(&digest[3], &version, sizeof(version));
  const int res = os_mbuf_append(ctxt->om, digest, sizeof(digest));
  return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// Pull half of multi-companion sync: write an index to select, read to fetch
// that event's record. The BLE connection is exclusive, so select+read pairs
// cannot interleave between companions.
int ScheduleService::OnEventReadAccess(struct ble_gatt_access_ctxt* ctxt) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    uint8_t index;
    if (OS_MBUF_PKTLEN(ctxt->om) != 1 || os_mbuf_copydata(ctxt->om, 0, 1, &index) != 0) {
      return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (index >= scheduleController.GetCount()) {
      return BLE_ATT_ERR_UNLIKELY;
    }
    selectedReadIndex = index;
    return 0;
  }

  if (selectedReadIndex >= scheduleController.GetCount()) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  const ScheduleController::Event& event = scheduleController.GetEvent(selectedReadIndex);
  const int res = os_mbuf_append(ctxt->om, &event, sizeof(event));
  return res == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

void ScheduleService::OnDisconnect() {
  scheduleController.DiscardStaging();
}
