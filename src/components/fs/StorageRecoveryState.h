#pragma once

#include <cstdint>

namespace Pinetime::Controllers {
  struct StorageRecoveryState {
    enum class Phase : uint8_t {
      Idle = 0,
      Pending = 1,
      Encoding = 2,
      Writing = 3,
      Succeeded = 4,
      Failed = 5,
    };

    static constexpr uint32_t Magic = 0x53544f52; // STOR
    static constexpr uint16_t Version = 1;

    uint32_t magic = 0;
    uint16_t version = 0;
    uint8_t operation = 0;
    Phase phase = Phase::Idle;
    uint8_t error = 0;
    uint8_t reserved = 0;
    uint32_t token = 0;
    uint32_t startedTick = 0;
    uint32_t elapsedMs = 0;
    uint32_t check = 0;

    static constexpr uint32_t Check(uint8_t operation,
                                    Phase phase,
                                    uint8_t error,
                                    uint32_t token,
                                    uint32_t startedTick,
                                    uint32_t elapsedMs) {
      uint32_t value =
        Magic ^ (static_cast<uint32_t>(Version) << 16) ^
        (static_cast<uint32_t>(operation) << 24) ^
        (static_cast<uint32_t>(phase) << 16) ^
        (static_cast<uint32_t>(error) << 8) ^ token ^ startedTick ^ elapsedMs;
      value ^= value << 13;
      value ^= value >> 17;
      value ^= value << 5;
      return value;
    }

    constexpr bool Valid() const {
      return magic == Magic &&
             version == Version &&
             reserved == 0 &&
             static_cast<uint8_t>(phase) <=
               static_cast<uint8_t>(Phase::Failed) &&
             check == Check(operation,
                            phase,
                            error,
                            token,
                            startedTick,
                            elapsedMs);
    }

    constexpr void Record(uint8_t newOperation,
                          Phase newPhase,
                          uint8_t newError,
                          uint32_t newToken,
                          uint32_t newStartedTick,
                          uint32_t newElapsedMs) {
      magic = Magic;
      version = Version;
      operation = newOperation;
      phase = newPhase;
      error = newError;
      reserved = 0;
      token = newToken;
      startedTick = newStartedTick;
      elapsedMs = newElapsedMs;
      check = Check(operation,
                    phase,
                    error,
                    token,
                    startedTick,
                    elapsedMs);
    }

    constexpr void Clear() {
      Record(0, Phase::Idle, 0, 0, 0, 0);
    }
  };
}
