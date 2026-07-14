#pragma once

#include "displayapp/screens/Screen.h"
#include "displayapp/screens/CheckboxList.h"

namespace Pinetime {
  namespace Controllers {
    class PrayerController;
  }

  namespace Applications {
    namespace Screens {
      class SettingPrayerAsr : public Screen {
      public:
        explicit SettingPrayerAsr(Pinetime::Controllers::PrayerController& prayerController);
        ~SettingPrayerAsr() override;

      private:
        CheckboxList checkboxList;
      };
    }
  }
}
