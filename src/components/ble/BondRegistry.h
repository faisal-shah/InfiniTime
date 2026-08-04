#pragma once

#include "components/ble/generated/CompanionProtocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Pinetime::Controllers {
  class BondRegistry {
  public:
    static constexpr size_t Capacity = CompanionProtocol::RetainedPeers;

    struct PeerIdentity {
      uint8_t type = 0;
      std::array<uint8_t, 6> address {};

      bool operator==(const PeerIdentity&) const = default;
    };

    struct Entry {
      PeerIdentity peer;
      uint32_t lastUsed = 0;

      bool operator==(const Entry&) const = default;
    };

    struct Snapshot {
      std::array<Entry, Capacity> entries {};
      uint8_t count = 0;
      uint32_t nextUseSequence = 1;
      uint32_t resetEpoch = 0;
      uint32_t evictionCount = 0;

      bool operator==(const Snapshot&) const = default;
    };

    enum class AdmissionKind : uint8_t {
      Rejected,
      Existing,
      Added,
      Evicted,
    };

    struct Admission {
      AdmissionKind kind;
      std::optional<PeerIdentity> evicted;
    };

    enum class TouchResult : uint8_t {
      Unknown,
      AlreadyMostRecent,
      OrderChanged,
    };

    size_t Count() const {
      return count;
    }

    bool Empty() const {
      return count == 0;
    }

    uint32_t ResetEpoch() const {
      return resetEpoch;
    }

    uint32_t EvictionCount() const {
      return evictionCount;
    }

    bool Contains(const PeerIdentity& peer) const;
    TouchResult Touch(const PeerIdentity& peer);
    std::optional<PeerIdentity> EvictionCandidate(const PeerIdentity& incoming) const;
    Admission Admit(const PeerIdentity& peer);
    bool Remove(const PeerIdentity& peer);
    // Remove a peer as a least-recently-used eviction, so the removal is
    // reflected in the eviction count that Capture/Restore carry. Used when the
    // store side (ble_gap_unpair) drove the eviction instead of Admit.
    bool RemoveForEviction(const PeerIdentity& peer);
    void ForgetAll();

    const Entry* Find(const PeerIdentity& peer) const;
    // Internal indexed access; caller must provide index < Count().
    const Entry& At(size_t index) const;

    Snapshot Capture() const;
    bool Restore(const Snapshot& snapshot);

    // A peer identity is storable only if its address type is one of the four
    // defined values and its address is not all zero (BLE_ADDR_ANY). Public so
    // snapshot validation can reject malformed store records.
    static bool IsValidPeer(const PeerIdentity& peer);

  private:
    std::optional<size_t> FindIndex(const PeerIdentity& peer) const;
    size_t LeastRecentlyUsedIndex() const;
    uint32_t NextUseSequence();
    void NormalizeUseSequences();

    std::array<Entry, Capacity> entries {};
    size_t count = 0;
    uint32_t nextUseSequence = 1;
    uint32_t resetEpoch = 0;
    uint32_t evictionCount = 0;
  };
}
