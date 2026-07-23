#pragma once

#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/Symbols.h"
#include "displayapp/widgets/PageIndicator.h"
#include "components/task/TaskController.h"
#include <array>
#include <lvgl/lvgl.h>

namespace Pinetime {
  namespace Applications {
    namespace Screens {
      // The daily task checklist. Two views on one screen:
      //   Summary  — a progress arc, "done / total", and the streak. Tap it to
      //   List     — a paged set of check boxes; tap a row to tick/untick today.
      // Definitions are phone-managed (read-only here); only the ticks are set
      // on the watch, straight through the TaskController.
      class Tasks : public Screen {
      public:
        Tasks(DisplayApp* app, Controllers::TaskController& taskController);
        ~Tasks() override;

        bool OnTouchEvent(TouchEvents event) override;

        // LVGL row callback (dispatched from a free function via user_data).
        void OnRowToggled(lv_obj_t* checkbox);

      private:
        static constexpr uint8_t rowsPerPage = 4;

        static uint8_t PageCount(uint8_t count) {
          return count == 0 ? 1 : (count + rowsPerPage - 1) / rowsPerPage;
        }

        void ShowSummary();
        void ShowList();
        void RenderPage();

        DisplayApp* app;
        Controllers::TaskController& taskController;

        enum class Mode : uint8_t { Summary, List };
        Mode mode = Mode::Summary;
        uint8_t page = 0;

        // List-mode widgets (one reusable row of check boxes per page).
        std::array<lv_obj_t*, rowsPerPage> checkboxes {};
        Widgets::PageIndicator pageIndicator {0, 1};
      };
    }

    template <>
    struct AppTraits<Apps::Tasks> {
      static constexpr Apps app = Apps::Tasks;
      static constexpr const char* icon = Screens::Symbols::check;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::Tasks(controllers.displayApp, controllers.tasksController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
