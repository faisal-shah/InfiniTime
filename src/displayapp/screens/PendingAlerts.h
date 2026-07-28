#pragma once

#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "systemtask/WakeLock.h"
#include "components/alertqueue/AlertQueue.h"
#include <lvgl/lvgl.h>

namespace Pinetime {
  namespace Controllers {
    class ScheduleController;
    class MotorController;
    class Settings;
  }

  namespace Applications {
    namespace Screens {
      // Unified screen for every watch-originated alert (multi-alarm, schedule
      // reminder, prayer alert). Renders the pending-alerts queue newest-first:
      // swipe left/right cycles without acknowledging; OK button or the
      // physical button acknowledges the shown entry and advances; empty queue
      // returns to the clock. Text is pulled from the owning controller at
      // render time (the queue stores no strings).
      class PendingAlerts : public Screen {
      public:
        PendingAlerts(DisplayApp* app,
                      Controllers::AlertQueue& alertQueue,
                      Controllers::ScheduleController& scheduleController,
                      System::SystemTask& systemTask,
                      Controllers::MotorController& motorController,
                      Controllers::Settings& settingsController);
        ~PendingAlerts() override;

        // A new firing arrived while this screen is up: jump to newest and
        // ring with the newest entry's profile.
        void OnNewFiring();

        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;
        void StopRingingOnly();
        void AcknowledgeCurrent();

      private:
        void Render();
        void Ring(uint16_t seconds);

        DisplayApp* app;
        Controllers::AlertQueue& alertQueue;
        Controllers::ScheduleController& scheduleController;
        System::WakeLock wakeLock;
        Controllers::MotorController& motorController;
        Controllers::Settings& settingsController;

        uint8_t index = 0; // 0 = newest

        lv_obj_t* sourceLabel = nullptr;
        lv_obj_t* timeLabel = nullptr;
        lv_obj_t* titleLabel = nullptr;
        lv_obj_t* positionLabel = nullptr;
        lv_obj_t* btnOk = nullptr;
        lv_task_t* taskStopRinging = nullptr;
      };
    }

    template <>
    struct AppTraits<Apps::PendingAlerts> {
      static constexpr Apps app = Apps::PendingAlerts;
      static constexpr const char* icon = nullptr;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::PendingAlerts(controllers.displayApp,
                                          controllers.alertQueue,
                                          controllers.scheduleController,
                                          *controllers.systemTask,
                                          controllers.motorController,
                                          controllers.settingsController);
      };
    };
  }
}
