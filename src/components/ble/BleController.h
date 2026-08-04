#pragma once

#include <array>
#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    class Ble {
    public:
      using BleAddress = std::array<uint8_t, 6>;
      enum class FirmwareUpdateStates { Idle, Running, Validated, Error };
      enum class AddressTypes { Public, Random, RPA_Public, RPA_Random };

      Ble() = default;
      bool IsConnected() const;
      void Connect();
      void Disconnect();

      bool IsRadioEnabled() const;
      void EnableRadio();
      void DisableRadio();

      void StartFirmwareUpdate();
      void StopFirmwareUpdate();
      void FirmwareUpdateTotalBytes(uint32_t totalBytes);
      void FirmwareUpdateCurrentBytes(uint32_t currentBytes);

      void State(FirmwareUpdateStates state) {
        firmwareUpdateState = state;
      }

      bool IsFirmwareUpdating() const {
        return isFirmwareUpdating;
      }

      uint32_t FirmwareUpdateTotalBytes() const {
        return firmwareUpdateTotalBytes;
      }

      uint32_t FirmwareUpdateCurrentBytes() const {
        return firmwareUpdateCurrentBytes;
      }

      FirmwareUpdateStates State() const {
        return firmwareUpdateState;
      }

      void Address(BleAddress&& addr) {
        address = addr;
      }

      const BleAddress& Address() const {
        return address;
      }

      void AddressType(AddressTypes t) {
        addressType = t;
      }

      // Times the radio had to be nudged back into advertising after it stopped
      // without anything asking it to. Surfaced in Sys Info because the symptom
      // -- a watch no scan can see, with a working screen -- is otherwise
      // indistinguishable from a phone-side Bluetooth problem. Any non-zero
      // value means the watch hit that state and recovered itself.
      void RecordAdvertisingRecovery() {
        if (advertisingRecoveries < UINT8_MAX) {
          advertisingRecoveries++;
        }
      }

      uint8_t AdvertisingRecoveries() const {
        return advertisingRecoveries;
      }

      void SetPairingKey(uint32_t k) {
        pairingKey = k;
      }

      uint32_t GetPairingKey() const {
        return pairingKey;
      }

    private:
      bool isConnected = false;
      bool isRadioEnabled = true;
      bool isFirmwareUpdating = false;
      uint32_t firmwareUpdateTotalBytes = 0;
      uint32_t firmwareUpdateCurrentBytes = 0;
      FirmwareUpdateStates firmwareUpdateState = FirmwareUpdateStates::Idle;
      BleAddress address;
      AddressTypes addressType;
      uint32_t pairingKey = 0;
      uint8_t advertisingRecoveries = 0;
    };
  }
}
