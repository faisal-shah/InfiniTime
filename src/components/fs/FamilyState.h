#pragma once

#include "components/ble/generated/CompanionProtocol.h"
#include "components/schedule/ScheduleRules.h"

#include <array>
#include <cstdint>

namespace Pinetime::Controllers {
  struct FamilyState {
    struct Settings {
      uint32_t stepsGoal = 10000;
      uint32_t screenTimeoutMs = 15000;
      int32_t infineatColorIndex = 0;
      uint16_t shakeWakeThreshold = 150;
      uint16_t heartRateBackgroundPeriod = 0xffff;
      uint8_t watchFace = 0;
      uint8_t clockType = 0;
      uint8_t weatherFormat = 0;
      uint8_t notificationStatus = 0;
      uint8_t chimeOption = 0;
      uint8_t brightness = 3;
      uint8_t wakeModes = 0;
      uint8_t ptsColorTime = 11;
      uint8_t ptsColorBar = 11;
      uint8_t ptsColorBackground = 3;
      uint8_t ptsGaugeStyle = 0;
      uint8_t ptsWeather = 1;
      uint8_t prideFlag = 0;
      bool alwaysOnDisplay = false;
      bool infineatShowSideCover = true;
#if defined(INFINISIM_ENABLE_BLE_TEST_CONTROL)
      bool dfuAndFsEnabledOnBoot = true;
#else
      bool dfuAndFsEnabledOnBoot = false;
#endif
    };

    struct Task {
      uint16_t id = 0;
      uint8_t order = 0;
      std::array<char, 24> title {};
      uint32_t lastModified = 0;
    };

    struct Alarm {
      uint8_t hour = 0;
      uint8_t minute = 0;
      uint8_t mode = 0;
      bool enabled = false;
    };

    struct PrayerSettings {
      uint8_t version = CompanionProtocol::PrayerSettingsProtocolVersion;
      uint8_t method = 0;
      uint8_t asrMadhab = 0;
      uint8_t flags = 0;
      int16_t latitudeE2 = 0;
      int16_t longitudeE2 = 0;
      int8_t utcOffsetQuarters = 0;
    };

    uint32_t generation = 0;
    Settings settings {};

    uint8_t scheduleCount = 0;
    uint32_t scheduleVersion = 0;
    std::array<ScheduleRules::Event, CompanionProtocol::ScheduleCapacity> schedules {};

    uint8_t taskCount = 0;
    uint32_t taskVersion = 0;
    std::array<Task, CompanionProtocol::TaskCapacity> tasks {};
    uint16_t taskStreak = 0;
    uint32_t taskRolloverDate = 0;

    uint32_t alarmVersion = 0;
    std::array<Alarm, 5> alarms {};

    PrayerSettings prayer {};

    bool findMyKeyPresent = false;
    std::array<uint8_t, 28> findMyKey {};
  };
}
