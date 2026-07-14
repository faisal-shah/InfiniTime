#pragma once

#include <array>
#include <memory>
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/List.h"

namespace Pinetime {
  namespace Applications {
    class DisplayApp;

    namespace Screens {
      // Settings -> Prayer: the four prayer sub-settings. Everything is
      // watch-editable so prayers work with no phone (doc/PrayerService.md).
      class SettingPrayer : public Screen {
      public:
        SettingPrayer(DisplayApp* app, Pinetime::Controllers::Settings& settingsController);
        ~SettingPrayer() override;

      private:
        std::unique_ptr<List> entriesList;
      };
    }
  }
}
