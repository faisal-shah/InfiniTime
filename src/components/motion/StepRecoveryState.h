#pragma once

#include <cstdint>
#include <optional>

namespace Pinetime::Controllers {
  struct StepRecoveryState {
    static constexpr uint32_t Magic = 0x53544550; // STEP
    static constexpr uint16_t Version = 1;

    uint32_t magic = Magic;
    uint16_t version = Version;
    uint16_t reserved = 0;
    uint32_t dayKey = 0;
    uint32_t total = 0;
    uint32_t check = 0;

    static constexpr uint32_t Check(uint32_t dayKey, uint32_t total) {
      uint32_t value = Magic ^ (static_cast<uint32_t>(Version) << 16) ^ dayKey ^ total;
      value ^= value << 13;
      value ^= value >> 17;
      value ^= value << 5;
      return value;
    }

    static constexpr StepRecoveryState Capture(uint32_t dayKey, uint32_t total) {
      return {
        Magic,
        Version,
        0,
        dayKey,
        total,
        Check(dayKey, total),
      };
    }

    constexpr bool Valid() const {
      return magic == Magic &&
             version == Version &&
             reserved == 0 &&
             dayKey != 0 &&
             check == Check(dayKey, total);
    }

    constexpr std::optional<uint32_t> Restore(uint32_t currentDayKey) const {
      return Valid() && dayKey == currentDayKey
        ? std::optional<uint32_t> {total}
        : std::nullopt;
    }

    constexpr void Clear(uint32_t currentDayKey) {
      *this = Capture(currentDayKey, 0);
    }
  };
}
