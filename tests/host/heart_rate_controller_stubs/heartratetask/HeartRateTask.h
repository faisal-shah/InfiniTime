#pragma once

#include <cstdint>

namespace Pinetime::Applications {
  class HeartRateTask {
  public:
    enum class Messages : uint8_t { GoToSleep, WakeUp, Enable, Disable };

    bool PushMessage(Messages value) {
      lastMessage = value;
      return sendResult;
    }

    bool sendResult = true;
    Messages lastMessage = Messages::GoToSleep;
  };
}
