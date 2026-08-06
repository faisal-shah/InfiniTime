#pragma once

#include <displayapp/screens/BatteryIcon.h>
#include <lvgl/src/lv_core/lv_obj.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <displayapp/Controllers.h>
#include "displayapp/screens/Screen.h"
#include "components/datetime/DateTimeController.h"
#include "components/ble/BleController.h"
#include "utility/DirtyValue.h"
#include "displayapp/apps/Apps.h"

namespace Pinetime {
  namespace Controllers {
    class Settings;
    class Battery;
    class Ble;
    class NotificationManager;
    class HeartRateController;
    class MotionController;
    class PrayerController;
    class TaskController;
  }

  namespace Applications {
    namespace Screens {

      class WatchFaceCasioStyleG7710 : public Screen {
      public:
        WatchFaceCasioStyleG7710(Controllers::DateTime& dateTimeController,
                                 const Controllers::Battery& batteryController,
                                 const Controllers::Ble& bleController,
                                 Controllers::NotificationManager& notificatioManager,
                                 Controllers::Settings& settingsController,
                                 Controllers::HeartRateController& heartRateController,
                                 Controllers::MotionController& motionController,
                                 Controllers::PrayerController& prayerController,
                                 Controllers::TaskController& taskController);
        ~WatchFaceCasioStyleG7710() override;

        void Refresh() override;

        static bool IsAvailable(Pinetime::System::StorageTask& storageTask);

      private:
        // Both are on the once-a-minute path and both are RAM-only: the
        // display task keeps rendering in always-on mode with the flash down.
        void RefreshPrayer();
        void RefreshTasks();

        Utility::DirtyValue<uint8_t> batteryPercentRemaining {};
        Utility::DirtyValue<bool> powerPresent {};
        Utility::DirtyValue<bool> bleState {};
        Utility::DirtyValue<bool> bleRadioEnabled {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::minutes>> currentDateTime {};
        Utility::DirtyValue<uint32_t> stepCount {};
        Utility::DirtyValue<uint8_t> heartbeat {};
        Utility::DirtyValue<bool> heartbeatRunning {};
        Utility::DirtyValue<uint8_t> notificationCount {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::days>> currentDate;

        lv_point_t line_icons_points[3] {{0, 5}, {117, 5}, {122, 0}};
        lv_point_t line_day_of_week_number_points[4] {{0, 0}, {100, 0}, {95, 95}, {0, 95}};
        lv_point_t line_prayer_window_points[3] {{0, 5}, {130, 5}, {135, 0}};
        lv_point_t line_prayer_next_points[3] {{0, 5}, {135, 5}, {140, 0}};
        lv_point_t line_time_points[3] {{0, 0}, {230, 0}, {235, 5}};

        lv_color_t color_text = lv_color_hex(0x98B69A);

        lv_style_t style_line;
        lv_style_t style_border;

        lv_obj_t* label_time;
        lv_obj_t* line_time;
        lv_obj_t* label_time_ampm;
        // Top-left box: numeric date over the weekday.
        lv_obj_t* label_date;
        lv_obj_t* label_day_of_week;
        lv_obj_t* line_day_of_week_number;
        // Top-right rows: the prayer window we are in, and when the next starts.
        lv_obj_t* label_prayer_window;
        lv_obj_t* line_prayer_window;
        lv_obj_t* label_prayer_next;
        lv_obj_t* label_prayer_next_ampm;
        lv_obj_t* prayerIcon;
        lv_obj_t* line_prayer_next;
        // Bottom centre, between heart rate and steps.
        lv_obj_t* label_tasks;
        lv_obj_t* tasksIcon;
        lv_obj_t* backgroundLabel;
        lv_obj_t* bleIcon;
        lv_obj_t* batteryPlug;
        lv_obj_t* label_battery_value;
        lv_obj_t* heartbeatIcon;
        lv_obj_t* heartbeatValue;
        lv_obj_t* stepIcon;
        lv_obj_t* stepValue;
        lv_obj_t* notificationIcon;
        lv_obj_t* line_icons;

        BatteryIcon batteryIcon;

        Controllers::DateTime& dateTimeController;
        const Controllers::Battery& batteryController;
        const Controllers::Ble& bleController;
        Controllers::NotificationManager& notificatioManager;
        Controllers::Settings& settingsController;
        Controllers::HeartRateController& heartRateController;
        Controllers::MotionController& motionController;
        Controllers::PrayerController& prayerController;
        Controllers::TaskController& taskController;

        lv_task_t* taskRefresh;
        lv_font_t* font_dot40 = nullptr;
        lv_font_t* font_dot30 = nullptr;
        lv_font_t* font_segment40 = nullptr;
        lv_font_t* font_segment115 = nullptr;
      };
    }

    template <>
    struct WatchFaceTraits<WatchFace::CasioStyleG7710> {
      static constexpr WatchFace watchFace = WatchFace::CasioStyleG7710;
      static constexpr const char* name = "Casio G7710";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFaceCasioStyleG7710(controllers.dateTimeController,
                                                     controllers.batteryController,
                                                     controllers.bleController,
                                                     controllers.notificationManager,
                                                     controllers.settingsController,
                                                     controllers.heartRateController,
                                                     controllers.motionController,
                                                     controllers.prayerController,
                                                     controllers.tasksController);
      };

      static bool IsAvailable(AppControllers& controllers) {
        return Screens::WatchFaceCasioStyleG7710::IsAvailable(
          controllers.storageTask);
      }
    };
  }
}
