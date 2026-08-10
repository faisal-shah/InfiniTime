#pragma once

#include <cstdint>

namespace Pinetime::Drivers {
  class Hrs3300 {
  public:
    struct Sample {
      bool isValid = false;
      uint16_t hrs = 0;
      uint16_t als = 0;
    };

    void Enable() {
    }

    void Disable() {
    }

    Sample ReadHrsAls() {
      return {};
    }
  };
}
