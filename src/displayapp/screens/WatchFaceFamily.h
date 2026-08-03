#pragma once

#include <lvgl/lvgl.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/BatteryIcon.h"
#include "components/datetime/DateTimeController.h"
#include "components/ble/BleController.h"
#include "components/ble/SimpleWeatherService.h"
#include <optional>
#include "utility/DirtyValue.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "displayapp/screens/BatteryIcon.h"

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
    class MultiAlarmController;
    class SimpleWeatherService;
  }

  namespace Applications {
    namespace Screens {

      class WatchFaceFamily : public Screen {
      public:
        WatchFaceFamily(Controllers::DateTime& dateTimeController,
                        const Controllers::Battery& batteryController,
                        const Controllers::Ble& bleController,
                        const Controllers::MultiAlarmController& multiAlarmController,
                        Controllers::NotificationManager& notificationManager,
                        Controllers::Settings& settingsController,
                        Controllers::HeartRateController& heartRateController,
                        Controllers::MotionController& motionController,
                        Controllers::PrayerController& prayerController,
                        Controllers::TaskController& taskController,
                        Controllers::SimpleWeatherService& weatherService);
        ~WatchFaceFamily() override;

        void Refresh() override;

      private:
        uint8_t displayedHour = -1;
        uint8_t displayedMinute = -1;

        Utility::DirtyValue<int> heartbeat {};
        Utility::DirtyValue<bool> heartbeatRunning {};
        Utility::DirtyValue<uint32_t> stepCount {};
        Utility::DirtyValue<size_t> notificationCount {};
        Utility::DirtyValue<uint8_t> taskTotal {};
        Utility::DirtyValue<uint8_t> taskDone {};
        Utility::DirtyValue<uint8_t> batteryPercent {};
        Utility::DirtyValue<bool> powerPresent {};
        Utility::DirtyValue<bool> bleConnected {};
        Utility::DirtyValue<bool> alarmEnabled {};
        Utility::DirtyValue<std::optional<Pinetime::Controllers::SimpleWeatherService::CurrentWeather>> currentWeather {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::minutes>> currentDateTime {};

        lv_obj_t* label_time;
        lv_obj_t* label_time_ampm;
        lv_obj_t* label_date;

        lv_obj_t* notificationIcon;
        lv_obj_t* label_notification;

        lv_obj_t* weatherIcon;
        lv_obj_t* temperature;

        lv_obj_t* prayerIcon;
        lv_obj_t* label_prayer_window;
        lv_obj_t* prayerTimeIcon;
        lv_obj_t* label_prayer_next;
        lv_obj_t* label_prayer_next_ampm;

        lv_obj_t* tasksIcon;
        lv_obj_t* label_tasks;

        lv_obj_t* heartbeatIcon;
        lv_obj_t* heartbeatValue;
        lv_obj_t* stepIcon;
        lv_obj_t* stepValue;

        lv_obj_t* banner;

        Controllers::DateTime& dateTimeController;
        Controllers::NotificationManager& notificationManager;
        Controllers::Settings& settingsController;
        Controllers::HeartRateController& heartRateController;
        Controllers::MotionController& motionController;
        Controllers::PrayerController& prayerController;
        Controllers::TaskController& taskController;
        Controllers::SimpleWeatherService& weatherService;

        // Face-local status row rather than the shared StatusIcons widget,
        // because that one has no numeric percentage and adding one there
        // would change every other watch face.
        lv_obj_t* statusRow;
        lv_obj_t* bleIcon;
        lv_obj_t* plugIcon;
        lv_obj_t* alarmIcon;
        lv_obj_t* label_battery;
        BatteryIcon batteryIcon;

        const Controllers::Battery& batteryController;
        const Controllers::Ble& bleController;
        const Controllers::MultiAlarmController& multiAlarmController;

        lv_task_t* taskRefresh;

        // Each returns true when it changed something whose width the status
        // band layout depends on.
        bool RefreshTime();
        void RefreshPrayer();
        bool RefreshTasks();
        bool RefreshWeather();
        void FitDateRow();
        void FitStatusBand();
        void RefreshStatus();
        bool RefreshNotifications();
      };
    }

    template <>
    struct WatchFaceTraits<WatchFace::Family> {
      static constexpr WatchFace watchFace = WatchFace::Family;
      static constexpr const char* name = "Family";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFaceFamily(controllers.dateTimeController,
                                            controllers.batteryController,
                                            controllers.bleController,
                                            controllers.multiAlarmController,
                                            controllers.notificationManager,
                                            controllers.settingsController,
                                            controllers.heartRateController,
                                            controllers.motionController,
                                            controllers.prayerController,
                                            controllers.tasksController,
                                            *controllers.weatherController);
      };

      // No external resources: every glyph this face uses is compiled into
      // internal flash, so unlike CasioStyleG7710 it cannot be unavailable.
      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
