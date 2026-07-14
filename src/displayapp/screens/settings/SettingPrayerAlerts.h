#pragma once

#include "displayapp/screens/Screen.h"
#include "displayapp/screens/CheckboxList.h"

namespace Pinetime {
  namespace Controllers {
    class PrayerController;
  }

  namespace Applications {
    namespace Screens {
      class SettingPrayerAlerts : public Screen {
      public:
        explicit SettingPrayerAlerts(Pinetime::Controllers::PrayerController& prayerController);
        ~SettingPrayerAlerts() override;

      private:
        CheckboxList checkboxList;
      };
    }
  }
}
