#include "components/ble/BondStoreCodec.h"

#include "components/ble/BondStorePolicy.h"

#include <array>
#include <limits>

using Pinetime::Controllers::BondCccdRecord;
using Pinetime::Controllers::BondRegistry;
using Pinetime::Controllers::BondSecurityRecord;
using Pinetime::Controllers::BondStoreCodec;
using Pinetime::Controllers::BondStorePolicy;
using Pinetime::Controllers::NimbleBondStoreSnapshot;

namespace {
  constexpr std::array<uint8_t, 4> Magic {'I', 'B', 'S', '1'};

  class Writer {
  public:
    Writer(uint8_t* data, size_t capacity) : data {data}, capacity {capacity} {
    }

    bool U8(uint8_t value) {
      if (position >= capacity) {
        return false;
      }
      data[position++] = value;
      return true;
    }

    bool U16(uint16_t value) {
      return U8(static_cast<uint8_t>(value)) && U8(static_cast<uint8_t>(value >> 8));
    }

    bool U32(uint32_t value) {
      return U16(static_cast<uint16_t>(value)) && U16(static_cast<uint16_t>(value >> 16));
    }

    bool U64(uint64_t value) {
      return U32(static_cast<uint32_t>(value)) && U32(static_cast<uint32_t>(value >> 32));
    }

    template<size_t Size>
    bool Bytes(const std::array<uint8_t, Size>& value) {
      for (uint8_t byte : value) {
        if (!U8(byte)) {
          return false;
        }
      }
      return true;
    }

    size_t Position() const {
      return position;
    }

  private:
    uint8_t* data;
    size_t capacity;
    size_t position = 0;
  };

  class Reader {
  public:
    Reader(const uint8_t* data, size_t size) : data {data}, size {size} {
    }

    bool U8(uint8_t& value) {
      if (position >= size) {
        return false;
      }
      value = data[position++];
      return true;
    }

    bool U16(uint16_t& value) {
      uint8_t low;
      uint8_t high;
      if (!U8(low) || !U8(high)) {
        return false;
      }
      value = static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
      return true;
    }

    bool U32(uint32_t& value) {
      uint16_t low;
      uint16_t high;
      if (!U16(low) || !U16(high)) {
        return false;
      }
      value = static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16);
      return true;
    }

    bool U64(uint64_t& value) {
      uint32_t low;
      uint32_t high;
      if (!U32(low) || !U32(high)) {
        return false;
      }
      value = static_cast<uint64_t>(low) | (static_cast<uint64_t>(high) << 32);
      return true;
    }

    template<size_t Size>
    bool Bytes(std::array<uint8_t, Size>& value) {
      for (uint8_t& byte : value) {
        if (!U8(byte)) {
          return false;
        }
      }
      return true;
    }

    size_t Position() const {
      return position;
    }

  private:
    const uint8_t* data;
    size_t size;
    size_t position = 0;
  };

  bool WritePeer(Writer& writer, const BondRegistry::PeerIdentity& peer) {
    return writer.U8(peer.type) && writer.Bytes(peer.address);
  }

  bool ReadPeer(Reader& reader, BondRegistry::PeerIdentity& peer) {
    return reader.U8(peer.type) && reader.Bytes(peer.address);
  }

  bool WriteSecurity(Writer& writer, const BondSecurityRecord& record) {
    uint8_t flags = 0;
    flags |= record.ltkPresent ? 1u << 0 : 0;
    flags |= record.irkPresent ? 1u << 1 : 0;
    flags |= record.csrkPresent ? 1u << 2 : 0;
    flags |= record.authenticated ? 1u << 3 : 0;
    flags |= record.secureConnections ? 1u << 4 : 0;
    return WritePeer(writer, record.peer) && writer.U8(record.keySize) && writer.U16(record.ediv) &&
           writer.U64(record.rand) && writer.Bytes(record.ltk) && writer.Bytes(record.irk) &&
           writer.Bytes(record.csrk) && writer.U8(flags) && writer.U8(0);
  }

  bool ReadSecurity(Reader& reader, BondSecurityRecord& record) {
    uint8_t flags;
    uint8_t reserved;
    if (!ReadPeer(reader, record.peer) || !reader.U8(record.keySize) || !reader.U16(record.ediv) ||
        !reader.U64(record.rand) || !reader.Bytes(record.ltk) || !reader.Bytes(record.irk) ||
        !reader.Bytes(record.csrk) || !reader.U8(flags) || !reader.U8(reserved)) {
      return false;
    }
    if ((flags & 0xe0u) != 0 || reserved != 0) {
      return false;
    }
    record.ltkPresent = (flags & (1u << 0)) != 0;
    record.irkPresent = (flags & (1u << 1)) != 0;
    record.csrkPresent = (flags & (1u << 2)) != 0;
    record.authenticated = (flags & (1u << 3)) != 0;
    record.secureConnections = (flags & (1u << 4)) != 0;
    return true;
  }

  bool WriteCccd(Writer& writer, const BondCccdRecord& record) {
    return WritePeer(writer, record.peer) && writer.U16(record.handle) && writer.U16(record.flags) &&
           writer.U8(record.valueChanged ? 1 : 0);
  }

  bool ReadCccd(Reader& reader, BondCccdRecord& record) {
    uint8_t valueChanged;
    if (!ReadPeer(reader, record.peer) || !reader.U16(record.handle) || !reader.U16(record.flags) ||
        !reader.U8(valueChanged) || valueChanged > 1) {
      return false;
    }
    record.valueChanged = valueChanged != 0;
    return true;
  }

  bool WriteRegistryEntry(Writer& writer, const BondRegistry::Entry& entry) {
    return WritePeer(writer, entry.peer) && writer.U32(entry.lastUsed) && writer.U8(0);
  }

  bool ReadRegistryEntry(Reader& reader, BondRegistry::Entry& entry) {
    uint8_t reserved;
    return ReadPeer(reader, entry.peer) && reader.U32(entry.lastUsed) && reader.U8(reserved) && reserved == 0;
  }

  bool Contains(const BondRegistry::Snapshot& registry, const BondRegistry::PeerIdentity& peer) {
    for (size_t i = 0; i < registry.count; i++) {
      if (registry.entries[i].peer == peer) {
        return true;
      }
    }
    return false;
  }

  bool DuplicateSecurity(const std::array<BondSecurityRecord, Pinetime::Controllers::CompanionProtocol::RetainedPeers>& records,
                         uint8_t count) {
    for (size_t i = 0; i < count; i++) {
      for (size_t j = 0; j < i; j++) {
        if (records[i].peer == records[j].peer) {
          return true;
        }
      }
    }
    return false;
  }
}

uint32_t BondStoreCodec::Crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }
  return ~crc;
}

bool BondStoreCodec::Validate(const NimbleBondStoreSnapshot& snapshot, DecodeError* error) {
  const auto fail = [error](DecodeError reason) {
    if (error != nullptr) {
      *error = reason;
    }
    return false;
  };

  if (snapshot.ourSecCount > CompanionProtocol::RetainedPeers ||
      snapshot.peerSecCount > CompanionProtocol::RetainedPeers ||
      snapshot.cccdCount > CompanionProtocol::MaxCccds ||
      snapshot.registry.count > CompanionProtocol::RetainedPeers) {
    return fail(DecodeError::Count);
  }
  if (snapshot.generation == std::numeric_limits<uint64_t>::max() ||
      snapshot.registry.resetEpoch == std::numeric_limits<uint32_t>::max()) {
    return fail(DecodeError::Range);
  }

  BondRegistry registry;
  if (!registry.Restore(snapshot.registry)) {
    return fail(DecodeError::Range);
  }

  for (size_t i = 0; i < snapshot.ourSecCount; i++) {
    const auto& record = snapshot.ourSecs[i];
    if (!BondRegistry::IsValidPeer(record.peer) || record.keySize > 16 ||
        (record.ltkPresent && record.keySize < 7)) {
      return fail(DecodeError::Range);
    }
  }
  for (size_t i = 0; i < snapshot.peerSecCount; i++) {
    const auto& record = snapshot.peerSecs[i];
    if (!BondRegistry::IsValidPeer(record.peer) || record.keySize > 16 ||
        (record.ltkPresent && record.keySize < 7)) {
      return fail(DecodeError::Range);
    }
  }
  if (DuplicateSecurity(snapshot.ourSecs, snapshot.ourSecCount) ||
      DuplicateSecurity(snapshot.peerSecs, snapshot.peerSecCount)) {
    return fail(DecodeError::Duplicate);
  }

  std::array<BondRegistry::PeerIdentity, CompanionProtocol::RetainedPeers> our {};
  std::array<BondRegistry::PeerIdentity, CompanionProtocol::RetainedPeers> peer {};
  std::array<BondStorePolicy::CccdKey, CompanionProtocol::MaxCccds> cccds {};
  for (size_t i = 0; i < snapshot.ourSecCount; i++) {
    our[i] = snapshot.ourSecs[i].peer;
  }
  for (size_t i = 0; i < snapshot.peerSecCount; i++) {
    peer[i] = snapshot.peerSecs[i].peer;
  }
  for (size_t i = 0; i < snapshot.cccdCount; i++) {
    const auto& record = snapshot.cccds[i];
    // Notify, indicate, and NimBLE's internal "modified" bit are the only
    // defined CCCD flags. A stored record with neither notify nor indicate is
    // meaningless and should have been deleted instead.
    if (!BondRegistry::IsValidPeer(record.peer) || record.handle == 0 ||
        (record.flags & ~0x0083u) != 0 || (record.flags & 0x0003u) == 0) {
      return fail(DecodeError::Range);
    }
    for (size_t previous = 0; previous < i; previous++) {
      if (record.peer == snapshot.cccds[previous].peer && record.handle == snapshot.cccds[previous].handle) {
        return fail(DecodeError::Duplicate);
      }
    }
    if (!Contains(snapshot.registry, record.peer)) {
      return fail(DecodeError::Alignment);
    }
    cccds[i] = {record.peer, record.handle};
  }

  if (!BondStorePolicy::ValidateStoreAlignment(snapshot.registry,
                                                our.data(),
                                                snapshot.ourSecCount,
                                                peer.data(),
                                                snapshot.peerSecCount,
                                                cccds.data(),
                                                snapshot.cccdCount)) {
    return fail(DecodeError::Alignment);
  }

  if (error != nullptr) {
    *error = DecodeError::None;
  }
  return true;
}

bool BondStoreCodec::Encode(const NimbleBondStoreSnapshot& snapshot,
                            Buffer& output,
                            size_t& outputSize,
                            bool formatInitialized) {
  if (!Validate(snapshot)) {
    outputSize = 0;
    return false;
  }

  const uint32_t flags = formatInitialized ? FormatInitializedFlag : 0;
  const size_t payloadSize = MetadataSize +
                             ((snapshot.ourSecCount + snapshot.peerSecCount) * SecurityRecordSize) +
                             (snapshot.cccdCount * CccdRecordSize) +
                             (snapshot.registry.count * RegistryRecordSize);
  const size_t totalSize = HeaderSize + payloadSize;
  if (totalSize > output.size()) {
    outputSize = 0;
    return false;
  }

  Writer payload {output.data() + HeaderSize, output.size() - HeaderSize};
  bool ok = payload.U64(snapshot.generation) && payload.U32(snapshot.registry.resetEpoch) &&
            payload.U32(snapshot.registry.nextUseSequence) && payload.U32(snapshot.registry.evictionCount) &&
            payload.U16(snapshot.ourSecCount) && payload.U16(snapshot.peerSecCount) &&
            payload.U16(snapshot.cccdCount) && payload.U16(snapshot.registry.count) && payload.U32(flags);
  for (size_t i = 0; i < snapshot.ourSecCount && ok; i++) {
    ok = WriteSecurity(payload, snapshot.ourSecs[i]);
  }
  for (size_t i = 0; i < snapshot.peerSecCount && ok; i++) {
    ok = WriteSecurity(payload, snapshot.peerSecs[i]);
  }
  for (size_t i = 0; i < snapshot.cccdCount && ok; i++) {
    ok = WriteCccd(payload, snapshot.cccds[i]);
  }
  for (size_t i = 0; i < snapshot.registry.count && ok; i++) {
    ok = WriteRegistryEntry(payload, snapshot.registry.entries[i]);
  }
  if (!ok || payload.Position() != payloadSize) {
    outputSize = 0;
    return false;
  }

  const uint32_t crc = Crc32(output.data() + HeaderSize, payloadSize);
  Writer header {output.data(), HeaderSize};
  for (uint8_t byte : Magic) {
    ok = ok && header.U8(byte);
  }
  ok = ok && header.U16(Version) && header.U16(HeaderSize) && header.U32(totalSize) &&
       header.U32(payloadSize) && header.U64(snapshot.generation) &&
       header.U32(snapshot.registry.resetEpoch) && header.U16(snapshot.ourSecCount) &&
       header.U16(snapshot.peerSecCount) && header.U16(snapshot.cccdCount) &&
       header.U16(snapshot.registry.count) && header.U16(SecurityRecordSize) &&
       header.U16(CccdRecordSize) && header.U16(RegistryRecordSize) &&
       header.U16(MetadataSize) && header.U32(flags) && header.U32(crc);
  if (!ok || header.Position() != HeaderSize) {
    outputSize = 0;
    return false;
  }
  outputSize = totalSize;
  return true;
}

BondStoreCodec::DecodeResult BondStoreCodec::Decode(const uint8_t* data,
                                                     size_t size,
                                                     NimbleBondStoreSnapshot& output) {
  output.Clear();
  if (size < HeaderSize) {
    return {DecodeError::TooShort, false};
  }

  Reader header {data, HeaderSize};
  for (uint8_t expected : Magic) {
    uint8_t actual;
    if (!header.U8(actual) || actual != expected) {
      return {DecodeError::Magic, false};
    }
  }

  uint16_t version;
  uint16_t headerSize;
  uint32_t totalSize;
  uint32_t payloadSize;
  uint64_t generation;
  uint32_t resetEpoch;
  uint16_t ourCount;
  uint16_t peerCount;
  uint16_t cccdCount;
  uint16_t registryCount;
  uint16_t securityRecordSize;
  uint16_t cccdRecordSize;
  uint16_t registryRecordSize;
  uint16_t metadataSize;
  uint32_t flags;
  uint32_t expectedCrc;
  if (!header.U16(version) || !header.U16(headerSize) || !header.U32(totalSize) ||
      !header.U32(payloadSize) || !header.U64(generation) || !header.U32(resetEpoch) ||
      !header.U16(ourCount) || !header.U16(peerCount) || !header.U16(cccdCount) ||
      !header.U16(registryCount) || !header.U16(securityRecordSize) ||
      !header.U16(cccdRecordSize) || !header.U16(registryRecordSize) ||
      !header.U16(metadataSize) || !header.U32(flags) || !header.U32(expectedCrc)) {
    return {DecodeError::Header, false};
  }
  if (version > Version) {
    return {DecodeError::FutureVersion, false};
  }
  if (version != Version) {
    return {DecodeError::Version, false};
  }
  if (headerSize != HeaderSize || metadataSize != MetadataSize || header.Position() != HeaderSize) {
    return {DecodeError::Header, false};
  }
  if (totalSize != size || payloadSize != size - HeaderSize) {
    return {DecodeError::Length, false};
  }
  if (ourCount > CompanionProtocol::RetainedPeers || peerCount > CompanionProtocol::RetainedPeers ||
      cccdCount > CompanionProtocol::MaxCccds || registryCount > CompanionProtocol::RetainedPeers) {
    return {DecodeError::Count, false};
  }
  if (securityRecordSize != SecurityRecordSize || cccdRecordSize != CccdRecordSize ||
      registryRecordSize != RegistryRecordSize) {
    return {DecodeError::RecordSize, false};
  }
  if ((flags & ~FormatInitializedFlag) != 0) {
    return {DecodeError::Flags, false};
  }

  const size_t expectedPayloadSize = MetadataSize + ((ourCount + peerCount) * SecurityRecordSize) +
                                     (cccdCount * CccdRecordSize) + (registryCount * RegistryRecordSize);
  if (payloadSize != expectedPayloadSize) {
    return {DecodeError::Length, false};
  }
  if (Crc32(data + HeaderSize, payloadSize) != expectedCrc) {
    return {DecodeError::Crc, false};
  }

  Reader payload {data + HeaderSize, payloadSize};
  uint64_t payloadGeneration;
  uint32_t payloadResetEpoch;
  uint16_t payloadOurCount;
  uint16_t payloadPeerCount;
  uint16_t payloadCccdCount;
  uint16_t payloadRegistryCount;
  uint32_t payloadFlags;
  if (!payload.U64(payloadGeneration) || !payload.U32(payloadResetEpoch) ||
      !payload.U32(output.registry.nextUseSequence) || !payload.U32(output.registry.evictionCount) ||
      !payload.U16(payloadOurCount) || !payload.U16(payloadPeerCount) ||
      !payload.U16(payloadCccdCount) || !payload.U16(payloadRegistryCount) ||
      !payload.U32(payloadFlags)) {
    return {DecodeError::Length, false};
  }
  if (payloadGeneration != generation || payloadResetEpoch != resetEpoch ||
      payloadOurCount != ourCount || payloadPeerCount != peerCount ||
      payloadCccdCount != cccdCount || payloadRegistryCount != registryCount ||
      payloadFlags != flags) {
    return {DecodeError::Header, false};
  }

  output.generation = generation;
  output.registry.resetEpoch = resetEpoch;
  output.ourSecCount = static_cast<uint8_t>(ourCount);
  output.peerSecCount = static_cast<uint8_t>(peerCount);
  output.cccdCount = static_cast<uint8_t>(cccdCount);
  output.registry.count = static_cast<uint8_t>(registryCount);

  for (size_t i = 0; i < output.ourSecCount; i++) {
    if (!ReadSecurity(payload, output.ourSecs[i])) {
      return {DecodeError::Range, false};
    }
  }
  for (size_t i = 0; i < output.peerSecCount; i++) {
    if (!ReadSecurity(payload, output.peerSecs[i])) {
      return {DecodeError::Range, false};
    }
  }
  for (size_t i = 0; i < output.cccdCount; i++) {
    if (!ReadCccd(payload, output.cccds[i])) {
      return {DecodeError::Range, false};
    }
  }
  for (size_t i = 0; i < output.registry.count; i++) {
    if (!ReadRegistryEntry(payload, output.registry.entries[i])) {
      return {DecodeError::Range, false};
    }
  }
  if (payload.Position() != payloadSize) {
    return {DecodeError::Length, false};
  }

  DecodeError validationError;
  if (!Validate(output, &validationError)) {
    return {validationError, false};
  }
  return {DecodeError::None, (flags & FormatInitializedFlag) != 0};
}
