#include "buttonhandler/ButtonHandler.h"

#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

void ButtonTimerCallback(TimerHandle_t xTimer) {
  auto* sysTask = static_cast<Pinetime::System::SystemTask*>(pvTimerGetTimerID(xTimer));
  (void) sysTask->TryPushMessage(Pinetime::System::Messages::HandleButtonTimerEvent);
}

void ButtonHandler::Init(Pinetime::System::SystemTask* systemTask) {
  gesturesAvailable = buttonTimer.Create("buttonTimer", pdMS_TO_TICKS(200), pdFALSE, systemTask, ButtonTimerCallback);
  if (!gesturesAvailable) {
    NRF_LOG_ERROR("[button] timer unavailable; using single-click fallback");
  }
}

bool ButtonHandler::ArmTimer(TickType_t period) {
  if (!gesturesAvailable || !buttonTimer.ChangePeriod(period)) {
    gesturesAvailable = false;
    NRF_LOG_WARNING("[button] timer command failed");
    return false;
  }
  return true;
}

ButtonActions ButtonHandler::HandleEvent(Events event) {
  static constexpr TickType_t doubleClickTime = pdMS_TO_TICKS(200);
  static constexpr TickType_t longPressTime = pdMS_TO_TICKS(400);
  static constexpr TickType_t longerPressTime = pdMS_TO_TICKS(2000);

  if (event == Events::Press) {
    buttonPressed = true;
  } else if (event == Events::Release) {
    releaseTime = xTaskGetTickCount();
    buttonPressed = false;
  }

  // A missing timer must not disable the physical button.  Double-click and
  // hold gestures are optional; release still provides a normal click.
  if (!gesturesAvailable) {
    return event == Events::Release ? ButtonActions::Click : ButtonActions::None;
  }

  switch (state) {
    case States::Idle:
      if (event == Events::Press) {
        if (ArmTimer(doubleClickTime)) {
          state = States::Pressed;
        }
      }
      break;
    case States::Pressed:
      if (event == Events::Press) {
        if (xTaskGetTickCount() - releaseTime < doubleClickTime) {
          (void) buttonTimer.Stop();
          state = States::Idle;
          return ButtonActions::DoubleClick;
        }
      } else if (event == Events::Release) {
        if (!ArmTimer(doubleClickTime)) {
          state = States::Idle;
          return ButtonActions::Click;
        }
      } else if (event == Events::Timer) {
        if (buttonPressed) {
          if (ArmTimer(longPressTime - doubleClickTime)) {
            state = States::Holding;
          } else {
            state = States::Idle;
          }
        } else {
          state = States::Idle;
          return ButtonActions::Click;
        }
      }
      break;
    case States::Holding:
      if (event == Events::Release) {
        (void) buttonTimer.Stop();
        state = States::Idle;
        return ButtonActions::Click;
      } else if (event == Events::Timer) {
        if (ArmTimer(longerPressTime - longPressTime - doubleClickTime)) {
          state = States::LongHeld;
          return ButtonActions::LongPress;
        }
        state = States::Idle;
      }
      break;
    case States::LongHeld:
      if (event == Events::Release) {
        (void) buttonTimer.Stop();
        state = States::Idle;
      } else if (event == Events::Timer) {
        state = States::Idle;
        return ButtonActions::LongerPress;
      }
      break;
  }
  return ButtonActions::None;
}
