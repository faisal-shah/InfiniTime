#pragma once

#include "components/ble/BondRegistry.h"
#include "components/ble/generated/CompanionProtocol.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Pinetime::Controllers {
  struct BondSecurityRecord {
    BondRegistry::PeerIdentity peer {};
    uint8_t keySize = 0;
    uint16_t ediv = 0;
    uint64_t rand = 0;
    std::array<uint8_t, 16> ltk {};
    std::array<uint8_t, 16> irk {};
    std::array<uint8_t, 16> csrk {};
    bool ltkPresent = false;
    bool irkPresent = false;
    bool csrkPresent = false;
    bool authenticated = false;
    bool secureConnections = false;

    bool operator==(const BondSecurityRecord&) const = default;
  };

  struct BondCccdRecord {
    BondRegistry::PeerIdentity peer {};
    uint16_t handle = 0;
    uint16_t flags = 0;
    bool valueChanged = false;

    bool operator==(const BondCccdRecord&) const = default;
  };

  // Portable value model of the NimBLE store. No NimBLE C structure, padding,
  // or bitfield crosses the persistence boundary.
  struct NimbleBondStoreSnapshot {
    uint8_t ourSecCount = 0;
    uint8_t peerSecCount = 0;
    uint8_t cccdCount = 0;
    std::array<BondSecurityRecord, CompanionProtocol::RetainedPeers> ourSecs {};
    std::array<BondSecurityRecord, CompanionProtocol::RetainedPeers> peerSecs {};
    std::array<BondCccdRecord, CompanionProtocol::MaxCccds> cccds {};
    BondRegistry::Snapshot registry {};
    uint64_t generation = 0;

    void Clear() {
      static_assert(std::is_trivially_copyable_v<NimbleBondStoreSnapshot>);
      std::memset(static_cast<void*>(this), 0, sizeof(*this));
      registry.nextUseSequence = 1;
    }

    bool operator==(const NimbleBondStoreSnapshot&) const = default;
  };
}
