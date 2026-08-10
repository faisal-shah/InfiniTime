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
    static constexpr size_t PayloadSize = SettingsSize + ScheduleMetadataSize +
                                          CompanionProtocol::ScheduleCapacity * CompanionProtocol::ScheduleRecordSize + TaskMetadataSize +
                                          CompanionProtocol::TaskCapacity * CompanionProtocol::TaskRecordSize + TaskStatsSize +
                                          AlarmMetadataSize + 5 * AlarmRecordSize + PrayerSize + FindMySize;
    static constexpr size_t EncodedSize = HeaderSize + PayloadSize;
    // Streaming deliberately uses a small fixed working set. Implementations
    // of Input and Output must accept requests up to this size, but never need
    // to retain data after Read/Write returns.
    static constexpr size_t StreamChunkSize = 64;

    using Buffer = std::array<uint8_t, EncodedSize>;

    class Output {
    public:
      virtual ~Output() = default;
      virtual bool Write(const uint8_t* data, size_t size) = 0;
    };

    class Input {
    public:
      virtual ~Input() = default;
      virtual bool Read(uint8_t* data, size_t size) = 0;
    };

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
    static bool Encode(const FamilyState& state, Output& output);
    static DecodeResult Decode(const uint8_t* data, size_t size, FamilyState& output);
    static DecodeResult Decode(Input& input, size_t size, FamilyState& output);
    static bool Validate(const FamilyState& state, DecodeError* error = nullptr);
  };
}
