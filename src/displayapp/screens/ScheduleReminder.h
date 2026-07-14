#pragma once

#include "displayapp/screens/Screen.h"
#include "systemtask/WakeLock.h"
#include <lvgl/lvgl.h>

namespace Pinetime {
  namespace Controllers {
    class ScheduleController;
    class MotorController;
  }

  namespace System {
    class SystemTask;
  }

  namespace Applications {
    class DisplayApp;

    namespace Screens {
      // Full-screen reminder shown when a scheduled event fires. Not a launcher
      // app: DisplayApp pushes it on Display::Messages::ScheduleReminderTriggered.
      class ScheduleReminder : public Screen {
      public:
        ScheduleReminder(DisplayApp* app,
                         Controllers::ScheduleController& scheduleController,
                         System::SystemTask& systemTask,
                         Controllers::MotorController& motorController);
        ~ScheduleReminder() override;

        void SetAlerting();
        void Dismiss();
        void StopRingingOnly();
        bool OnButtonPushed() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        DisplayApp* app;
        Controllers::ScheduleController& scheduleController;
        System::WakeLock wakeLock;
        Controllers::MotorController& motorController;

        lv_obj_t* titleLabel = nullptr;
        lv_obj_t* timeLabel = nullptr;
        lv_obj_t* btnOk = nullptr;
        lv_task_t* taskStopRinging = nullptr;
      };
    }
  }
}
