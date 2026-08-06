#pragma once

#include "components/fs/FamilyState.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {
  class FamilyStateCodec {
  public:
    static constexpr uint16_t Version = CompanionProtocol::FamilyStateSnapshotSchemaVersion;
    static constexpr size_t HeaderSize = 24;
    static constexpr size_t SettingsSize = 30;
    static constexpr size_t ScheduleMetadataSize = 8;
    static constexpr size_t TaskMetadataSize = 8;
    static constexpr size_t TaskStatsSize = 8;
    static constexpr size_t AlarmMetadataSize = 8;
    static constexpr size_t AlarmRecordSize = 4;
    static constexpr size_t PrayerSize = 12;
    static constexpr size_t FindMySize = 32;
    static constexpr size_t PayloadSize =
      SettingsSize +
      ScheduleMetadataSize +
      CompanionProtocol::ScheduleCapacity * CompanionProtocol::ScheduleRecordSize +
      TaskMetadataSize +
      CompanionProtocol::TaskCapacity * CompanionProtocol::TaskRecordSize +
      TaskStatsSize +
      AlarmMetadataSize +
      5 * AlarmRecordSize +
      PrayerSize +
      FindMySize;
    static constexpr size_t EncodedSize = HeaderSize + PayloadSize;

    using Buffer = std::array<uint8_t, EncodedSize>;

    enum class DecodeError : uint8_t {
      None,
      Length,
      Magic,
      Version,
      Header,
      Reserved,
      Crc,
      Range,
      Padding,
    };

    struct DecodeResult {
      DecodeError error = DecodeError::None;

      explicit operator bool() const {
        return error == DecodeError::None;
      }
    };

    static bool Encode(const FamilyState& state, Buffer& output);
    static DecodeResult Decode(const uint8_t* data, size_t size, FamilyState& output);
    static bool Validate(const FamilyState& state, DecodeError* error = nullptr);
  };
}
