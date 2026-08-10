#pragma once

#include <cstdint>

namespace Pinetime::Applications {
  class HeartRateTask;
}

namespace Pinetime::Controllers {
  class HeartRateController {
  public:
    enum class States : uint8_t { Stopped, NotEnoughData, NoTouch, Running };

    void SetHeartRateTask(Applications::HeartRateTask* value) {
      task = value;
    }

    void Update(States, uint8_t) {
    }

    Applications::HeartRateTask* task = nullptr;
  };
}
