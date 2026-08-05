#include "components/ble/BondStoreCodec.h"

#include <array>
#include <cstdint>
#include <cstdio>

using Pinetime::Controllers::BondRegistry;
using Pinetime::Controllers::BondSecurityRecord;
using Pinetime::Controllers::BondStoreCodec;
using Pinetime::Controllers::NimbleBondStoreSnapshot;

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

  BondSecurityRecord Security(uint8_t value) {
    BondSecurityRecord record;
    record.peer = Peer(value);
    record.keySize = 16;
    record.ediv = static_cast<uint16_t>(0x1200 + value);
    record.rand = 0x8877665544332200ull + value;
    for (size_t i = 0; i < 16; i++) {
      record.ltk[i] = static_cast<uint8_t>(value + i);
      record.irk[i] = static_cast<uint8_t>(value + 0x20 + i);
      record.csrk[i] = static_cast<uint8_t>(value + 0x40 + i);
    }
    record.ltkPresent = true;
    record.irkPresent = true;
    record.csrkPresent = (value & 1u) != 0;
    record.authenticated = true;
    record.secureConnections = true;
    return record;
  }

  NimbleBondStoreSnapshot Snapshot(uint8_t peerCount, uint8_t cccdCount) {
    NimbleBondStoreSnapshot snapshot;
    snapshot.generation = 0x0102030405060708ull;
    snapshot.registry.resetEpoch = 3;
    snapshot.registry.evictionCount = 7;
    snapshot.registry.count = peerCount;
    snapshot.registry.nextUseSequence = peerCount + 10;
    snapshot.ourSecCount = peerCount;
    snapshot.peerSecCount = peerCount;
    snapshot.cccdCount = cccdCount;
    for (uint8_t i = 0; i < peerCount; i++) {
      snapshot.registry.entries[i] = {Peer(i + 1), static_cast<uint32_t>(i + 1)};
      snapshot.ourSecs[i] = Security(i + 1);
      snapshot.peerSecs[i] = Security(i + 1);
      snapshot.peerSecs[i].ediv++;
    }
    for (uint8_t i = 0; i < cccdCount; i++) {
      snapshot.cccds[i] = {Peer(static_cast<uint8_t>((i % peerCount) + 1)),
                           static_cast<uint16_t>(0x0100 + i),
                           static_cast<uint16_t>(((i & 1u) + 1) | (i == 0 ? 0x0080 : 0)),
                           (i & 1u) != 0};
    }
    return snapshot;
  }

  void PutU32(uint8_t* data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value);
    data[1] = static_cast<uint8_t>(value >> 8);
    data[2] = static_cast<uint8_t>(value >> 16);
    data[3] = static_cast<uint8_t>(value >> 24);
  }

  void RefreshCrc(BondStoreCodec::Buffer& buffer, size_t size) {
    PutU32(buffer.data() + 48,
           BondStoreCodec::Crc32(buffer.data() + BondStoreCodec::HeaderSize,
                                 size - BondStoreCodec::HeaderSize));
  }
}

int main() {
  static_assert(BondStoreCodec::MaxEncodedSize == 1304);

  {
    NimbleBondStoreSnapshot empty;
    BondStoreCodec::Buffer encoded {};
    size_t size = 0;
    Check(BondStoreCodec::Encode(empty, encoded, size), "empty snapshot encodes");
    static constexpr std::array<uint8_t, 84> Golden {
      0x49, 0x42, 0x53, 0x31, 0x01, 0x00, 0x34, 0x00, 0x54, 0x00, 0x00, 0x00,
      0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x44, 0x00, 0x0c, 0x00, 0x0c, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x8e, 0x09, 0xd1, 0xc4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    };
    Check(size == Golden.size(), "golden size is stable");
    bool matches = size == Golden.size();
    for (size_t i = 0; i < size && matches; i++) {
      matches = encoded[i] == Golden[i];
    }
    Check(matches, "empty snapshot matches golden bytes");
  }

  {
    const auto source = Snapshot(3, 7);
    BondStoreCodec::Buffer first {};
    BondStoreCodec::Buffer second {};
    size_t firstSize = 0;
    size_t secondSize = 0;
    Check(BondStoreCodec::Encode(source, first, firstSize), "non-empty snapshot encodes");
    Check(BondStoreCodec::Encode(source, second, secondSize), "same snapshot encodes twice");
    Check(firstSize == secondSize && first == second, "encoding is deterministic");

    NimbleBondStoreSnapshot decoded;
    const auto result = BondStoreCodec::Decode(first.data(), firstSize, decoded);
    Check(static_cast<bool>(result), "encoded snapshot decodes");
    Check(result.formatInitialized, "format-initialized marker round-trips");
    Check(decoded == source, "all explicit security, CCCD, registry, epoch, and generation fields round-trip");

    auto semanticCopy = source.ourSecs[0];
    Check(semanticCopy == source.ourSecs[0], "field-wise security equality accepts identical records");
    semanticCopy.authenticated = !semanticCopy.authenticated;
    Check(!(semanticCopy == source.ourSecs[0]), "field-wise security equality detects flag changes");

    auto corrupt = first;
    corrupt[BondStoreCodec::HeaderSize + 5] ^= 0x80;
    decoded = source;
    Check(BondStoreCodec::Decode(corrupt.data(), firstSize, decoded).error == BondStoreCodec::DecodeError::Crc,
          "payload corruption is rejected by CRC");
    Check(decoded.ourSecCount == 0 && decoded.peerSecCount == 0 && decoded.cccdCount == 0 &&
            decoded.registry.count == 0 && decoded.generation == 0,
          "decode clears caller-owned output before an early failure");
    Check(BondStoreCodec::Decode(first.data(), firstSize - 1, decoded).error == BondStoreCodec::DecodeError::Length,
          "truncation is rejected");
    Check(BondStoreCodec::Decode(first.data(), BondStoreCodec::HeaderSize - 1, decoded).error ==
            BondStoreCodec::DecodeError::TooShort,
          "short header is rejected");

    auto future = first;
    future[4] = static_cast<uint8_t>(BondStoreCodec::Version + 1);
    Check(BondStoreCodec::Decode(future.data(), firstSize, decoded).error == BondStoreCodec::DecodeError::FutureVersion,
          "future version is rejected explicitly");
  }

  {
    const auto maximum = Snapshot(Pinetime::Controllers::CompanionProtocol::RetainedPeers,
                                  Pinetime::Controllers::CompanionProtocol::MaxCccds);
    BondStoreCodec::Buffer encoded {};
    size_t size = 0;
    Check(BondStoreCodec::Encode(maximum, encoded, size), "maximum-capacity snapshot encodes");
    Check(size == BondStoreCodec::MaxEncodedSize, "maximum-capacity snapshot fills fixed buffer exactly");
    NimbleBondStoreSnapshot decoded;
    Check(static_cast<bool>(BondStoreCodec::Decode(encoded.data(), size, decoded)),
          "maximum-capacity snapshot decodes");
    Check(decoded == maximum, "maximum-capacity snapshot round-trips");
  }

  {
    const auto source = Snapshot(2, 2);
    BondStoreCodec::Buffer encoded {};
    size_t size = 0;
    BondStoreCodec::Encode(source, encoded, size);
    NimbleBondStoreSnapshot decoded;

    // Second OUR_SEC peer starts after metadata plus one security record.
    auto duplicateSecurity = encoded;
    const size_t firstOur = BondStoreCodec::HeaderSize + BondStoreCodec::MetadataSize;
    const size_t secondOur = firstOur + BondStoreCodec::SecurityRecordSize;
    for (size_t i = 0; i < 7; i++) {
      duplicateSecurity[secondOur + i] = duplicateSecurity[firstOur + i];
    }
    RefreshCrc(duplicateSecurity, size);
    Check(BondStoreCodec::Decode(duplicateSecurity.data(), size, decoded).error == BondStoreCodec::DecodeError::Duplicate,
          "duplicate security identity is rejected after CRC validation");

    auto duplicateCccd = encoded;
    const size_t firstCccd = BondStoreCodec::HeaderSize + BondStoreCodec::MetadataSize +
                             (4 * BondStoreCodec::SecurityRecordSize);
    const size_t secondCccd = firstCccd + BondStoreCodec::CccdRecordSize;
    for (size_t i = 0; i < BondStoreCodec::CccdRecordSize; i++) {
      duplicateCccd[secondCccd + i] = duplicateCccd[firstCccd + i];
    }
    RefreshCrc(duplicateCccd, size);
    Check(BondStoreCodec::Decode(duplicateCccd.data(), size, decoded).error == BondStoreCodec::DecodeError::Duplicate,
          "duplicate CCCD key is rejected after CRC validation");

    auto badKeySize = encoded;
    badKeySize[firstOur + 7] = 17;
    RefreshCrc(badKeySize, size);
    Check(BondStoreCodec::Decode(badKeySize.data(), size, decoded).error == BondStoreCodec::DecodeError::Range,
          "security field ranges are validated");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
