#pragma once

#include "displayapp/apps/Apps.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/widgets/Counter.h"
#include "displayapp/Controllers.h"
#include "components/multialarm/MultiAlarmController.h"
#include "Symbols.h"
#include <lvgl/lvgl.h>

namespace Pinetime {
  namespace Controllers {
    class Settings;
  }
  namespace Applications {
    namespace Screens {
      // Multi-alarm app: a list of MaxAlarms rows (enable toggle + time +
      // daily/once), tapping a row's time opens an inline editor. One screen
      // object with an internal List/Edit view toggle, so no second app enum.
      class MultiAlarm : public Screen {
      public:
        MultiAlarm(Controllers::MultiAlarmController& multiAlarmController, Controllers::Settings& settingsController);
        ~MultiAlarm() override;

        void OnRowEvent(lv_obj_t* obj, lv_event_t event);
        void OnEditorEvent(lv_obj_t* obj, lv_event_t event);
        void RefreshSave();
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        void ShowList();
        void ShowEditor(uint8_t index);
        void SaveEditor();
        void BeginSave(bool accepted);
        void ShowSaving();
        void ShowSaveFailed();
        void SetRowText(uint8_t i, const Controllers::MultiAlarmController::Alarm& alarm);
        void UpdateEditorAmPm();

        Controllers::MultiAlarmController& multiAlarmController;
        Controllers::Settings& settingsController;

        enum class View { List, Edit, Saving, SaveFailed } view = View::List;
        uint8_t editingIndex = 0;

        lv_obj_t* listContainer = nullptr;
        lv_obj_t* rowTime[Controllers::MultiAlarmController::MaxAlarms] = {};
        lv_obj_t* rowSwitch[Controllers::MultiAlarmController::MaxAlarms] = {};
        lv_obj_t* rowMode[Controllers::MultiAlarmController::MaxAlarms] = {};

        lv_obj_t* editContainer = nullptr;
        lv_obj_t* btnMode = nullptr;
        lv_obj_t* txtMode = nullptr;
        lv_obj_t* btnSave = nullptr;
        lv_obj_t* btnAmPm = nullptr; // 12h mode only
        lv_obj_t* lblAmPm = nullptr;
        lv_task_t* saveTask = nullptr;
        uint32_t completionAtSave = 0;
        Controllers::MultiAlarmController::Mode editMode = Controllers::MultiAlarmController::Mode::Daily;
        Widgets::Counter hourCounter = Widgets::Counter(0, 23, jetbrains_mono_42);
        Widgets::Counter minuteCounter = Widgets::Counter(0, 59, jetbrains_mono_42);
      };
    }

    template <>
    struct AppTraits<Apps::MultiAlarm> {
      static constexpr Apps app = Apps::MultiAlarm;
      static constexpr const char* icon = Screens::Symbols::bell;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::MultiAlarm(controllers.multiAlarmController, controllers.settingsController);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
