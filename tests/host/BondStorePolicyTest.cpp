#include "components/ble/BondStorePolicy.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

using Pinetime::Controllers::BondRegistry;
using Pinetime::Controllers::BondStorePolicy;

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  BondRegistry::PeerIdentity Peer(uint8_t value) {
    return {1, {value, static_cast<uint8_t>(value + 1), 2, 3, 4, 5}};
  }

  using CccdKey = BondStorePolicy::CccdKey;

  // Portable stand-in for the store/config backend: the C store needs the whole
  // NimBLE host to link, which is not available natively, so the fake keeps the
  // one property the policy depends on -- fixed capacity with a "full" reply --
  // and nothing else. Identities substitute for the security/CCCD records.
  class FakeBackend {
  public:
    static constexpr size_t MaxBonds = BondRegistry::Capacity;
    static constexpr size_t MaxCccds = 40;

    enum class Result { Ok, Full };

    Result WriteSecurity(std::vector<BondRegistry::PeerIdentity>& set, const BondRegistry::PeerIdentity& peer) {
      for (const auto& existing : set) {
        if (existing == peer) {
          return Result::Ok; // overwrite in place
        }
      }
      if (set.size() >= MaxBonds) {
        return Result::Full;
      }
      set.push_back(peer);
      return Result::Ok;
    }

    Result WriteOurSecurity(const BondRegistry::PeerIdentity& peer) {
      return WriteSecurity(ourSec, peer);
    }

    Result WritePeerSecurity(const BondRegistry::PeerIdentity& peer) {
      return WriteSecurity(peerSec, peer);
    }

    Result WriteCccd(const BondRegistry::PeerIdentity& peer, uint16_t handle) {
      for (const auto& existing : cccds) {
        if (existing.peer == peer && existing.handle == handle) {
          return Result::Ok;
        }
      }
      if (cccds.size() >= MaxCccds) {
        return Result::Full;
      }
      cccds.push_back({peer, handle});
      return Result::Ok;
    }

    // Mirrors ble_gap_unpair / ble_store_util_delete_peer: one call removes both
    // security halves, every CCCD, and (in firmware) the resolving-list entry.
    void DeletePeer(const BondRegistry::PeerIdentity& peer) {
      Erase(ourSec, peer);
      Erase(peerSec, peer);
      std::vector<CccdKey> kept;
      for (const auto& entry : cccds) {
        if (!(entry.peer == peer)) {
          kept.push_back(entry);
        }
      }
      cccds = kept;
    }

    size_t OurSecCount() const {
      return ourSec.size();
    }
    size_t PeerSecCount() const {
      return peerSec.size();
    }
    size_t CccdCount() const {
      return cccds.size();
    }

    bool HasBond(const BondRegistry::PeerIdentity& peer) const {
      for (const auto& existing : ourSec) {
        if (existing == peer) {
          return true;
        }
      }
      return false;
    }

  private:
    static void Erase(std::vector<BondRegistry::PeerIdentity>& set, const BondRegistry::PeerIdentity& peer) {
      std::vector<BondRegistry::PeerIdentity> kept;
      for (const auto& existing : set) {
        if (!(existing == peer)) {
          kept.push_back(existing);
        }
      }
      set = kept;
    }

    std::vector<BondRegistry::PeerIdentity> ourSec;
    std::vector<BondRegistry::PeerIdentity> peerSec;
    std::vector<CccdKey> cccds;
  };

  // Reproduces ble_store_write's retry loop: on a full backend it asks the
  // policy what to sacrifice, applies the unpair cascade, then retries once.
  bool WriteSecurityThroughPolicy(FakeBackend& backend,
                                  BondStorePolicy& policy,
                                  BondStorePolicy::ObjectType type,
                                  const BondRegistry::PeerIdentity& peer) {
    while (true) {
      const auto result = type == BondStorePolicy::ObjectType::OurSecurity ? backend.WriteOurSecurity(peer)
                                                                           : backend.WritePeerSecurity(peer);
      if (result == FakeBackend::Result::Ok) {
        policy.OnRecordWritten(type);
        return true;
      }
      const auto plan = policy.PlanOverflow(type, peer);
      if (plan.action != BondStorePolicy::OverflowAction::Evict) {
        return false;
      }
      backend.DeletePeer(plan.evict);
      if (!policy.CommitEviction(plan.evict)) {
        return false;
      }
    }
  }

  bool WriteCccdThroughPolicy(FakeBackend& backend, BondStorePolicy& policy, const BondRegistry::PeerIdentity& peer, uint16_t handle) {
    const auto result = backend.WriteCccd(peer, handle);
    if (result == FakeBackend::Result::Ok) {
      policy.OnRecordWritten(BondStorePolicy::ObjectType::Cccd);
      return true;
    }
    const auto plan = policy.PlanOverflow(BondStorePolicy::ObjectType::Cccd, peer);
    return plan.action == BondStorePolicy::OverflowAction::Evict;
  }

  // Full pairing of a brand-new phone: both security halves are written, then
  // the encryption event admits it to the registry.
  void BondNewPeer(FakeBackend& backend, BondStorePolicy& policy, const BondRegistry::PeerIdentity& peer) {
    WriteSecurityThroughPolicy(backend, policy, BondStorePolicy::ObjectType::OurSecurity, peer);
    WriteSecurityThroughPolicy(backend, policy, BondStorePolicy::ObjectType::PeerSecurity, peer);
    policy.OnBondEstablished(peer);
  }

  void AcknowledgeAll(BondStorePolicy& policy) {
    policy.AcknowledgePersisted(policy.Generation());
  }

  bool ValidateAligned(const BondRegistry::Snapshot& registry,
                       const std::vector<BondRegistry::PeerIdentity>& our,
                       const std::vector<BondRegistry::PeerIdentity>& peer,
                       const std::vector<CccdKey>& cccds) {
    return BondStorePolicy::ValidateStoreAlignment(registry, our.data(), our.size(), peer.data(), peer.size(), cccds.data(), cccds.size());
  }
}

int main() {
  static_assert(std::is_same_v<decltype(BondStorePolicy {}.Generation()), uint64_t>,
                "generation is a 64-bit, non-wrapping counter");

  // A security write marks critical dirty and advances the generation.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    Check(!policy.Dirty().critical, "policy starts clean");
    const uint64_t before = policy.Generation();
    WriteSecurityThroughPolicy(backend, policy, BondStorePolicy::ObjectType::OurSecurity, Peer(1));
    Check(policy.Dirty().critical, "security write marks critical dirty");
    Check(policy.Generation() > before, "security write advances generation");
    Check(!policy.Dirty().usage, "security write is not a usage-only change");
  }

  // A CCCD write marks critical dirty.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    WriteCccdThroughPolicy(backend, policy, Peer(1), 0x0010);
    Check(policy.Dirty().critical, "cccd write marks critical dirty");
  }

  // Redundant known reconnections keep the already-MRU peer retained without
  // changing its persisted state.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    AcknowledgeAll(policy);
    const auto before = policy.CaptureRegistry();
    const uint64_t generation = policy.Generation();
    Check(policy.OnKnownConnection(Peer(1)), "sole retained identity is known on reconnection");
    Check(policy.OnKnownConnection(Peer(1)), "repeated sole retained identity is still known");
    Check(policy.CaptureRegistry() == before, "repeated sole-peer touch leaves the snapshot unchanged");
    Check(policy.Generation() == generation, "repeated sole-peer touch leaves generation unchanged");
    Check(!policy.Dirty().usage && !policy.Dirty().critical, "repeated sole-peer touch leaves dirty state clean");
    Check(!policy.OnKnownConnection(Peer(9)), "unknown identity is not retained");
  }

  // A current-MRU touch is a no-op, while an older peer produces exactly one
  // usage mutation and moves to most recent.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    BondNewPeer(backend, policy, Peer(2));
    AcknowledgeAll(policy);
    const auto beforeMruTouch = policy.CaptureRegistry();
    const uint64_t beforeMruGeneration = policy.Generation();
    Check(policy.OnKnownConnection(Peer(2)), "current-MRU retained identity is known");
    Check(policy.CaptureRegistry() == beforeMruTouch, "current-MRU touch leaves the snapshot unchanged");
    Check(policy.Generation() == beforeMruGeneration, "current-MRU touch leaves generation unchanged");
    Check(!policy.Dirty().usage, "current-MRU touch leaves usage clean");

    Check(policy.OnKnownConnection(Peer(1)), "older retained identity is known");
    Check(policy.Generation() == beforeMruGeneration + 1, "older-peer touch records one usage generation");
    Check(policy.Dirty().usage && !policy.Dirty().critical, "older-peer touch marks only usage dirty");
    Check(policy.Registry().At(0).lastUsed > policy.Registry().At(1).lastUsed,
          "older-peer touch updates relative LRU order");
  }

  // Acknowledging a generation clears exactly the changes it covers.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    const uint64_t captured = policy.Generation();
    AcknowledgeAll(policy);
    Check(!policy.Dirty().critical && !policy.Dirty().usage, "acknowledging the current generation clears dirty");

    // A critical write races the persist: the writer only saw `captured`.
    WriteSecurityThroughPolicy(backend, policy, BondStorePolicy::ObjectType::OurSecurity, Peer(2));
    policy.AcknowledgePersisted(captured);
    Check(policy.Dirty().critical, "a write after capture stays dirty when an older generation is acknowledged");
    policy.AcknowledgePersisted(policy.Generation());
    Check(!policy.Dirty().critical, "acknowledging the newer generation clears it");
  }

  // A usage touch racing a persist is likewise preserved.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    BondNewPeer(backend, policy, Peer(2));
    const uint64_t captured = policy.Generation();
    policy.OnKnownConnection(Peer(1)); // usage change after the snapshot
    policy.AcknowledgePersisted(captured);
    Check(policy.Dirty().usage, "a usage touch after capture stays dirty");
    Check(!policy.Dirty().critical, "the acknowledged critical state is cleared independently");
  }

  // Generation never wraps and acknowledgement ignores stale values.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    const uint64_t g1 = policy.Generation();
    AcknowledgeAll(policy);
    Check(!policy.Dirty().critical, "acknowledged snapshot is clean");

    WriteSecurityThroughPolicy(backend, policy, BondStorePolicy::ObjectType::OurSecurity, Peer(2));
    const uint64_t g2 = policy.Generation();
    Check(g2 > g1, "each change advances the 64-bit generation");
    policy.AcknowledgePersisted(g1); // a generation older than the latest change
    Check(policy.Dirty().critical, "a stale acknowledgement does not clear a newer change");
    policy.AcknowledgePersisted(g2);
    Check(!policy.Dirty().critical, "acknowledging the change's generation clears it");

    // Generation grows monotonically over many mutations without wrapping.
    BondNewPeer(backend, policy, Peer(2));
    AcknowledgeAll(policy);
    uint64_t previous = policy.Generation();
    bool monotonic = true;
    for (int i = 0; i < 2000; i++) {
      policy.OnKnownConnection(i % 2 == 0 ? Peer(1) : Peer(2));
      if (policy.Generation() <= previous) {
        monotonic = false;
      }
      previous = policy.Generation();
    }
    Check(monotonic, "generation increases monotonically across many changes");
  }

  // Returning-bond handling follows the same order-change rule.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    BondNewPeer(backend, policy, Peer(2));
    AcknowledgeAll(policy);
    const uint64_t currentMruGeneration = policy.Generation();
    policy.OnBondEstablished(Peer(2));
    Check(policy.Generation() == currentMruGeneration, "existing current-MRU bond establishment is not a usage change");
    Check(!policy.Dirty().usage, "existing current-MRU bond establishment leaves usage clean");
    policy.OnBondEstablished(Peer(1));
    Check(policy.Generation() == currentMruGeneration + 1, "existing older bond establishment marks one usage change");
    Check(policy.Dirty().usage && !policy.Dirty().critical, "existing older bond establishment marks only usage dirty");
  }

  // A sixth phone evicts the least-recently-used bond, cascading through unpair.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    Check(backend.OurSecCount() == BondRegistry::Capacity, "backend holds five bonds");
    Check(policy.Registry().Count() == BondRegistry::Capacity, "registry holds five peers");

    policy.OnKnownConnection(Peer(1));
    AcknowledgeAll(policy);

    const uint32_t priorEvictions = policy.EvictionCount();
    BondNewPeer(backend, policy, Peer(6));
    Check(policy.EvictionCount() == priorEvictions + 1, "exactly one eviction for the sixth phone");
    Check(!backend.HasBond(Peer(2)), "least-recently-used bond removed from backend");
    Check(backend.HasBond(Peer(1)), "recently-used bond survives");
    Check(backend.HasBond(Peer(6)), "new bond written to backend");
    Check(!policy.Registry().Contains(Peer(2)), "registry drops the evicted peer");
    Check(policy.Registry().Contains(Peer(6)), "registry admits the new peer");
    Check(policy.Dirty().critical, "eviction and admission mark critical dirty");
  }

  // The eviction count is the registry's own, so a snapshot carries it.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    BondNewPeer(backend, policy, Peer(6));
    Check(policy.EvictionCount() == 1, "policy eviction count reflects one eviction");
    Check(policy.CaptureRegistry().evictionCount == 1, "registry snapshot carries the eviction count");

    BondStorePolicy restored;
    Check(restored.RestoreRegistry(policy.CaptureRegistry()), "eviction-bearing snapshot restores");
    Check(restored.EvictionCount() == 1, "restored eviction count matches the snapshot");
  }

  // OUR_SEC and PEER_SEC overflow separately but only one bond is evicted.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    const uint32_t priorEvictions = policy.EvictionCount();
    Check(WriteSecurityThroughPolicy(backend, policy, BondStorePolicy::ObjectType::OurSecurity, Peer(6)),
          "sixth our-sec fits after one eviction");
    Check(WriteSecurityThroughPolicy(backend, policy, BondStorePolicy::ObjectType::PeerSecurity, Peer(6)),
          "sixth peer-sec fits without a second eviction");
    Check(policy.EvictionCount() == priorEvictions + 1, "separate security halves cause a single eviction");
  }

  // Successful repeat pairing: forget then re-admit keeps counts aligned.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    const uint32_t priorEvictions = policy.EvictionCount();

    // Repeat pairing: delete this peer's bond and forget it from the registry.
    backend.DeletePeer(Peer(3));
    Check(policy.OnPeerForgotten(Peer(3)), "forgetting a retained peer removes it");
    Check(policy.EvictionCount() == priorEvictions, "forgetting a peer is not an eviction");
    Check(policy.Registry().Count() == BondRegistry::Capacity - 1, "registry drops the forgotten peer");
    Check(!backend.HasBond(Peer(3)) && !policy.Registry().Contains(Peer(3)),
          "store and registry agree the peer is gone");

    // The replacement pairing completes.
    BondNewPeer(backend, policy, Peer(3));
    Check(policy.Registry().Count() == BondRegistry::Capacity, "successful repeat pairing restores five peers");
    Check(policy.EvictionCount() == priorEvictions, "successful repeat pairing evicts nothing");
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      Check(backend.HasBond(Peer(i)), "every phone bonded after a successful repeat pairing");
    }
  }

  // Failed repeat pairing: forgetting still leaves store and registry aligned.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    backend.DeletePeer(Peer(3));
    Check(policy.OnPeerForgotten(Peer(3)), "forgetting a retained peer removes it");
    // The replacement never completes (peer walks away): no OnBondEstablished.
    Check(policy.Registry().Count() == BondRegistry::Capacity - 1, "a failed repeat pairing leaves four peers");
    Check(!backend.HasBond(Peer(3)) && !policy.Registry().Contains(Peer(3)),
          "a failed repeat pairing keeps store and registry aligned around the absent peer");
    Check(!policy.OnPeerForgotten(Peer(3)), "forgetting an absent peer is a no-op");
  }

  // Regression: admission must follow the store-side eviction, never precede
  // it. This is why registry reconciliation moved out of the pre-persist
  // ENC_CHANGE event (which fires before NimBLE writes keys) and into the
  // post-write store path.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    // Admitting a sixth peer while the registry is still full -- the mistake the
    // old ENC_CHANGE ordering made -- is refused as an invariant violation and
    // changes nothing.
    const uint32_t priorViolations = policy.InvariantViolations();
    policy.OnBondEstablished(Peer(6));
    Check(policy.InvariantViolations() == priorViolations + 1,
          "admitting into a full registry before eviction is an invariant violation");
    Check(!policy.Registry().Contains(Peer(6)), "the premature admission does not touch the registry");
    Check(policy.Registry().Count() == BondRegistry::Capacity, "the registry still holds exactly five peers");

    // The correct order -- evict through the store overflow path, then admit
    // once both halves are written -- aligns the store and registry at five.
    BondNewPeer(backend, policy, Peer(6));
    Check(policy.Registry().Contains(Peer(6)) && policy.Registry().Count() == BondRegistry::Capacity,
          "eviction-then-admission aligns the registry at five with the newcomer present");
  }

  // A CCCD overflow rejects the record without sacrificing a bond.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    for (uint8_t peer = 1; peer <= BondRegistry::Capacity; peer++) {
      for (uint16_t handle = 0; handle < 8; handle++) {
        Check(WriteCccdThroughPolicy(backend, policy, Peer(peer), static_cast<uint16_t>(0x100 + handle)),
              "cccd within derived capacity is stored");
      }
    }
    Check(backend.CccdCount() == FakeBackend::MaxCccds, "cccd store reaches derived capacity");

    const uint32_t priorEvictions = policy.EvictionCount();
    Check(!WriteCccdThroughPolicy(backend, policy, Peer(1), 0x0999), "cccd overflow is rejected");
    Check(policy.CccdOverflowRejections() == 1, "cccd overflow increments the diagnostic");
    Check(policy.EvictionCount() == priorEvictions, "cccd overflow evicts no bond");
  }

  // A failed unpair must not commit a registry mutation implying success.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    const auto plan = policy.PlanOverflow(BondStorePolicy::ObjectType::OurSecurity, Peer(6));
    Check(plan.action == BondStorePolicy::OverflowAction::Evict, "overflow plans an eviction");
    Check(policy.Registry().Contains(plan.evict), "planned victim remains until the store side succeeds");
    Check(policy.Registry().Count() == BondRegistry::Capacity, "a failed unpair leaves the registry unchanged");
  }

  // CommitEviction against a victim the registry never held is an invariant fault.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    const uint32_t priorEvictions = policy.EvictionCount();
    Check(!policy.CommitEviction(Peer(9)), "committing an eviction for an unknown victim fails");
    Check(policy.InvariantViolations() == 1, "the disagreement is counted as an invariant violation");
    Check(policy.EvictionCount() == priorEvictions, "no eviction is recorded for the phantom victim");
    Check(policy.Registry().Count() == BondRegistry::Capacity, "the registry is left untouched");
  }

  // OnBondEstablished never evicts at the registry level for a full store.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      BondNewPeer(backend, policy, Peer(i));
    }
    const auto before = policy.CaptureRegistry();
    policy.OnBondEstablished(Peer(6));
    Check(policy.InvariantViolations() == 1, "a full registry admitting a new peer is an invariant violation");
    Check(!policy.Registry().Contains(Peer(6)), "the unexpected peer is not admitted");
    Check(policy.Registry().Count() == BondRegistry::Capacity, "no existing peer is evicted at the registry level");
    Check(policy.CaptureRegistry().entries == before.entries, "the registry contents are unchanged");
  }

  // ValidateStoreAlignment accepts a well-formed snapshot and rejects corruption.
  {
    BondRegistry reference;
    reference.Admit(Peer(1));
    reference.Admit(Peer(2));
    reference.Admit(Peer(3));
    const auto snapshot = reference.Capture();

    const std::vector<BondRegistry::PeerIdentity> our {Peer(1), Peer(2), Peer(3)};
    const std::vector<BondRegistry::PeerIdentity> peer {Peer(3), Peer(1), Peer(2)}; // order need not match
    const std::vector<CccdKey> cccds {{Peer(1), 0x10}, {Peer(1), 0x20}, {Peer(2), 0x10}};
    Check(ValidateAligned(snapshot, our, peer, cccds), "a one-to-one snapshot validates");

    // Mid-pair: a security record exists with no registry entry yet.
    {
      BondRegistry empty;
      const auto emptySnapshot = empty.Capture();
      Check(!ValidateAligned(emptySnapshot, {Peer(1)}, {Peer(1)}, {}),
            "a security record with no registry peer is refused (mid-pair)");
    }

    // Missing peer: OUR_SEC repeats Peer(1) and never covers Peer(3).
    Check(!ValidateAligned(snapshot, {Peer(1), Peer(1), Peer(2)}, peer, {}),
          "a duplicate/missing OUR_SEC identity is refused");

    // Count mismatch: fewer security records than registry peers.
    Check(!ValidateAligned(snapshot, {Peer(1), Peer(2)}, {Peer(1), Peer(2)}, {}),
          "too few security records is refused");

    // PEER_SEC references an identity the registry does not hold.
    Check(!ValidateAligned(snapshot, our, {Peer(1), Peer(2), Peer(9)}, {}),
          "a PEER_SEC identity outside the registry is refused");

    // Duplicate CCCD by (peer, handle).
    Check(!ValidateAligned(snapshot, our, peer, {{Peer(1), 0x10}, {Peer(1), 0x10}}),
          "a duplicate CCCD is refused");

    // CCCD owned by an unregistered peer.
    Check(!ValidateAligned(snapshot, our, peer, {{Peer(9), 0x10}}),
          "a CCCD for an unregistered peer is refused");

    // Invalid address inside an otherwise correct-count OUR_SEC set.
    {
      BondRegistry single;
      single.Admit(Peer(1));
      const auto singleSnapshot = single.Capture();
      const BondRegistry::PeerIdentity zeroAddress {1, {}};
      Check(!ValidateAligned(singleSnapshot, {zeroAddress}, {Peer(1)}, {}),
            "an all-zero identity is refused");
    }
  }

  // Registry capture and restore are clean seams for the persistence writer.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    BondNewPeer(backend, policy, Peer(2));
    const auto snapshot = policy.CaptureRegistry();

    BondStorePolicy restored;
    Check(restored.RestoreRegistry(snapshot), "snapshot restores into a fresh policy");
    Check(!restored.Dirty().critical && !restored.Dirty().usage, "a restored snapshot is clean");
    Check(restored.Registry().Contains(Peer(1)) && restored.Registry().Contains(Peer(2)),
          "restored registry holds the snapshot peers");
  }

  // Forget-all is a single critical mutation and carries its reset epoch.
  {
    FakeBackend backend;
    BondStorePolicy policy;
    BondNewPeer(backend, policy, Peer(1));
    AcknowledgeAll(policy);
    const uint32_t priorEpoch = policy.Registry().ResetEpoch();
    policy.OnForgetAll();
    Check(policy.Registry().Empty(), "forget-all clears the registry");
    Check(policy.Registry().ResetEpoch() == priorEpoch + 1, "forget-all advances the reset epoch");
    Check(policy.Dirty().critical, "forget-all is critical dirty");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
