#pragma once

#include "components/fs/FamilyState.h"

#include <cstdint>

namespace Pinetime {
  namespace System {
    class StorageTask;
  }
  namespace Controllers {
    // Find My / OpenHaystack beacon state. The watch does NO cryptography: it
    // stores one pre-computed 28-byte advertisement key (a P-224 public-key
    // X-coordinate the companion generated) and turns it into a static-random
    // BLE address plus a manufacturer-specific advertising payload.
    //
    // OFF-safety: `active` is RAM-only and defaults false, so the boot/normal
    // advertising path is untouched. The key is read from the active
    // family-state snapshot. There is no timer and no background work.
    class BeaconController {
    public:
      static constexpr uint8_t KeySize = 28;

      explicit BeaconController(System::StorageTask& storageTask);

      // Does not enable beaconing.
      void Init() {
      }

      bool HasKey() const;

      bool IsBeaconing() const {
        return active;
      }

      // Intent flag, set on SystemTask before NimbleController posts radio
      // reconciliation to the NimBLE host queue. RAM only.
      void SetActive(bool value) {
        active = value;
      }

      // BLE task, RAM only.
      bool StageKey(const uint8_t key[KeySize]);
      // SystemTask: submit the staged family-state candidate.
      void CommitStagedKey();
      void OnPersisted(uint32_t token, bool success);

      // Pure builders, no BLE, host-testable. See doc/BeaconService.md.
      // BuildAddress produces the 6-byte value for ble_hs_id_set_rnd in NimBLE
      // host byte order (little-endian: out[0] is the LSB).
      void BuildAddress(uint8_t out[6]) const;
      // BuildPayload produces the raw 31-byte advertising data for
      // ble_gap_adv_set_data.
      void BuildPayload(uint8_t out[31]) const;

    private:
      static uint32_t MutationToken(const uint8_t key[KeySize]);
      const FamilyState& Active() const;

      bool active = false; // RAM-only intent; OFF at boot
      uint8_t stagedKey[KeySize] {};
      bool stagedValid = false;
      uint32_t pendingToken = 0;
      System::StorageTask& storageTask;
    };
  }
}
