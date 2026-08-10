#pragma once

#include <cstdint>

namespace Pinetime::Controllers {
  class Ppg {
  public:
    static constexpr uint32_t deltaTms = 100;

    void Reset(bool) {
    }

    int8_t Preprocess(uint16_t, uint16_t) {
      return 0;
    }

    int HeartRate() {
      return -2;
    }
  };
}
