#pragma once

#include "components/ble/BondStoreSnapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {
  class BondStoreCodec {
  public:
    static constexpr uint16_t Version = 1;
    static constexpr size_t HeaderSize = 52;
    static constexpr size_t MetadataSize = 32;
    static constexpr size_t SecurityRecordSize = 68;
    static constexpr size_t CccdRecordSize = 12;
    static constexpr size_t RegistryRecordSize = 12;
    static constexpr uint32_t MigrationCompleteFlag = 1u;
    static constexpr size_t MaxEncodedSize =
      HeaderSize + MetadataSize + (2 * CompanionProtocol::RetainedPeers * SecurityRecordSize) +
      (CompanionProtocol::MaxCccds * CccdRecordSize) + (CompanionProtocol::RetainedPeers * RegistryRecordSize);

    using Buffer = std::array<uint8_t, MaxEncodedSize>;

    enum class DecodeError : uint8_t {
      None,
      TooShort,
      Magic,
      FutureVersion,
      Version,
      Header,
      Length,
      Count,
      RecordSize,
      Flags,
      Crc,
      Range,
      Duplicate,
      Alignment,
    };

    struct DecodeResult {
      DecodeError error = DecodeError::None;
      bool migrationComplete = false;

      explicit operator bool() const {
        return error == DecodeError::None;
      }
    };

    static bool Encode(const NimbleBondStoreSnapshot& snapshot,
                       Buffer& output,
                       size_t& outputSize,
                       bool migrationComplete = true);
    // Clears and decodes directly into caller-owned storage. On failure output
    // may contain a partial snapshot and must not be consumed.
    static DecodeResult Decode(const uint8_t* data,
                               size_t size,
                               NimbleBondStoreSnapshot& output);
    static bool Validate(const NimbleBondStoreSnapshot& snapshot, DecodeError* error = nullptr);
    static uint32_t Crc32(const uint8_t* data, size_t size);
  };
}
