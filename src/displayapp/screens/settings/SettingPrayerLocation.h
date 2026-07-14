#pragma once

#include "displayapp/screens/Screen.h"
#include "displayapp/screens/ScreenList.h"

namespace Pinetime {
  namespace Controllers {
    class PrayerController;
  }

  namespace Applications {
    class DisplayApp;

    namespace Screens {
      // Manual location entry for prayer times: three self-committing pages
      // (latitude, longitude, UTC offset), so the watch is configurable with
      // no phone. Coordinates are entered in degree hundredths, the exact
      // wire/persist resolution, so nothing is lost against app-written
      // values.
      class SettingPrayerLocation : public Screen {
      public:
        SettingPrayerLocation(DisplayApp* app, Pinetime::Controllers::PrayerController& prayerController);
        ~SettingPrayerLocation() override;

        bool OnTouchEvent(TouchEvents event) override;

      private:
        Controllers::PrayerController& prayerController;

        static constexpr int nScreens = 3;
        auto CreateScreenList();
        ScreenList<nScreens> screens;
      };
    }
  }
}
