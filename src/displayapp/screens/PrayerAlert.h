#pragma once

#include "displayapp/screens/Screen.h"
#include "systemtask/WakeLock.h"
#include <lvgl/lvgl.h>

namespace Pinetime {
  namespace Controllers {
    class PrayerController;
    class MotorController;
  }

  namespace System {
    class SystemTask;
  }

  namespace Applications {
    class DisplayApp;

    namespace Screens {
      // Full-screen alert shown at prayer time. Not a launcher app: DisplayApp
      // pushes it on Display::Messages::PrayerAlertTriggered.
      class PrayerAlert : public Screen {
      public:
        PrayerAlert(DisplayApp* app,
                    Controllers::PrayerController& prayerController,
                    System::SystemTask& systemTask,
                    Controllers::MotorController& motorController);
        ~PrayerAlert() override;

        void SetAlerting();
        void Dismiss();
        void StopRingingOnly();
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        DisplayApp* app;
        Controllers::PrayerController& prayerController;
        System::WakeLock wakeLock;
        Controllers::MotorController& motorController;

        lv_obj_t* nameLabel = nullptr;
        lv_obj_t* timeLabel = nullptr;
        lv_obj_t* btnOk = nullptr;
        lv_task_t* taskStopRinging = nullptr;
      };
    }
  }
}
