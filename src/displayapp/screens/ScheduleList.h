#pragma once

#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/widgets/PageIndicator.h"
#include "components/schedule/ScheduleController.h"
#include <array>
#include <lvgl/lvgl.h>

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      // Read-only paged view of the upcoming schedule occurrences (14 days).
      class ScheduleList : public Screen {
      public:
        ScheduleList(DisplayApp* app,
                     Controllers::ScheduleController& scheduleController,
                     Controllers::Settings& settingsController);
        ~ScheduleList() override;

        bool OnTouchEvent(TouchEvents event) override;

      private:
        static constexpr uint8_t rowsPerPage = 4;
        static constexpr uint8_t maxOccurrences = 20;

        static uint8_t PageCount(uint8_t occurrences) {
          return occurrences == 0 ? 1 : (occurrences + rowsPerPage - 1) / rowsPerPage;
        }

        void RenderPage();

        DisplayApp* app;
        Controllers::ScheduleController& scheduleController;
        Controllers::Settings& settingsController;

        std::array<Controllers::ScheduleController::Occurrence, maxOccurrences> occurrences;
        uint8_t occurrenceCount;
        uint8_t page = 0;

        std::array<lv_obj_t*, rowsPerPage> rowLabels {};
        lv_obj_t* emptyLabel = nullptr;
        Widgets::PageIndicator pageIndicator;
      };
    }

    template <>
    struct AppTraits<Apps::Schedule> {
      static constexpr Apps app = Apps::Schedule;
      static constexpr const char* icon = Screens::Symbols::calendar;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::ScheduleList(controllers.displayApp, controllers.scheduleController, controllers.settingsController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
