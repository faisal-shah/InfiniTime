#pragma once
#include <cstdint>

namespace Pinetime {
  namespace Applications {
    namespace Display {
      enum class Messages : uint8_t {
        GoToSleep,
        GoToAOD,
        GoToRunning,
        UpdateBleConnection,
        TouchEvent,
        ButtonPushed,
        ButtonLongPressed,
        ButtonLongerPressed,
        ButtonDoubleClicked,
        NewNotification,
        TimerDone,
        BleFirmwareUpdateStarted,
        // Resets the screen timeout timer when awake
        // Does nothing when asleep
        NotifyDeviceActivity,
        ShowPairingKey,
        PendingAlertsTriggered,
        Chime,
        BleRadioEnableToggle,
        BeaconModeEnable,
        BeaconModeDisable,
        BondForgetAllRequested,
        // The dependency-free boot frame is drawn before persisted settings
        // are loaded. Rebuild the clock once those settings are available.
        ReloadClock,
      };
    }
  }
}
