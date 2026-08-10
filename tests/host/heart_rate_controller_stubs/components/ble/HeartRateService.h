#pragma once

#include <cstdint>

namespace Pinetime::Controllers {
  class HeartRateService {
  public:
    void OnNewHeartRateValue(uint8_t value) {
      notifications++;
      lastValue = value;
    }

    int notifications = 0;
    uint8_t lastValue = 0;
  };
}
