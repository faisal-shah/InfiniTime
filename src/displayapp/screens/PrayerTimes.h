#pragma once

#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "components/prayer/PrayerRules.h"
#include <lvgl/lvgl.h>
#include <array>

namespace Pinetime {
  namespace Controllers {
    class PrayerController;
    class DateTime;
    class Settings;
  }

  namespace Applications {
    namespace Screens {
      // Launcher app: today's six prayer times (sunrise included, display
      // only), the next upcoming one highlighted. Read-only; settings live in
      // Settings -> Prayer and in the companion app.
      class PrayerTimes : public Screen {
      public:
        PrayerTimes(Controllers::PrayerController& prayerController,
                    Controllers::DateTime& dateTimeController,
                    Controllers::Settings& settingsController);
        ~PrayerTimes() override;

        void Refresh() override;

      private:
        Controllers::PrayerController& prayerController;
        Controllers::DateTime& dateTimeController;
        Controllers::Settings& settingsController;

        std::array<lv_obj_t*, Controllers::PrayerRules::Prayer::Count> nameLabels {};
        std::array<lv_obj_t*, Controllers::PrayerRules::Prayer::Count> timeLabels {};
        lv_task_t* taskRefresh = nullptr;

        void Render();
      };
    }

    template <>
    struct AppTraits<Apps::Prayer> {
      static constexpr Apps app = Apps::Prayer;
      static constexpr const char* icon = Screens::Symbols::moon;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::PrayerTimes(controllers.prayerController, controllers.dateTimeController, controllers.settingsController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
