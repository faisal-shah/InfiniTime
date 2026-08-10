#include "components/ble/DfuService.h"
#include <algorithm>
#include <array>
#include <cstring>
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/settings/Settings.h"
#include "drivers/SpiNorFlash.h"
#include "systemtask/SystemTask.h"
#include <nrf_log.h>

using namespace Pinetime::Controllers;

constexpr ble_uuid128_t DfuService::serviceUuid;
constexpr ble_uuid128_t DfuService::controlPointCharacteristicUuid;
constexpr ble_uuid128_t DfuService::revisionCharacteristicUuid;
constexpr ble_uuid128_t DfuService::packetCharacteristicUuid;

namespace {
  bool CopyMbufRange(const os_mbuf* mbuf, size_t offset, uint8_t* destination, size_t size) {
    if (mbuf == nullptr || destination == nullptr) {
      return false;
    }
    const size_t packetSize = OS_MBUF_PKTLEN(mbuf);
    if (offset > packetSize || size > packetSize - offset) {
      return false;
    }
    return os_mbuf_copydata(mbuf, static_cast<int>(offset), static_cast<int>(size), destination) == 0;
  }

  template <size_t Size>
  bool CopyMbufExact(const os_mbuf* mbuf, std::array<uint8_t, Size>& destination) {
    return mbuf != nullptr && OS_MBUF_PKTLEN(mbuf) == destination.size() && CopyMbufRange(mbuf, 0, destination.data(), destination.size());
  }
}

int DfuServiceCallback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
  auto dfuService = static_cast<DfuService*>(arg);
  return dfuService->OnServiceData(conn_handle, attr_handle, ctxt);
}

void NotificationTimerCallback(TimerHandle_t xTimer) {
  auto notificationManager = static_cast<DfuService::NotificationManager*>(pvTimerGetTimerID(xTimer));
  notificationManager->OnNotificationTimer();
}

void TimeoutTimerCallback(TimerHandle_t xTimer) {
  auto dfuService = static_cast<DfuService*>(pvTimerGetTimerID(xTimer));
  dfuService->OnTimeout();
}

DfuService::DfuService(Pinetime::System::SystemTask& systemTask,
                       Pinetime::Controllers::Ble& bleController,
                       Pinetime::Drivers::SpiNorFlash& spiNorFlash)
  : systemTask {systemTask},
    bleController {bleController},
    dfuImage {spiNorFlash},
    characteristicDefinition {{
                                .uuid = &packetCharacteristicUuid.u,
                                .access_cb = DfuServiceCallback,
                                .arg = this,
                                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                                .val_handle = nullptr,
                              },
                              {
                                .uuid = &controlPointCharacteristicUuid.u,
                                .access_cb = DfuServiceCallback,
                                .arg = this,
                                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                                .val_handle = nullptr,
                              },
                              {
                                .uuid = &revisionCharacteristicUuid.u,
                                .access_cb = DfuServiceCallback,
                                .arg = this,
                                .flags = BLE_GATT_CHR_F_READ,
                                .val_handle = &revision,

                              },
                              {0}

    },
    serviceDefinition {
      {/* Device Information Service */
       .type = BLE_GATT_SVC_TYPE_PRIMARY,
       .uuid = &serviceUuid.u,
       .characteristics = characteristicDefinition},
      {0},
    } {
}

int DfuService::Init() {
  // DfuService is part of a global object graph, so its constructor runs before
  // the scheduler.  Create both timers here, from embedded storage, after boot
  // has reached the optional BLE phase.  A timer failure disables DFU only.
  if (!timeoutTimer.Create("dfuTimeout", pdMS_TO_TICKS(10000), pdFALSE, this, TimeoutTimerCallback) || !notificationManager.Init()) {
    NRF_LOG_ERROR("[DFU] timers unavailable; DFU service disabled");
    available = false;
    return 0;
  }
  available = true;
  const int result = ble_gatts_count_cfg(serviceDefinition);
  return result == 0 ? ble_gatts_add_svcs(serviceDefinition) : result;
}

int DfuService::OnServiceData(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context) {
  if (!available) {
    return BLE_ATT_ERR_INSUFFICIENT_RES;
  }
#ifndef PINETIME_IS_RECOVERY
  if (systemTask.GetSettings().GetDfuAndFsMode() == Pinetime::Controllers::Settings::DfuAndFsMode::Disabled) {
    Pinetime::Controllers::NotificationManager::Notification notif;
    memcpy(notif.message.data(), denyAlert, denyAlertLength);
    notif.size = denyAlertLength;
    notif.category = Pinetime::Controllers::NotificationManager::Categories::SimpleAlert;
    systemTask.GetNotificationManager().Push(std::move(notif));
    (void) systemTask.TryPushMessage(Pinetime::System::Messages::OnNewNotification);
    return BLE_ATT_ERR_INSUFFICIENT_AUTHOR;
  }
#endif

  if (context == nullptr || context->om == nullptr) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  if (bleController.IsFirmwareUpdating()) {
    if (!timeoutTimer.Reset()) {
      NRF_LOG_ERROR("[DFU] timeout timer command failed; aborting update");
      bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
      Reset();
      return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
  }

  if (ble_gatts_find_chr(&serviceUuid.u, &packetCharacteristicUuid.u, nullptr, &packetCharacteristicHandle) != 0 ||
      ble_gatts_find_chr(&serviceUuid.u, &controlPointCharacteristicUuid.u, nullptr, &controlPointCharacteristicHandle) != 0 ||
      ble_gatts_find_chr(&serviceUuid.u, &revisionCharacteristicUuid.u, nullptr, &revisionCharacteristicHandle) != 0) {
    NRF_LOG_ERROR("[DFU] characteristic lookup failed");
    return BLE_ATT_ERR_UNLIKELY;
  }

  if (attributeHandle == packetCharacteristicHandle) {
    if (context->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
      return WritePacketHandler(connectionHandle, context->om);
    else
      return 0;
  } else if (attributeHandle == controlPointCharacteristicHandle) {
    if (context->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
      return ControlPointHandler(connectionHandle, context->om);
    else
      return 0;
  } else if (attributeHandle == revisionCharacteristicHandle) {
    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR)
      return SendDfuRevision(context->om);
    else
      return 0;
  } else {
    NRF_LOG_INFO("[DFU] Unknown Characteristic : %d", attributeHandle);
    return 0;
  }
}

int DfuService::SendDfuRevision(os_mbuf* om) const {
  int res = os_mbuf_append(om, &revision, sizeof(revision));
  return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int DfuService::WritePacketHandler(uint16_t connectionHandle, os_mbuf* om) {
  switch (state) {
    case States::Start: {
      std::array<uint8_t, Dfu::Protocol::StartPacketSize> packet {};
      if (!CopyMbufExact(om, packet)) {
        NRF_LOG_WARNING("[DFU] -> Invalid Start packet length");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      softdeviceSize = Dfu::ReadLittleEndian32(packet.data());
      bootloaderSize = Dfu::ReadLittleEndian32(packet.data() + 4);
      applicationSize = Dfu::ReadLittleEndian32(packet.data() + 8);
      bleController.FirmwareUpdateTotalBytes(applicationSize);
      NRF_LOG_INFO("[DFU] -> Start data received : SD size : %d, BT size : %d, app size : %d",
                   softdeviceSize,
                   bootloaderSize,
                   applicationSize);

      ErrorCodes prepareError = ErrorCodes::NoError;
      if (softdeviceSize != 0 || bootloaderSize != 0) {
        prepareError = ErrorCodes::NotSupported;
      } else if (!DfuImage::IsValidImageSize(applicationSize)) {
        prepareError = ErrorCodes::DataSizeExceedsLimits;
      }

      if (prepareError != ErrorCodes::NoError) {
        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                         static_cast<uint8_t>(Opcodes::StartDFU),
                         static_cast<uint8_t>(prepareError)};
        SendNotification(connectionHandle, data, sizeof(data));
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
        Reset();
        return 0;
      }

      // Wait until SystemTask has disabled sleeping
      // This isn't quite correct, as we don't actually know
      // if BleFirmwareUpdateStarted has been received yet
      constexpr TickType_t sleepDisableTimeout = pdMS_TO_TICKS(1000);
      const TickType_t sleepDisableStarted = xTaskGetTickCount();
      while (!systemTask.IsSleepDisabled() && xTaskGetTickCount() - sleepDisableStarted < sleepDisableTimeout) {
        vTaskDelay(pdMS_TO_TICKS(5));
      }
      if (!systemTask.IsSleepDisabled()) {
        NRF_LOG_ERROR("[DFU] timed out waiting for sleep lock");
        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                         static_cast<uint8_t>(Opcodes::StartDFU),
                         static_cast<uint8_t>(ErrorCodes::OperationFailed)};
        SendNotification(connectionHandle, data, sizeof(data));
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
        Reset();
        return 0;
      }

      const auto prepareResult = dfuImage.Prepare(applicationSize);
      if (prepareResult != DfuImage::PrepareResult::Success) {
        const ErrorCodes error =
          prepareResult == DfuImage::PrepareResult::InvalidSize ? ErrorCodes::DataSizeExceedsLimits : ErrorCodes::OperationFailed;
        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response), static_cast<uint8_t>(Opcodes::StartDFU), static_cast<uint8_t>(error)};
        SendNotification(connectionHandle, data, sizeof(data));
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
        Reset();
        return 0;
      }

      uint8_t data[] {static_cast<uint8_t>(Opcodes::Response),
                      static_cast<uint8_t>(Opcodes::StartDFU),
                      static_cast<uint8_t>(ErrorCodes::NoError)};
      SendNotification(connectionHandle, data, sizeof(data));
      state = States::Init;
    }
      return 0;
    case States::Init: {
      const size_t packetSize = OS_MBUF_PKTLEN(om);
      if (packetSize < Dfu::Protocol::InitHeaderSize + sizeof(expectedCrc)) {
        NRF_LOG_WARNING("[DFU] -> Init packet too short (%lu bytes)",
                        static_cast<unsigned long>(packetSize));
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      std::array<uint8_t, Dfu::Protocol::InitHeaderSize> header {};
      if (!CopyMbufRange(om, 0, header.data(), header.size())) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      const uint16_t softdeviceArrayLength = Dfu::ReadLittleEndian16(header.data() + 8);
      const size_t crcOffset = header.size() + (static_cast<size_t>(softdeviceArrayLength) * sizeof(uint16_t));
      if (crcOffset > packetSize || sizeof(expectedCrc) != packetSize - crcOffset) {
        NRF_LOG_WARNING("[DFU] -> Init packet claims %d softdevices, inconsistent with its length", softdeviceArrayLength);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      std::array<uint8_t, sizeof(expectedCrc)> crcBytes {};
      if (!CopyMbufRange(om, crcOffset, crcBytes.data(), crcBytes.size())) {
        return BLE_ATT_ERR_UNLIKELY;
      }
      expectedCrc = Dfu::ReadLittleEndian16(crcBytes.data());
      initParametersReceived = true;
      initParametersComplete = false;

      NRF_LOG_INFO("[DFU] -> Init data received : deviceType = %d, deviceRevision = %d, applicationVersion = %d, nb SD = %d, CRC = %u",
                   Dfu::ReadLittleEndian16(header.data()),
                   Dfu::ReadLittleEndian16(header.data() + 2),
                   Dfu::ReadLittleEndian32(header.data() + 4),
                   softdeviceArrayLength,
                   expectedCrc);

      return 0;
    }

    case States::Data: {
      const size_t packetSize = OS_MBUF_PKTLEN(om);
      if (!dfuImage.CanAppend(packetSize)) {
        NRF_LOG_WARNING("[DFU] -> Data packet exceeds the remaining image (%lu bytes)",
                        static_cast<unsigned long>(packetSize));
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
        Reset();
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }

      std::array<uint8_t, Dfu::ImageWriteBuffer::BufferSize> packet {};
      size_t offset = 0;
      while (offset < packetSize) {
        const size_t copySize = std::min(packet.size(), packetSize - offset);
        if (!CopyMbufRange(om, offset, packet.data(), copySize) || !dfuImage.Append(packet.data(), copySize)) {
          NRF_LOG_WARNING("[DFU] -> Data copy or flash program failed");
          bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
          Reset();
          return BLE_ATT_ERR_UNLIKELY;
        }
        offset += copySize;
      }

      nbPacketReceived++;
      const size_t bytesReceived = dfuImage.BytesReceived();
      bleController.FirmwareUpdateCurrentBytes(bytesReceived);

      if (Dfu::Protocol::ShouldNotify(nbPacketsToNotify, nbPacketReceived, bytesReceived, applicationSize)) {
        uint8_t data[5] {static_cast<uint8_t>(Opcodes::PacketReceiptNotification),
                         static_cast<uint8_t>(bytesReceived & 0x000000FFu),
                         static_cast<uint8_t>(bytesReceived >> 8u),
                         static_cast<uint8_t>(bytesReceived >> 16u),
                         static_cast<uint8_t>(bytesReceived >> 24u)};
        NRF_LOG_INFO("[DFU] -> Send packet notification: %lu bytes received",
                     static_cast<unsigned long>(bytesReceived));
        SendNotification(connectionHandle, data, 5);
      }
      if (dfuImage.IsComplete()) {
        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                         static_cast<uint8_t>(Opcodes::ReceiveFirmwareImage),
                         static_cast<uint8_t>(ErrorCodes::NoError)};
        NRF_LOG_INFO("[DFU] -> Send packet notification : all bytes received!");
        SendNotification(connectionHandle, data, sizeof(data));
        state = States::Validate;
      }
    }
      return 0;
    default:
      // Invalid state
      return 0;
  }
  return 0;
}

int DfuService::ControlPointHandler(uint16_t connectionHandle, os_mbuf* om) {
  uint8_t opcodeValue = 0;
  if (!CopyMbufRange(om, 0, &opcodeValue, sizeof(opcodeValue))) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  const size_t packetSize = OS_MBUF_PKTLEN(om);
  if (Dfu::Protocol::ControlPacketSize(opcodeValue) == 0) {
    NRF_LOG_WARNING("[DFU] -> Unsupported control opcode %d", opcodeValue);
    return 0;
  }

  std::array<uint8_t, 3> packet {};
  if (!Dfu::Protocol::IsValidControlPacketSize(opcodeValue, packetSize) || !CopyMbufRange(om, 0, packet.data(), packetSize)) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  auto opcode = static_cast<Opcodes>(opcodeValue);
  NRF_LOG_INFO("[DFU] -> ControlPointHandler");

  switch (opcode) {
    case Opcodes::StartDFU: {
      if (state != States::Idle && state != States::Start) {
        NRF_LOG_INFO("[DFU] -> Start DFU requested, but we are not in Idle state");
        return 0;
      }
      if (state == States::Start) {
        NRF_LOG_INFO("[DFU] -> Start DFU requested, but we are already in Start state");
        return 0;
      }
      auto imageType = static_cast<ImageTypes>(packet[1]);
      if (imageType == ImageTypes::Application) {
        NRF_LOG_INFO("[DFU] -> Start DFU, mode = Application");
        dfuImage.Reset();
        initParametersReceived = false;
        initParametersComplete = false;
        state = States::Start;
        bleController.StartFirmwareUpdate();
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Running);
        bleController.FirmwareUpdateTotalBytes(0xffffffffu);
        bleController.FirmwareUpdateCurrentBytes(0);
        if (!systemTask.TryPushMessage(Pinetime::System::Messages::BleFirmwareUpdateStarted)) {
          NRF_LOG_ERROR("[DFU] system queue full; update not started");
          bleController.StopFirmwareUpdate();
          bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
          state = States::Idle;
          return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
      } else {
        NRF_LOG_INFO("[DFU] -> Start DFU, mode %u not supported!",
                     static_cast<unsigned int>(imageType));
        return 0;
      }
    } break;
    case Opcodes::InitDFUParameters: {
      if (state != States::Init) {
        NRF_LOG_INFO("[DFU] -> Init DFU requested, but we are not in Init state");
        return 0;
      }
      if (packet[1] > 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
      }
      bool isInitComplete = packet[1] == 1;
      NRF_LOG_INFO("[DFU] -> Init DFU parameters %s", isInitComplete ? " complete" : " not complete");

      if (isInitComplete) {
        if (!initParametersReceived) {
          uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                           static_cast<uint8_t>(Opcodes::InitDFUParameters),
                           static_cast<uint8_t>(ErrorCodes::InvalidState)};
          SendNotification(connectionHandle, data, sizeof(data));
          return 0;
        }
        initParametersComplete = true;
        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                         static_cast<uint8_t>(Opcodes::InitDFUParameters),
                         static_cast<uint8_t>(ErrorCodes::NoError)};
        SendNotificationAsync(connectionHandle, data, sizeof(data));
        return 0;
      }
      initParametersReceived = false;
      initParametersComplete = false;
    }
      return 0;
    case Opcodes::PacketReceiptNotificationRequest:
      nbPacketsToNotify = Dfu::Protocol::ReadPacketReceiptInterval(packet.data(), packetSize);
      NRF_LOG_INFO("[DFU] -> Receive Packet Notification Request, nb packet = %d", nbPacketsToNotify);
      return 0;
    case Opcodes::ReceiveFirmwareImage:
      if (state != States::Init || !initParametersComplete) {
        NRF_LOG_INFO("[DFU] -> Receive firmware image requested, but we are not in Start Init");
        return 0;
      }
      if (!dfuImage.Init(applicationSize, expectedCrc)) {
        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                         static_cast<uint8_t>(Opcodes::ReceiveFirmwareImage),
                         static_cast<uint8_t>(ErrorCodes::OperationFailed)};
        SendNotification(connectionHandle, data, sizeof(data));
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
        Reset();
        return 0;
      }
      NRF_LOG_INFO("[DFU] -> Starting receive firmware");
      state = States::Data;
      return 0;
    case Opcodes::ValidateFirmware: {
      if (state != States::Validate) {
        NRF_LOG_INFO("[DFU] -> Validate firmware image requested, but we are not in Data state %u",
                     static_cast<unsigned int>(state));
        return 0;
      }

      NRF_LOG_INFO("[DFU] -> Validate firmware image requested -- %d", connectionHandle);

      const auto validation = dfuImage.Validate();
      if (validation == DfuImage::ValidationResult::Success) {
        state = States::Validated;
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Validated);
        NRF_LOG_INFO("Image CRC, MCUboot structure and SHA256 OK");

        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                         static_cast<uint8_t>(Opcodes::ValidateFirmware),
                         static_cast<uint8_t>(ErrorCodes::NoError)};
        SendNotificationAsync(connectionHandle, data, sizeof(data));
      } else {
        const ErrorCodes error =
          validation == DfuImage::ValidationResult::CrcMismatch || validation == DfuImage::ValidationResult::InvalidImage
            ? ErrorCodes::CrcError
            : ErrorCodes::OperationFailed;
        NRF_LOG_WARNING("Image validation failed: %u",
                        static_cast<unsigned int>(validation));

        uint8_t data[3] {static_cast<uint8_t>(Opcodes::Response),
                         static_cast<uint8_t>(Opcodes::ValidateFirmware),
                         static_cast<uint8_t>(error)};
        SendNotification(connectionHandle, data, sizeof(data));
        bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
        Reset();
      }

      return 0;
    }
    case Opcodes::ActivateImageAndReset:
      if (state != States::Validated) {
        NRF_LOG_INFO("[DFU] -> Activate image and reset requested, but we are not in Validated state");
        return 0;
      }
      NRF_LOG_INFO("[DFU] -> Activate image and reset!");
      bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Validated);
      Reset();
      return 0;
    default:
      return 0;
  }
}

void DfuService::OnTimeout() {
  bleController.State(Pinetime::Controllers::Ble::FirmwareUpdateStates::Error);
  Reset();
}

void DfuService::Reset() {
  const bool wasUpdating = bleController.IsFirmwareUpdating();
  state = States::Idle;
  nbPacketsToNotify = 0;
  nbPacketReceived = 0;
  softdeviceSize = 0;
  bootloaderSize = 0;
  applicationSize = 0;
  expectedCrc = 0;
  initParametersReceived = false;
  initParametersComplete = false;
  dfuImage.Reset();
  notificationManager.Reset();
  bleController.StopFirmwareUpdate();
  (void) timeoutTimer.Stop();
  if (wasUpdating && !systemTask.TryPushMessage(Pinetime::System::Messages::BleFirmwareUpdateFinished)) {
    NRF_LOG_ERROR("[DFU] system queue full; finish notification dropped");
  }
}

void DfuService::SendNotification(uint16_t connectionHandle, const uint8_t* data, size_t size) {
  if (!notificationManager.Send(connectionHandle, controlPointCharacteristicHandle, data, size)) {
    NRF_LOG_WARNING("[DFU] notification send failed");
  }
}

void DfuService::SendNotificationAsync(uint16_t connectionHandle, const uint8_t* data, size_t size) {
  if (!notificationManager.AsyncSend(connectionHandle, controlPointCharacteristicHandle, data, size)) {
    NRF_LOG_WARNING("[DFU] deferred notification unavailable; trying immediate send");
    SendNotification(connectionHandle, data, size);
  }
}

bool DfuService::NotificationManager::Init() {
  return timer.Create("dfuNotify", pdMS_TO_TICKS(1000), pdFALSE, this, NotificationTimerCallback);
}

bool DfuService::NotificationManager::AsyncSend(uint16_t connection, uint16_t charactHandle, const uint8_t* data, size_t s) {
  if (!timer.IsCreated() || data == nullptr || size != 0 || s == 0 || s > BufferSize) {
    return false;
  }

  connectionHandle = connection;
  characteristicHandle = charactHandle;
  size = s;
  std::memcpy(buffer, data, size);
  if (!timer.Start()) {
    connectionHandle = 0;
    characteristicHandle = 0;
    size = 0;
    return false;
  }
  return true;
}

void DfuService::NotificationManager::OnNotificationTimer() {
  if (size > 0) {
    const bool sent = Send(connectionHandle, characteristicHandle, buffer, size);
    size = 0;
    if (!sent) {
      NRF_LOG_WARNING("[DFU] deferred notification send failed");
    }
  }
}

bool DfuService::NotificationManager::Send(uint16_t connection, uint16_t charactHandle, const uint8_t* data, size_t s) {
  if (data == nullptr || s == 0) {
    return false;
  }
  auto* om = ble_hs_mbuf_from_flat(data, s);
  if (om == nullptr) {
    return false;
  }
  return ble_gattc_notify_custom(connection, charactHandle, om) == 0;
}

void DfuService::NotificationManager::Reset() {
  connectionHandle = 0;
  characteristicHandle = 0;
  size = 0;
  (void) timer.Stop();
}
