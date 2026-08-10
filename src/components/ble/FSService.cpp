#include <nrf_log.h>
#include "FSService.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/settings/Settings.h"
#include "systemtask/SystemTask.h"
#include <algorithm>

using namespace Pinetime::Controllers;

constexpr ble_uuid16_t FSService::fsServiceUuid;
constexpr ble_uuid128_t FSService::fsVersionUuid;
constexpr ble_uuid128_t FSService::fsTransferUuid;

int FSServiceCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  auto* fsService = static_cast<FSService*>(arg);
  return fsService->OnFSServiceRequested(conn_handle, attr_handle, ctxt);
}

FSService::FSService(Pinetime::System::SystemTask& systemTask,
                     Pinetime::System::StorageTask& storageTask)
  : systemTask {systemTask},
    storageTask {storageTask},
    characteristicDefinition {{.uuid = &fsVersionUuid.u,
                               .access_cb = FSServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ,
                               .val_handle = &versionCharacteristicHandle},
                              {
                                .uuid = &fsTransferUuid.u,
                                .access_cb = FSServiceCallback,
                                .arg = this,
                                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                .val_handle = &transferCharacteristicHandle,
                              },
                              {0}},
    serviceDefinition {
      {/* Device Information Service */
       .type = BLE_GATT_SVC_TYPE_PRIMARY,
       .uuid = &fsServiceUuid.u,
       .characteristics = characteristicDefinition},
      {0},
    } {
}

int FSService::Init() {
  const int result = ble_gatts_count_cfg(serviceDefinition);
  return result == 0 ? ble_gatts_add_svcs(serviceDefinition) : result;
}

int FSService::OnFSServiceRequested(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context) {
#ifndef PINETIME_IS_RECOVERY
  if (systemTask.GetSettings().GetDfuAndFsMode() == Pinetime::Controllers::Settings::DfuAndFsMode::Disabled) {
    Pinetime::Controllers::NotificationManager::Notification notif;
    memcpy(notif.message.data(), denyAlert, denyAlertLength);
    notif.size = denyAlertLength;
    notif.category = Pinetime::Controllers::NotificationManager::Categories::SimpleAlert;
    systemTask.GetNotificationManager().Push(std::move(notif));
    systemTask.PushMessage(Pinetime::System::Messages::OnNewNotification);
    return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
  }
#endif

  if (attributeHandle == versionCharacteristicHandle) {
    NRF_LOG_INFO("FS_S : handle = %d", versionCharacteristicHandle);
    int res = os_mbuf_append(context->om, &fsVersion, sizeof(fsVersion));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  if (attributeHandle == transferCharacteristicHandle) {
    return FSCommandHandler(connectionHandle, context->om);
  }
  return 0;
}

int FSService::FSCommandHandler(uint16_t connectionHandle, os_mbuf* om) {
  // A client may write an empty value, and this reads the first byte. Bail
  // before the wake-up below, so an empty write cannot pull the watch out of
  // sleep either.
  if (om->om_len < 1) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  auto command = static_cast<commands>(om->om_data[0]);
  NRF_LOG_INFO("[FS_S] -> FSCommandHandler Command %d", command);
  // Just always make sure we are awake...
  systemTask.PushMessage(Pinetime::System::Messages::StartFileTransfer);
  vTaskDelay(10);
  while (systemTask.IsSleeping()) {
    vTaskDelay(100); // 50ms
  }
  lfs_info info = {0};
  const auto stopAndReturn = [this](int result) {
    systemTask.PushMessage(Pinetime::System::Messages::StopFileTransfer);
    return result;
  };
  switch (command) {
    case commands::READ: {
      NRF_LOG_INFO("[FS_S] -> Read");
      if (OS_MBUF_PKTLEN(om) < sizeof(ReadHeader)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      auto* header = (ReadHeader*) om->om_data;
      uint16_t plen = header->pathlen;
      if (plen >= maxpathlen ||
          OS_MBUF_PKTLEN(om) < sizeof(ReadHeader) + plen) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      memcpy(filepath, header->pathstr, plen);
      filepath[plen] = 0;
      ReadResponse resp {};
      os_mbuf* om;
      resp.command = commands::READ_DATA;
      resp.status = 0x01;
      resp.chunkoff = header->chunkoff;
      int res = storageTask.Stat(filepath, info);
      if (res != LFS_ERR_OK || info.type != LFS_TYPE_REG ||
          header->chunkoff > info.size) {
        resp.status = (int8_t) res;
        resp.chunklen = 0;
        resp.totallen = 0;
        om = ble_hs_mbuf_from_flat(&resp, sizeof(ReadResponse));
      } else {
        resp.chunklen = std::min(
          {header->chunksize,
           info.size - header->chunkoff,
           static_cast<uint32_t>(fileData.size())});
        resp.totallen = info.size;
        uint32_t totalSize = 0;
        res = storageTask.ReadFile(filepath,
                                   header->chunkoff,
                                   fileData.data(),
                                   resp.chunklen,
                                   totalSize);
        if (res < 0) {
          resp.status = static_cast<int8_t>(res);
          resp.chunklen = 0;
        } else {
          resp.chunklen = static_cast<uint32_t>(res);
          resp.totallen = totalSize;
        }
        om = ble_hs_mbuf_from_flat(&resp, sizeof(ReadResponse));
        os_mbuf_append(om, fileData.data(), resp.chunklen);
      }

      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
      break;
    }
    case commands::READ_PACING: {
      NRF_LOG_INFO("[FS_S] -> Readpacing");
      if (OS_MBUF_PKTLEN(om) < sizeof(ReadPacing)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      auto* header = (ReadHeader*) om->om_data;
      ReadResponse resp;
      resp.command = commands::READ_DATA;
      resp.status = 0x01;
      resp.chunkoff = header->chunkoff;
      int res = storageTask.Stat(filepath, info);
      if (res != LFS_ERR_OK || info.type != LFS_TYPE_REG ||
          header->chunkoff > info.size) {
        resp.status = (int8_t) res;
        resp.chunklen = 0;
        resp.totallen = 0;
      } else {
        resp.chunklen = std::min(
          {header->chunksize,
           info.size - header->chunkoff,
           static_cast<uint32_t>(fileData.size())});
        resp.totallen = info.size;
      }
      os_mbuf* om;
      if (resp.chunklen > 0) {
        uint32_t totalSize = 0;
        res = storageTask.ReadFile(filepath,
                                   header->chunkoff,
                                   fileData.data(),
                                   resp.chunklen,
                                   totalSize);
        if (res < 0) {
          resp.status = static_cast<int8_t>(res);
          resp.chunklen = 0;
        } else {
          resp.chunklen = static_cast<uint32_t>(res);
          resp.totallen = totalSize;
        }
        om = ble_hs_mbuf_from_flat(&resp, sizeof(ReadResponse));
        os_mbuf_append(om, fileData.data(), resp.chunklen);
      } else {
        resp.chunklen = 0;
        om = ble_hs_mbuf_from_flat(&resp, sizeof(ReadResponse));
      }
      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
      break;
    }
    case commands::WRITE: {
      NRF_LOG_INFO("[FS_S] -> Write");
      if (OS_MBUF_PKTLEN(om) < sizeof(WriteHeader)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      auto* header = (WriteHeader*) om->om_data;
      uint16_t plen = header->pathlen;
      if (plen >= maxpathlen ||
          OS_MBUF_PKTLEN(om) < sizeof(WriteHeader) + plen) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      memcpy(filepath, header->pathstr, plen);
      filepath[plen] = 0; // Copy and null terminate string
      fileSize = header->totalSize;
      WriteResponse resp {};
      resp.command = commands::WRITE_PACING;
      resp.offset = header->offset;
      resp.modTime = 0;

      const int res = storageTask.EnsureFile(filepath);
      resp.status = res == LFS_ERR_OK ? 0x01 : static_cast<int8_t>(res);
      resp.freespace = std::min<size_t>(
        storageTask.FreeSpace(),
        header->offset <= static_cast<uint32_t>(fileSize)
          ? static_cast<uint32_t>(fileSize) - header->offset
          : 0);
      auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(WriteResponse));
      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
      break;
    }
    case commands::WRITE_DATA: {
      NRF_LOG_INFO("[FS_S] -> WriteData");
      if (OS_MBUF_PKTLEN(om) < sizeof(WritePacing)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      auto* header = (WritePacing*) om->om_data;
      if (header->dataSize > OS_MBUF_PKTLEN(om) - sizeof(WritePacing) ||
          header->dataSize > fileData.size()) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      WriteResponse resp {};
      resp.command = commands::WRITE_PACING;
      resp.offset = header->offset;
      const int res = storageTask.WriteFile(
        filepath, header->offset, header->data, header->dataSize);
      if (res < 0) {
        resp.status = (int8_t) res;
      } else {
        resp.status = 0x01;
      }
      resp.freespace = std::min<size_t>(
        storageTask.FreeSpace(),
        header->offset <= static_cast<uint32_t>(fileSize)
          ? static_cast<uint32_t>(fileSize) - header->offset
          : 0);
      auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(WriteResponse));
      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
      break;
    }
    case commands::DELETE: {
      NRF_LOG_INFO("[FS_S] -> Delete");
      if (OS_MBUF_PKTLEN(om) < sizeof(DelHeader)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      auto* header = (DelHeader*) om->om_data;
      uint16_t plen = header->pathlen;
      if (plen >= maxpathlen ||
          OS_MBUF_PKTLEN(om) < sizeof(DelHeader) + plen) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      char path[maxpathlen] = {0};
      memcpy(path, header->pathstr, plen);
      path[plen] = 0; // Copy and null terminate string
      DelResponse resp {};
      resp.command = commands::DELETE_STATUS;
      int res = storageTask.DeletePath(path);
      resp.status = (res == 0) ? 0x01 : (int8_t) res;
      auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(DelResponse));
      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
      break;
    }
    case commands::MKDIR: {
      NRF_LOG_INFO("[FS_S] -> MKDir");
      if (OS_MBUF_PKTLEN(om) < sizeof(MKDirHeader)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      auto* header = (MKDirHeader*) om->om_data;
      uint16_t plen = header->pathlen;
      if (plen >= maxpathlen ||
          OS_MBUF_PKTLEN(om) < sizeof(MKDirHeader) + plen) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      char path[maxpathlen] = {0};
      memcpy(path, header->pathstr, plen);
      path[plen] = 0; // Copy and null terminate string
      MKDirResponse resp {};
      resp.command = commands::MKDIR_STATUS;
      resp.modification_time = 0;
      int res = storageTask.CreateDirectory(path);
      resp.status = (res == 0) ? 0x01 : (int8_t) res;
      auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(MKDirResponse));
      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
      break;
    }
    case commands::LISTDIR: {
      NRF_LOG_INFO("[FS_S] -> ListDir");
      if (OS_MBUF_PKTLEN(om) < sizeof(ListDirHeader)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      ListDirHeader* header = (ListDirHeader*) om->om_data;
      uint16_t plen = header->pathlen;
      if (plen >= maxpathlen ||
          OS_MBUF_PKTLEN(om) < sizeof(ListDirHeader) + plen) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      char path[maxpathlen] = {0};
      path[plen] = 0; // Copy and null terminate string
      memcpy(path, header->pathstr, plen);

      ListDirResponse resp {};

      resp.command = commands::LISTDIR_ENTRY;
      resp.status = 0x01;
      resp.totalentries = 0;
      resp.entry = 0;
      resp.modification_time = 0;
      uint32_t totalEntries = 0;
      int res = storageTask.ListDirectoryEntry(
        path, 0, info, totalEntries);
      if (res < 0) {
        resp.status = (int8_t) res;
        auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(ListDirResponse));
        ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
        break;
      };
      resp.totalentries = totalEntries;
      while (res > 0) {
        switch (info.type) {
          case LFS_TYPE_REG: {
            resp.flags = 0;
            resp.file_size = info.size;
            break;
          }
          case LFS_TYPE_DIR: {
            resp.flags = 1;
            resp.file_size = 0;
            break;
          }
        }

        // strcpy(resp.path, info.name);
        resp.path_length = strlen(info.name);
        auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(ListDirResponse));
        os_mbuf_append(om, info.name, resp.path_length);
        ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
        /*
         * Todo Figure out how to know when the previous Notify was TX'd
         * For now just delay 100ms to make sure that the data went out...
         */
        vTaskDelay(100); // Allow stuff to actually go out over the BLE conn
        resp.entry++;
        res = storageTask.ListDirectoryEntry(
          path, resp.entry, info, totalEntries);
      }
      resp.file_size = 0;
      resp.path_length = 0;
      resp.flags = 0;
      auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(ListDirResponse));
      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
      break;
    }
    case commands::MOVE: {
      NRF_LOG_INFO("[FS_S] -> Move");
      if (OS_MBUF_PKTLEN(om) < sizeof(MoveHeader)) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      MoveHeader* header = (MoveHeader*) om->om_data;
      const uint16_t plen = header->OldPathLength;
      if (plen >= maxpathlen || header->NewPathLength >= maxpathlen ||
          OS_MBUF_PKTLEN(om) <
            sizeof(MoveHeader) + plen + 1 + header->NewPathLength) {
        return stopAndReturn(BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN);
      }
      char oldPath[maxpathlen] = {0};
      char path[maxpathlen] = {0};
      memcpy(oldPath, header->pathstr, plen);
      memcpy(path, &header->pathstr[plen + 1], header->NewPathLength);
      MoveResponse resp {};
      resp.command = commands::MOVE_STATUS;
      int8_t res =
        static_cast<int8_t>(storageTask.RenamePath(oldPath, path));
      resp.status = (res == 0) ? 1 : res;
      auto* om = ble_hs_mbuf_from_flat(&resp, sizeof(MoveResponse));
      ble_gattc_notify_custom(connectionHandle, transferCharacteristicHandle, om);
    }
    default:
      break;
  }
  NRF_LOG_INFO("[FS_S] -> done ");
  systemTask.PushMessage(Pinetime::System::Messages::StopFileTransfer);
  return 0;
}
