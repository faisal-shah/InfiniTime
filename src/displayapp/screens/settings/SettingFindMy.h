#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>

#include "displayapp/screens/Screen.h"
#include "displayapp/screens/CheckboxList.h"

namespace Pinetime {
  namespace Controllers {
    class BeaconController;
  }

  namespace Applications {
    class DisplayApp;

    namespace Screens {
      // Find My beacon toggle. When ON the watch broadcasts a FindMy beacon and
      // becomes non-connectable, so this on-watch toggle is the only way to turn
      // it back OFF. The ON option is disabled until a key has been provisioned
      // from the companion.
      class SettingFindMy : public Screen {
      public:
        SettingFindMy(DisplayApp* app, Pinetime::Controllers::BeaconController& beaconController);
        ~SettingFindMy() override;

      private:
        DisplayApp* app;
        Pinetime::Controllers::BeaconController& beaconController;
        CheckboxList checkboxList;
      };
    }
  }
}
