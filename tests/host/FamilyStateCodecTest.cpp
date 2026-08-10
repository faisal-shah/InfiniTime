#include "components/fs/FamilyStateCodec.h"
#include "components/fs/Crc32.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using Pinetime::Controllers::FamilyState;
using Pinetime::Controllers::FamilyStateCodec;

namespace {
  namespace ScheduleRules = Pinetime::Controllers::ScheduleRules;

  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  FamilyState Fixture() {
    FamilyState state;
    state.generation = 9;
    state.settings.stepsGoal = 12000;
    state.settings.alwaysOnDisplay = true;
    state.settings.watchFace = 7;
    state.settings.wakeModes = 0x0a;

    state.scheduleCount = 1;
    state.scheduleVersion = 0x01020304;
    auto& event = state.schedules[0];
    event.id = 7;
    event.ruleKind = static_cast<uint8_t>(ScheduleRules::RuleKind::Weekly);
    event.hour = 18;
    event.minute = 30;
    event.anchorYear = 2026;
    event.anchorMonth = 8;
    event.anchorDay = 6;
    event.param = 0x2a;
    event.flags = 1;
    std::strcpy(event.title, "Quran practice");
    event.lastModified = 0x11223344;

    state.taskCount = 1;
    state.taskVersion = 0x55667788;
    state.tasks[0].id = 10;
    state.tasks[0].order = 0;
    std::strcpy(state.tasks[0].title.data(), "Brush teeth");
    state.tasks[0].lastModified = 0xaabbccdd;
    state.taskStreak = 12;
    state.taskRolloverDate = 20260806;

    state.alarmVersion = 4;
    state.alarms[0] = {7, 15, 1, true};

    state.prayer.method = 2;
    state.prayer.asrMadhab = 1;
    state.prayer.flags = 3;
    state.prayer.latitudeE2 = 2976;
    state.prayer.longitudeE2 = -9527;
    state.prayer.utcOffsetQuarters = -20;

    state.findMyKeyPresent = true;
    for (size_t index = 0; index < state.findMyKey.size(); index++) {
      state.findMyKey[index] = static_cast<uint8_t>(index + 1);
    }
    return state;
  }

  FamilyState MaximumFixture() {
    FamilyState state = Fixture();
    state.scheduleCount = Pinetime::Controllers::CompanionProtocol::ScheduleCapacity;
    for (size_t index = 0; index < state.schedules.size(); index++) {
      auto& event = state.schedules[index];
      event = {};
      event.id = static_cast<uint16_t>(index + 1);
      event.ruleKind = static_cast<uint8_t>(ScheduleRules::RuleKind::Weekly);
      event.hour = static_cast<uint8_t>(index % 24);
      event.minute = static_cast<uint8_t>((index * 7) % 60);
      event.anchorYear = 2026;
      event.anchorMonth = 8;
      event.anchorDay = 9;
      std::snprintf(event.title, sizeof(event.title), "schedule-%zu", index);
      event.lastModified = static_cast<uint32_t>(1000 + index);
    }

    state.taskCount = Pinetime::Controllers::CompanionProtocol::TaskCapacity;
    for (size_t index = 0; index < state.tasks.size(); index++) {
      auto& task = state.tasks[index];
      task = {};
      task.id = static_cast<uint16_t>(index + 1);
      task.order = static_cast<uint8_t>(index);
      std::snprintf(task.title.data(), task.title.size(), "task-%zu", index);
      task.lastModified = static_cast<uint32_t>(2000 + index);
    }
    return state;
  }

  class VectorOutput final : public FamilyStateCodec::Output {
  public:
    bool Write(const uint8_t* data, size_t size) override {
      largestWrite = std::max(largestWrite, size);
      bytes.insert(bytes.end(), data, data + size);
      return true;
    }

    std::vector<uint8_t> bytes;
    size_t largestWrite = 0;
  };

  class ChunkedInput final : public FamilyStateCodec::Input {
  public:
    explicit ChunkedInput(const std::vector<uint8_t>& bytes) : bytes {bytes} {
    }

    bool Read(uint8_t* output, size_t size) override {
      largestRead = std::max(largestRead, size);
      if (size > bytes.size() - position) {
        return false;
      }
      // Model an underlying transport that advances in tiny fragments while
      // still satisfying the codec's exact-read contract.
      for (size_t copied = 0; copied < size; copied++) {
        output[copied] = bytes[position + copied];
      }
      position += size;
      return true;
    }

    size_t largestRead = 0;

  private:
    const std::vector<uint8_t>& bytes;
    size_t position = 0;
  };

  class RejectingOutput final : public FamilyStateCodec::Output {
  public:
    bool Write(const uint8_t*, size_t) override {
      writes++;
      return writes < 2;
    }

  private:
    size_t writes = 0;
  };
}

int main() {
  Check(FamilyStateCodec::PayloadSize == 2122, "fixed payload size");
  Check(FamilyStateCodec::EncodedSize == 2146, "fixed encoded size");
  Check(FamilyStateCodec::StreamChunkSize <= 64, "streaming scratch stays bounded");

  const auto fixture = Fixture();
  FamilyStateCodec::Buffer encoded;
  Check(FamilyStateCodec::Encode(fixture, encoded), "fixture encodes");
  Check(encoded[0] == 'I' && encoded[1] == 'F' && encoded[2] == 'S' && encoded[3] == '3', "magic is IFS3");
  const uint32_t storedCrc = static_cast<uint32_t>(encoded[16]) | (static_cast<uint32_t>(encoded[17]) << 8) |
                             (static_cast<uint32_t>(encoded[18]) << 16) | (static_cast<uint32_t>(encoded[19]) << 24);
  Check(storedCrc == Pinetime::Controllers::Crc32::Compute(encoded.data() + FamilyStateCodec::HeaderSize, FamilyStateCodec::PayloadSize),
        "streaming payload CRC matches canonical bytes");

  VectorOutput streamed;
  Check(FamilyStateCodec::Encode(fixture, streamed), "fixture stream encodes");
  Check(streamed.bytes.size() == encoded.size() && std::equal(streamed.bytes.begin(), streamed.bytes.end(), encoded.begin()),
        "streamed encoding is byte-identical to buffer encoding");
  Check(streamed.largestWrite <= FamilyStateCodec::StreamChunkSize, "encoder emits only bounded chunks");
  RejectingOutput rejectingOutput;
  Check(!FamilyStateCodec::Encode(fixture, rejectingOutput), "stream write failure is reported without a partial success");

  FamilyState decoded;
  const auto result = FamilyStateCodec::Decode(encoded.data(), encoded.size(), decoded);
  Check(static_cast<bool>(result), "fixture decodes");
  Check(decoded.generation == fixture.generation, "generation round trips");
  Check(decoded.settings.stepsGoal == fixture.settings.stepsGoal, "settings round trip");
  Check(decoded.settings.alwaysOnDisplay, "settings flags round trip");
  Check(decoded.scheduleCount == 1 && decoded.scheduleVersion == fixture.scheduleVersion, "schedule metadata round trips");
  Check(std::strcmp(decoded.schedules[0].title, "Quran practice") == 0, "schedule record round trips");
  Check(decoded.taskCount == 1 && decoded.taskVersion == fixture.taskVersion, "task metadata round trips");
  Check(std::strcmp(decoded.tasks[0].title.data(), "Brush teeth") == 0, "task record round trips");
  Check(decoded.taskStreak == 12 && decoded.taskRolloverDate == 20260806, "task stats round trip");
  Check(decoded.alarms[0].enabled && decoded.alarms[0].minute == 15, "alarm round trips");
  Check(decoded.prayer.longitudeE2 == -9527 && decoded.prayer.utcOffsetQuarters == -20, "prayer settings round trip");
  Check(decoded.findMyKeyPresent && decoded.findMyKey[27] == 28, "Find My key round trips");

  ChunkedInput streamedInput {streamed.bytes};
  FamilyState streamedDecoded;
  Check(static_cast<bool>(FamilyStateCodec::Decode(streamedInput, streamed.bytes.size(), streamedDecoded)), "chunked stream decodes");
  Check(streamedInput.largestRead <= FamilyStateCodec::StreamChunkSize, "decoder requests only bounded chunks");
  Check(streamedDecoded.generation == fixture.generation && streamedDecoded.schedules[0].id == fixture.schedules[0].id,
        "streamed decode preserves state");
  auto truncatedBytes = streamed.bytes;
  truncatedBytes.pop_back();
  ChunkedInput truncatedInput {truncatedBytes};
  Check(!static_cast<bool>(FamilyStateCodec::Decode(truncatedInput, streamed.bytes.size(), streamedDecoded)),
        "short stream read is rejected");

  const auto maximum = MaximumFixture();
  VectorOutput maximumBytes;
  Check(FamilyStateCodec::Encode(maximum, maximumBytes), "maximum-capacity state stream encodes");
  ChunkedInput maximumInput {maximumBytes.bytes};
  FamilyState maximumDecoded;
  Check(static_cast<bool>(FamilyStateCodec::Decode(maximumInput, maximumBytes.bytes.size(), maximumDecoded)),
        "maximum-capacity state stream decodes");
  Check(maximumDecoded.scheduleCount == maximum.scheduleCount && maximumDecoded.schedules.back().id == maximum.schedules.back().id &&
          maximumDecoded.taskCount == maximum.taskCount && maximumDecoded.tasks.back().order == maximum.tasks.back().order,
        "maximum-capacity state round trips");

  auto corrupted = encoded;
  corrupted[FamilyStateCodec::HeaderSize + 20] ^= 0x80;
  Check(FamilyStateCodec::Decode(corrupted.data(), corrupted.size(), decoded).error == FamilyStateCodec::DecodeError::Crc,
        "CRC corruption rejected");

  corrupted = encoded;
  corrupted[FamilyStateCodec::HeaderSize + FamilyStateCodec::SettingsSize + 1] = 1;
  Check(FamilyStateCodec::Decode(corrupted.data(), corrupted.size(), decoded).error == FamilyStateCodec::DecodeError::Crc,
        "CRC failure takes precedence over corrupted reserved payload bytes");
  const uint32_t reservedCrc =
    Pinetime::Controllers::Crc32::Compute(corrupted.data() + FamilyStateCodec::HeaderSize, FamilyStateCodec::PayloadSize);
  corrupted[16] = static_cast<uint8_t>(reservedCrc);
  corrupted[17] = static_cast<uint8_t>(reservedCrc >> 8);
  corrupted[18] = static_cast<uint8_t>(reservedCrc >> 16);
  corrupted[19] = static_cast<uint8_t>(reservedCrc >> 24);
  Check(FamilyStateCodec::Decode(corrupted.data(), corrupted.size(), decoded).error == FamilyStateCodec::DecodeError::Reserved,
        "valid CRC still rejects nonzero reserved payload bytes");

  corrupted = encoded;
  corrupted[4]++;
  Check(FamilyStateCodec::Decode(corrupted.data(), corrupted.size(), decoded).error == FamilyStateCodec::DecodeError::Version,
        "future schema rejected");

  auto invalid = fixture;
  invalid.scheduleCount = Pinetime::Controllers::CompanionProtocol::ScheduleCapacity + 1;
  Check(!FamilyStateCodec::Encode(invalid, encoded), "over-capacity schedule rejected");

  invalid = fixture;
  invalid.schedules[1].id = 99;
  Check(!FamilyStateCodec::Encode(invalid, encoded), "nonzero unused schedule slot rejected");

  invalid = fixture;
  invalid.findMyKeyPresent = false;
  Check(!FamilyStateCodec::Encode(invalid, encoded), "hidden Find My key rejected");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
