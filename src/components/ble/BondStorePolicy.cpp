#include "components/ble/BondStorePolicy.h"

using Pinetime::Controllers::BondRegistry;
using Pinetime::Controllers::BondStorePolicy;

void BondStorePolicy::MarkCritical() {
  criticalGeneration = ++generation;
}

void BondStorePolicy::MarkUsage() {
  usageGeneration = ++generation;
}

void BondStorePolicy::OnRecordWritten(ObjectType) {
  // Both security halves and CCCDs are durable state the writer must flush.
  MarkCritical();
}

void BondStorePolicy::OnRecordDeleted(ObjectType) {
  MarkCritical();
}

BondStorePolicy::OverflowPlan BondStorePolicy::PlanOverflow(ObjectType type, const BondRegistry::PeerIdentity& incoming) {
  if (IsSecurity(type)) {
    // A security overflow means a new phone is pairing while the store is full.
    // Sacrifice the least-recently-used bond so the newcomer fits. If the
    // registry cannot name a candidate -- the incoming peer is already retained
    // or the registry is not actually full -- there is no safe eviction, so
    // reject rather than delete an unrelated bond.
    const auto candidate = registry.EvictionCandidate(incoming);
    if (candidate) {
      return {OverflowAction::Evict, *candidate};
    }
    return {OverflowAction::Reject, {}};
  }

  // A CCCD overflow is not supposed to happen: capacity is derived as
  // retained peers times persisted notify characteristics, so a full set of
  // bonds cannot exhaust it. Reject the record and count it; never tear down a
  // bond to make room for a subscription.
  cccdOverflowRejections++;
  return {OverflowAction::Reject, {}};
}

bool BondStorePolicy::CommitEviction(const BondRegistry::PeerIdentity& evicted) {
  if (!registry.RemoveForEviction(evicted)) {
    // The store side tore down a bond the registry never knew about. The caller
    // must not report success on top of this disagreement.
    invariantViolations++;
    return false;
  }
  MarkCritical();
  return true;
}

bool BondStorePolicy::OnPeerForgotten(const BondRegistry::PeerIdentity& peer) {
  // Not an LRU eviction: the peer's own bond was deleted, so drop it from the
  // registry too without touching the eviction count. If the caller re-pairs it
  // OnBondEstablished re-admits it; if the re-pair fails, both halves stay
  // aligned around an absent peer.
  if (!registry.Remove(peer)) {
    return false;
  }
  MarkCritical();
  return true;
}

void BondStorePolicy::OnForgetAll() {
  registry.ForgetAll();
  MarkCritical();
}

bool BondStorePolicy::OnKnownConnection(const BondRegistry::PeerIdentity& peer) {
  const auto touched = registry.Touch(peer);
  if (touched == BondRegistry::TouchResult::Unknown) {
    return false;
  }
  if (touched == BondRegistry::TouchResult::OrderChanged) {
    MarkUsage();
  }
  return true;
}

void BondStorePolicy::OnBondEstablished(const BondRegistry::PeerIdentity& peer) {
  if (registry.Contains(peer)) {
    // A returning phone: only its position in the LRU order changed.
    if (registry.Touch(peer) == BondRegistry::TouchResult::OrderChanged) {
      MarkUsage();
    }
    return;
  }
  if (registry.Count() < BondRegistry::Capacity) {
    registry.Admit(peer);
    MarkCritical();
    return;
  }
  // A brand-new phone against a full registry: the store-side overflow path was
  // supposed to have evicted a bond before this point. Refuse to silently evict
  // a second peer at the registry level -- record the disagreement instead.
  invariantViolations++;
}

BondStorePolicy::DirtyState BondStorePolicy::Dirty() const {
  return {criticalGeneration > criticalAcknowledged, usageGeneration > usageAcknowledged, generation};
}

void BondStorePolicy::AcknowledgePersisted(uint64_t acknowledged) {
  if (acknowledged > criticalAcknowledged) {
    criticalAcknowledged = acknowledged;
  }
  if (acknowledged > usageAcknowledged) {
    usageAcknowledged = acknowledged;
  }
}

bool BondStorePolicy::RestoreRegistry(const BondRegistry::Snapshot& snapshot, uint64_t restoredGeneration) {
  if (!registry.Restore(snapshot)) {
    return false;
  }
  generation = restoredGeneration;
  criticalGeneration = generation;
  usageGeneration = generation;
  criticalAcknowledged = generation;
  usageAcknowledged = generation;
  return true;
}

bool BondStorePolicy::ValidateStoreAlignment(const BondRegistry::Snapshot& registrySnapshot,
                                             const BondRegistry::PeerIdentity* ourSecs,
                                             size_t ourCount,
                                             const BondRegistry::PeerIdentity* peerSecs,
                                             size_t peerCount,
                                             const CccdKey* cccds,
                                             size_t cccdCount) {
  // A restorable registry snapshot gives a valid, duplicate-free peer set to
  // check the store records against.
  BondRegistry probe;
  if (!probe.Restore(registrySnapshot)) {
    return false;
  }

  // Exactly one OUR_SEC and one PEER_SEC per registered peer.
  if (ourCount != registrySnapshot.count || peerCount != registrySnapshot.count) {
    return false;
  }

  // Each security record must be a valid identity, belong to a registered peer,
  // and not repeat. Count + membership + uniqueness together force a bijection,
  // so no registry peer is missing and none is covered twice.
  for (size_t i = 0; i < ourCount; i++) {
    if (!BondRegistry::IsValidPeer(ourSecs[i]) || !probe.Contains(ourSecs[i])) {
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (ourSecs[i] == ourSecs[j]) {
        return false;
      }
    }
  }
  for (size_t i = 0; i < peerCount; i++) {
    if (!BondRegistry::IsValidPeer(peerSecs[i]) || !probe.Contains(peerSecs[i])) {
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (peerSecs[i] == peerSecs[j]) {
        return false;
      }
    }
  }

  // Every CCCD belongs to a registered peer and is unique by (peer, handle).
  for (size_t i = 0; i < cccdCount; i++) {
    if (!BondRegistry::IsValidPeer(cccds[i].peer) || !probe.Contains(cccds[i].peer)) {
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (cccds[i] == cccds[j]) {
        return false;
      }
    }
  }

  return true;
}
