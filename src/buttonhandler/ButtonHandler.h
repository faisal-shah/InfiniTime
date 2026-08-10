#pragma once

#include "buttonhandler/ButtonActions.h"
#include "components/timer/StaticTimer.h"
#include "systemtask/SystemTask.h"

namespace Pinetime {
  namespace Controllers {
    class ButtonHandler {
    public:
      enum class Events : uint8_t { Press, Release, Timer };
      void Init(Pinetime::System::SystemTask* systemTask);
      ButtonActions HandleEvent(Events event);

    private:
      enum class States : uint8_t { Idle, Pressed, Holding, LongHeld };
      bool ArmTimer(TickType_t period);
      TickType_t releaseTime = 0;
      StaticTimer buttonTimer;
      bool gesturesAvailable = false;
      bool buttonPressed = false;
      States state = States::Idle;
    };
  }
}
