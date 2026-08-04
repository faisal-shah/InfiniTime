#include "components/ble/BondRegistry.h"

#include <cstdint>
#include <cstdio>
#include <limits>

using Pinetime::Controllers::BondRegistry;

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

  BondRegistry::PeerIdentity Peer(uint8_t value, uint8_t type = 1) {
    return {type, {value, static_cast<uint8_t>(value + 1), 2, 3, 4, 5}};
  }
}

int main() {
  static_assert(BondRegistry::Capacity == 5);

  {
    BondRegistry registry;
    Check(registry.Admit(Peer(1, 9)).kind == BondRegistry::AdmissionKind::Rejected, "invalid address type rejected");
    Check(registry.Admit({1, {}}).kind == BondRegistry::AdmissionKind::Rejected, "all-zero address rejected");
    Check(registry.Empty(), "invalid peers never enter the registry");
  }

  {
    BondRegistry registry;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      const auto result = registry.Admit(Peer(i));
      Check(result.kind == BondRegistry::AdmissionKind::Added, "initial peers are added");
      Check(!result.evicted.has_value(), "initial admission does not evict");
    }
    Check(registry.Count() == BondRegistry::Capacity, "registry reaches five peers");

    Check(registry.EvictionCandidate(Peer(9)) == Peer(1), "adapter can inspect eviction candidate before mutation");
    const auto sixth = registry.Admit(Peer(9));
    Check(sixth.kind == BondRegistry::AdmissionKind::Evicted, "sixth peer evicts");
    Check(sixth.evicted == Peer(1), "oldest peer is evicted");
    Check(!registry.Contains(Peer(1)) && registry.Contains(Peer(9)), "eviction replaces only the oldest peer");
    Check(registry.EvictionCount() == 1, "eviction counter increments");
  }

  {
    BondRegistry registry;
    for (uint8_t i = 1; i <= BondRegistry::Capacity; i++) {
      registry.Admit(Peer(i));
    }
    Check(registry.Touch(Peer(1)) == BondRegistry::TouchResult::OrderChanged,
          "known battery-only connection moves an older peer to most recent");
    Check(registry.EvictionCandidate(Peer(9)) == Peer(2), "eviction plan reflects the latest touch");
    const auto sixth = registry.Admit(Peer(9));
    Check(sixth.evicted == Peer(2), "recently used oldest peer is preserved");

    const auto existing = registry.Admit(Peer(3));
    Check(existing.kind == BondRegistry::AdmissionKind::Existing, "repeat admission reuses same peer");
    Check(!existing.evicted.has_value(), "repeat pairing does not evict another peer");
    Check(!registry.EvictionCandidate(Peer(3)).has_value(), "existing peer never plans collateral eviction");
  }

  {
    BondRegistry registry;
    registry.Admit(Peer(1));
    const auto singleBefore = registry.Capture();
    Check(registry.Touch(Peer(1)) == BondRegistry::TouchResult::AlreadyMostRecent,
          "the sole peer is already most recent");
    Check(registry.Capture() == singleBefore, "touching the sole peer leaves the snapshot unchanged");
    Check(registry.Touch(Peer(9)) == BondRegistry::TouchResult::Unknown, "unknown peer touch is distinguished");

    registry.Admit(Peer(2));
    const auto mruBefore = registry.Capture();
    Check(registry.Touch(Peer(2)) == BondRegistry::TouchResult::AlreadyMostRecent,
          "current most-recent peer touch is distinguished");
    Check(registry.Capture() == mruBefore, "touching the current most-recent peer leaves the snapshot unchanged");
    Check(registry.Touch(Peer(1)) == BondRegistry::TouchResult::OrderChanged,
          "older peer touch changes relative order");
    Check(registry.At(0).lastUsed > registry.At(1).lastUsed, "order-changing touch makes the older peer most recent");
  }

  {
    BondRegistry registry;
    registry.Admit(Peer(1));
    registry.Admit(Peer(2));
    registry.Admit(Peer(3));
    Check(registry.Remove(Peer(2)), "remove finds middle peer");
    Check(registry.Count() == 2, "remove decrements count");
    Check(registry.Contains(Peer(1)) && registry.Contains(Peer(3)), "remove preserves remaining peers");
    Check(!registry.Remove(Peer(2)), "remove missing peer is a no-op");
  }

  {
    BondRegistry registry;
    registry.Admit(Peer(1));
    registry.Admit(Peer(2));
    registry.Touch(Peer(1));
    auto snapshot = registry.Capture();

    BondRegistry restored;
    Check(restored.Restore(snapshot), "valid snapshot restores");
    Check(restored.Capture().entries == snapshot.entries, "restored entries are byte-equivalent");
    Check(restored.Capture().nextUseSequence == snapshot.nextUseSequence, "use sequence restores");

    auto duplicate = snapshot;
    duplicate.entries[1].peer = duplicate.entries[0].peer;
    Check(!restored.Restore(duplicate), "duplicate peer snapshot rejected");

    auto duplicateUse = snapshot;
    duplicateUse.entries[1].lastUsed = duplicateUse.entries[0].lastUsed;
    Check(!restored.Restore(duplicateUse), "duplicate LRU sequence rejected");

    auto badCount = snapshot;
    badCount.count = BondRegistry::Capacity + 1;
    Check(!restored.Restore(badCount), "over-capacity snapshot rejected");

    const auto beforeInvalidRestore = restored.Capture();
    auto badSequence = snapshot;
    badSequence.nextUseSequence = badSequence.entries[1].lastUsed;
    Check(!restored.Restore(badSequence), "non-monotonic next sequence rejected");
    const auto afterInvalidRestore = restored.Capture();
    Check(afterInvalidRestore.entries == beforeInvalidRestore.entries &&
            afterInvalidRestore.count == beforeInvalidRestore.count &&
            afterInvalidRestore.nextUseSequence == beforeInvalidRestore.nextUseSequence,
          "invalid restore leaves registry unchanged");

    auto dirtyTail = snapshot;
    dirtyTail.entries[BondRegistry::Capacity - 1] = {Peer(9), 99};
    BondRegistry cleanTail;
    Check(cleanTail.Restore(dirtyTail), "unused snapshot tail is ignored");
    Check(cleanTail.Capture().entries[BondRegistry::Capacity - 1] == BondRegistry::Entry {}, "unused restored tail is zeroed");
  }

  {
    BondRegistry registry;
    auto snapshot = BondRegistry::Snapshot {};
    snapshot.count = 2;
    snapshot.entries[0] = {Peer(1), 10};
    snapshot.entries[1] = {Peer(2), 20};
    snapshot.nextUseSequence = std::numeric_limits<uint32_t>::max();
    Check(registry.Restore(snapshot), "near-rollover snapshot restores");
    Check(registry.Touch(Peer(1)) == BondRegistry::TouchResult::OrderChanged,
          "touch normalizes sequence rollover");
    const auto normalized = registry.Capture();
    Check(normalized.nextUseSequence == 4, "normalization compacts use sequence");
    Check(normalized.entries[0].lastUsed == 3, "touched peer becomes newest after normalization");
    Check(normalized.entries[1].lastUsed == 2, "other peer retains relative order");
  }

  {
    BondRegistry registry;
    registry.Admit(Peer(1));
    const uint32_t priorEpoch = registry.ResetEpoch();
    registry.ForgetAll();
    Check(registry.Empty(), "forget all clears peers");
    Check(registry.ResetEpoch() == priorEpoch + 1, "forget all increments reset epoch");
    Check(registry.EvictionCount() == 0, "forget all does not report an LRU eviction");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
