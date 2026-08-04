#pragma once

#include "components/ble/BondRegistry.h"

#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {
  // Portable half of the bond store adapter. It owns the fixed-capacity
  // BondRegistry and the dirty bookkeeping the asynchronous persistence writer
  // consumes. It has no NimBLE dependency: NimbleBondStoreAdapter translates
  // GAP and store-callback events into these calls, and the host tests drive it
  // through a fake backend.
  //
  // Threading: the mutating calls, Dirty(), Generation(), Capture, and
  // AcknowledgePersisted all run on the NimBLE host task, under whatever lock
  // the store callback already holds. The SystemTask persistence writer never
  // touches this object directly -- it captures a snapshot on the host task,
  // writes it, then posts the acknowledgement back to the host task. Nothing
  // here touches the filesystem, blocks, or computes a digest.
  class BondStorePolicy {
  public:
    enum class ObjectType : uint8_t {
      OurSecurity,
      PeerSecurity,
      Cccd,
    };

    enum class OverflowAction : uint8_t {
      // No room and no bond may be sacrificed: reject the write.
      Reject,
      // Evict the identified least-recently-used bond, then retry the write.
      Evict,
    };

    struct OverflowPlan {
      OverflowAction action = OverflowAction::Reject;
      BondRegistry::PeerIdentity evict {};
    };

    // A CCCD record identified by its owning peer and characteristic value
    // handle. Used to validate that a store snapshot has no duplicate
    // subscriptions.
    struct CccdKey {
      BondRegistry::PeerIdentity peer {};
      uint16_t handle = 0;

      bool operator==(const CccdKey&) const = default;
    };

    // What the writer must flush. `generation` is the sequence number of the
    // most recent change; the writer captures it, persists, then acknowledges
    // it so only changes it actually wrote are cleared. It is 64-bit so it never
    // wraps in practice. `critical` covers security keys, CCCDs, and
    // deletes/evictions; `usage` is LRU touch only.
    struct DirtyState {
      bool critical = false;
      bool usage = false;
      uint64_t generation = 0;
    };

    BondRegistry& Registry() {
      return registry;
    }

    const BondRegistry& Registry() const {
      return registry;
    }

    // A security or CCCD record was written to the backend successfully.
    void OnRecordWritten(ObjectType type);
    // A security or CCCD record was deleted from the backend successfully.
    void OnRecordDeleted(ObjectType type);

    // The backend reported it is full while writing a record for `incoming`.
    // Deciding what to sacrifice never mutates the registry; the adapter must
    // call CommitEviction only after the corresponding unpair actually
    // succeeds, so a failed store operation never implies a registry change.
    OverflowPlan PlanOverflow(ObjectType type, const BondRegistry::PeerIdentity& incoming);

    // Confirm the eviction the adapter planned, after the peer's bond has been
    // torn down in the backend. Returns false when the victim was not in the
    // registry: that means the store and registry disagreed, so the caller must
    // treat it as an invariant failure rather than a completed eviction.
    bool CommitEviction(const BondRegistry::PeerIdentity& evicted);

    // A bond was deleted from the backend for `peer` outside of an eviction --
    // e.g. a repeat pairing dropping its own keys. Removes the registry entry
    // without counting an LRU eviction, keeping the two halves aligned even if
    // the replacement pairing never completes. Returns false when the peer was
    // not retained.
    bool OnPeerForgotten(const BondRegistry::PeerIdentity& peer);

    // Clear the complete host store and registry as one critical logical
    // change. The adapter performs the backend deletion first.
    void OnForgetAll();

    // A connection to a known identity succeeded (including an unencrypted
    // battery-only reconnection). Refreshes LRU order only when it changes the
    // relative order. Returns false when the identity is not a retained peer.
    bool OnKnownConnection(const BondRegistry::PeerIdentity& peer);

    // A bond finished establishing for `peer`. A brand-new peer is admitted
    // (critical); a returning peer only refreshes LRU order (usage). The
    // store-side overflow path has already made room, so a full registry facing
    // an unknown peer here is an invariant violation: it is counted and left
    // untouched rather than evicting a second, unrelated bond.
    void OnBondEstablished(const BondRegistry::PeerIdentity& peer);

    DirtyState Dirty() const;

    // Clear the dirty flags for every change up to and including `generation`.
    // A change made after the writer captured its snapshot keeps its kind dirty
    // even when an older generation is acknowledged, so a write racing a persist
    // is never lost. Acknowledgement is monotonic: a stale (smaller) value is a
    // no-op.
    void AcknowledgePersisted(uint64_t generation);

    uint64_t Generation() const {
      return generation;
    }

    // Least-recently-used bonds evicted to make room for a new phone. This is
    // the registry's own counter -- the same one Capture/Restore carry -- so
    // the persisted eviction total matches what actually happened.
    uint32_t EvictionCount() const {
      return registry.EvictionCount();
    }

    // CCCD overflows rejected without sacrificing a bond. A non-zero value means
    // a phone subscribed to more characteristics than the derived capacity
    // allows, which is a protocol-manifest bug, not a runtime state.
    uint32_t CccdOverflowRejections() const {
      return cccdOverflowRejections;
    }

    // Store/registry disagreements the adapter refused to paper over: a planned
    // eviction whose victim was already gone, or a bond establishing against a
    // full registry the overflow path should have made room in. Always zero in
    // correct operation.
    uint32_t InvariantViolations() const {
      return invariantViolations;
    }

    BondRegistry::Snapshot CaptureRegistry() const {
      return registry.Capture();
    }

    // Restore seam for the persistence writer's reload path. Restores the
    // registry and resets the dirty baseline to clean at the new generation: a
    // freshly loaded snapshot has nothing outstanding to persist.
    bool RestoreRegistry(const BondRegistry::Snapshot& snapshot, uint64_t restoredGeneration = 0);

    // Validate that a store snapshot's security and CCCD records align exactly
    // one-to-one with a registry snapshot: exactly one OUR_SEC and one PEER_SEC
    // for every registered identity (no duplicate or missing peers), every CCCD
    // owned by a registered identity and unique by (peer, handle), and every
    // address a valid identity. Portable so both capture and restore, and the
    // host tests, share one definition of "aligned".
    static bool ValidateStoreAlignment(const BondRegistry::Snapshot& registry,
                                       const BondRegistry::PeerIdentity* ourSecs,
                                       size_t ourCount,
                                       const BondRegistry::PeerIdentity* peerSecs,
                                       size_t peerCount,
                                       const CccdKey* cccds,
                                       size_t cccdCount);

  private:
    static bool IsSecurity(ObjectType type) {
      return type == ObjectType::OurSecurity || type == ObjectType::PeerSecurity;
    }

    void MarkCritical();
    void MarkUsage();

    BondRegistry registry;
    uint64_t generation = 0;
    uint64_t criticalGeneration = 0;
    uint64_t usageGeneration = 0;
    uint64_t criticalAcknowledged = 0;
    uint64_t usageAcknowledged = 0;
    uint32_t cccdOverflowRejections = 0;
    uint32_t invariantViolations = 0;
  };
}
