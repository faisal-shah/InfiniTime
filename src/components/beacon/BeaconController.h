#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    class FS;

    // Find My / OpenHaystack beacon state. The watch does NO cryptography: it
    // stores one pre-computed 28-byte advertisement key (a P-224 public-key
    // X-coordinate the companion generated) and turns it into a static-random
    // BLE address plus a manufacturer-specific advertising payload.
    //
    // OFF-safety: `active` is RAM-only and defaults false, so the boot/normal
    // advertising path is untouched. The key persists in /.system/findmy.dat;
    // Init reads it once and then this controller is completely inert until the
    // user turns beacon mode on. There is no timer and no background work.
    class BeaconController {
    public:
      static constexpr uint8_t KeySize = 28;

      explicit BeaconController(Controllers::FS& fs);

      // SystemTask, boot (flash awake): load the stored key if present. Does
      // NOT enable beaconing.
      void Init();

      bool HasKey() const {
        return hasKey;
      }

      bool IsBeaconing() const {
        return active;
      }

      // Intent flag, set on SystemTask before NimbleController posts radio
      // reconciliation to the NimBLE host queue. RAM only.
      void SetActive(bool value) {
        active = value;
      }

      // BLE task, RAM only.
      void StageKey(const uint8_t key[KeySize]);
      // SystemTask, flash awake: persist the staged key (atomic rename).
      void CommitStagedKey();

      // Pure builders, no BLE, host-testable. See doc/BeaconService.md.
      // BuildAddress produces the 6-byte value for ble_hs_id_set_rnd in NimBLE
      // host byte order (little-endian: out[0] is the LSB).
      void BuildAddress(uint8_t out[6]) const;
      // BuildPayload produces the raw 31-byte advertising data for
      // ble_gap_adv_set_data.
      void BuildPayload(uint8_t out[31]) const;

    private:
      static constexpr uint8_t formatVersion = 1;
      static constexpr const char* datPath = "/.system/findmy.dat";
      static constexpr const char* stagePath = "/.system/findmy.stg";

      struct __attribute__((packed)) FileContent {
        uint8_t version;
        uint8_t keyPresent;
        uint8_t advKey[KeySize];
      };

      static_assert(sizeof(FileContent) == 30, "findmy.dat layout");

      void SaveToFile();

      Controllers::FS& fs;

      bool active = false; // RAM-only intent; OFF at boot
      bool hasKey = false;
      uint8_t advKey[KeySize] {};

      uint8_t stagedKey[KeySize] {};
      bool stagedValid = false;
    };
  }
}
