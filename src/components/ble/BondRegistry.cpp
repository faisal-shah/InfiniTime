#include "components/ble/BondRegistry.h"

#include <limits>

using Pinetime::Controllers::BondRegistry;

std::optional<size_t> BondRegistry::FindIndex(const PeerIdentity& peer) const {
  for (size_t i = 0; i < count; i++) {
    if (entries[i].peer == peer) {
      return i;
    }
  }
  return std::nullopt;
}

const BondRegistry::Entry* BondRegistry::Find(const PeerIdentity& peer) const {
  const auto index = FindIndex(peer);
  return index ? &entries[*index] : nullptr;
}

const BondRegistry::Entry& BondRegistry::At(size_t index) const {
  return entries[index];
}

bool BondRegistry::Contains(const PeerIdentity& peer) const {
  return FindIndex(peer).has_value();
}

uint32_t BondRegistry::NextUseSequence() {
  if (nextUseSequence == std::numeric_limits<uint32_t>::max()) {
    NormalizeUseSequences();
  }
  return nextUseSequence++;
}

void BondRegistry::NormalizeUseSequences() {
  std::array<uint32_t, Capacity> prior {};
  for (size_t i = 0; i < count; i++) {
    prior[i] = entries[i].lastUsed;
  }
  for (size_t i = 0; i < count; i++) {
    uint32_t rank = 1;
    for (size_t other = 0; other < count; other++) {
      if (prior[other] < prior[i]) {
        rank++;
      }
    }
    entries[i].lastUsed = rank;
  }
  nextUseSequence = static_cast<uint32_t>(count + 1);
}

BondRegistry::TouchResult BondRegistry::Touch(const PeerIdentity& peer) {
  const auto index = FindIndex(peer);
  if (!index) {
    return TouchResult::Unknown;
  }
  for (size_t i = 0; i < count; i++) {
    if (entries[i].lastUsed > entries[*index].lastUsed) {
      entries[*index].lastUsed = NextUseSequence();
      return TouchResult::OrderChanged;
    }
  }
  return TouchResult::AlreadyMostRecent;
}

std::optional<BondRegistry::PeerIdentity> BondRegistry::EvictionCandidate(const PeerIdentity& incoming) const {
  if (!IsValidPeer(incoming) || Contains(incoming) || count < Capacity) {
    return std::nullopt;
  }
  return entries[LeastRecentlyUsedIndex()].peer;
}

size_t BondRegistry::LeastRecentlyUsedIndex() const {
  size_t least = 0;
  for (size_t i = 1; i < count; i++) {
    if (entries[i].lastUsed < entries[least].lastUsed) {
      least = i;
    }
  }
  return least;
}

BondRegistry::Admission BondRegistry::Admit(const PeerIdentity& peer) {
  if (!IsValidPeer(peer)) {
    return {AdmissionKind::Rejected, std::nullopt};
  }
  if (Touch(peer) != TouchResult::Unknown) {
    return {AdmissionKind::Existing, std::nullopt};
  }

  if (count < Capacity) {
    entries[count++] = Entry {peer, NextUseSequence()};
    return {AdmissionKind::Added, std::nullopt};
  }

  const size_t index = LeastRecentlyUsedIndex();
  const PeerIdentity evicted = entries[index].peer;
  entries[index] = Entry {peer, NextUseSequence()};
  evictionCount++;
  return {AdmissionKind::Evicted, evicted};
}

bool BondRegistry::Remove(const PeerIdentity& peer) {
  const auto index = FindIndex(peer);
  if (!index) {
    return false;
  }
  for (size_t i = *index + 1; i < count; i++) {
    entries[i - 1] = entries[i];
  }
  entries[--count] = {};
  return true;
}

bool BondRegistry::RemoveForEviction(const PeerIdentity& peer) {
  if (!Remove(peer)) {
    return false;
  }
  evictionCount++;
  return true;
}

void BondRegistry::ForgetAll() {
  entries = {};
  count = 0;
  nextUseSequence = 1;
  resetEpoch++;
}

BondRegistry::Snapshot BondRegistry::Capture() const {
  Snapshot snapshot;
  snapshot.entries = entries;
  snapshot.count = static_cast<uint8_t>(count);
  snapshot.nextUseSequence = nextUseSequence;
  snapshot.resetEpoch = resetEpoch;
  snapshot.evictionCount = evictionCount;
  return snapshot;
}

bool BondRegistry::IsValidPeer(const PeerIdentity& peer) {
  if (peer.type > 3) {
    return false;
  }
  for (uint8_t value : peer.address) {
    if (value != 0) {
      return true;
    }
  }
  return false;
}

bool BondRegistry::Restore(const Snapshot& snapshot) {
  if (snapshot.count > Capacity || snapshot.nextUseSequence == 0) {
    return false;
  }

  for (size_t i = 0; i < snapshot.count; i++) {
    if (!IsValidPeer(snapshot.entries[i].peer) || snapshot.entries[i].lastUsed == 0) {
      return false;
    }
    for (size_t other = i + 1; other < snapshot.count; other++) {
      if (snapshot.entries[i].peer == snapshot.entries[other].peer ||
          snapshot.entries[i].lastUsed == snapshot.entries[other].lastUsed) {
        return false;
      }
    }
  }

  uint32_t maxUsed = 0;
  for (size_t i = 0; i < snapshot.count; i++) {
    if (snapshot.entries[i].lastUsed > maxUsed) {
      maxUsed = snapshot.entries[i].lastUsed;
    }
  }
  if (snapshot.count > 0 && snapshot.nextUseSequence <= maxUsed) {
      return false;
  }

  entries = snapshot.entries;
  for (size_t i = snapshot.count; i < Capacity; i++) {
    entries[i] = {};
  }
  count = snapshot.count;
  nextUseSequence = snapshot.nextUseSequence;
  resetEpoch = snapshot.resetEpoch;
  evictionCount = snapshot.evictionCount;
  return true;
}
