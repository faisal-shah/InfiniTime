#include "components/fs/FamilyStateCodec.h"

#include <cstdio>
#include <cstring>

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
}

int main() {
  Check(FamilyStateCodec::PayloadSize == 2122, "fixed payload size");
  Check(FamilyStateCodec::EncodedSize == 2146, "fixed encoded size");

  const auto fixture = Fixture();
  FamilyStateCodec::Buffer encoded;
  Check(FamilyStateCodec::Encode(fixture, encoded), "fixture encodes");
  Check(encoded[0] == 'I' && encoded[1] == 'F' && encoded[2] == 'S' && encoded[3] == '3',
        "magic is IFS3");

  FamilyState decoded;
  const auto result = FamilyStateCodec::Decode(encoded.data(), encoded.size(), decoded);
  Check(static_cast<bool>(result), "fixture decodes");
  Check(decoded.generation == fixture.generation, "generation round trips");
  Check(decoded.settings.stepsGoal == fixture.settings.stepsGoal, "settings round trip");
  Check(decoded.settings.alwaysOnDisplay, "settings flags round trip");
  Check(decoded.scheduleCount == 1 && decoded.scheduleVersion == fixture.scheduleVersion,
        "schedule metadata round trips");
  Check(std::strcmp(decoded.schedules[0].title, "Quran practice") == 0,
        "schedule record round trips");
  Check(decoded.taskCount == 1 && decoded.taskVersion == fixture.taskVersion,
        "task metadata round trips");
  Check(std::strcmp(decoded.tasks[0].title.data(), "Brush teeth") == 0,
        "task record round trips");
  Check(decoded.taskStreak == 12 && decoded.taskRolloverDate == 20260806,
        "task stats round trip");
  Check(decoded.alarms[0].enabled && decoded.alarms[0].minute == 15,
        "alarm round trips");
  Check(decoded.prayer.longitudeE2 == -9527 && decoded.prayer.utcOffsetQuarters == -20,
        "prayer settings round trip");
  Check(decoded.findMyKeyPresent && decoded.findMyKey[27] == 28,
        "Find My key round trips");

  auto corrupted = encoded;
  corrupted[FamilyStateCodec::HeaderSize + 20] ^= 0x80;
  Check(FamilyStateCodec::Decode(corrupted.data(), corrupted.size(), decoded).error ==
          FamilyStateCodec::DecodeError::Crc,
        "CRC corruption rejected");

  corrupted = encoded;
  corrupted[4]++;
  Check(FamilyStateCodec::Decode(corrupted.data(), corrupted.size(), decoded).error ==
          FamilyStateCodec::DecodeError::Version,
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
