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
      // Calculation-method picker: five methods over two CheckboxList pages.
      class SettingPrayerMethod : public Screen {
      public:
        SettingPrayerMethod(DisplayApp* app, Pinetime::Controllers::PrayerController& prayerController);
        ~SettingPrayerMethod() override;

        bool OnTouchEvent(TouchEvents event) override;

      private:
        Controllers::PrayerController& prayerController;

        static constexpr int optionsPerScreen = 4;
        static constexpr int nScreens = 2;

        auto CreateScreenList() const;
        std::unique_ptr<Screen> CreateScreen(unsigned int screenNum) const;

        ScreenList<nScreens> screens;
      };
    }
  }
}
