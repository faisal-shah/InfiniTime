#pragma once

#include <memory>
#include "displayapp/screens/Screen.h"
#include "displayapp/screens/ScreenList.h"

namespace Pinetime {
  namespace Controllers {
    class DateTime;
    class Battery;
    class BrightnessController;
    class Ble;
  }

  namespace Drivers {
    class Watchdog;
  }

  namespace System {
    class SystemTask;
  }

  namespace Applications {
    class DisplayApp;

    namespace Screens {
      class SystemInfo : public Screen {
      public:
        explicit SystemInfo(DisplayApp* app,
                            Pinetime::Controllers::DateTime& dateTimeController,
                            const Pinetime::Controllers::Battery& batteryController,
                            Pinetime::Controllers::BrightnessController& brightnessController,
                            const Pinetime::Controllers::Ble& bleController,
                            const Pinetime::Drivers::Watchdog& watchdog,
                            Pinetime::Controllers::MotionController& motionController,
                            const Pinetime::Drivers::Cst816S& touchPanel,
                            const Pinetime::Drivers::SpiNorFlash& spiNorFlash,
                            const Pinetime::System::SystemTask& systemTask);
        ~SystemInfo() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        Pinetime::Controllers::DateTime& dateTimeController;
        const Pinetime::Controllers::Battery& batteryController;
        Pinetime::Controllers::BrightnessController& brightnessController;
        const Pinetime::Controllers::Ble& bleController;
        const Pinetime::Drivers::Watchdog& watchdog;
        Pinetime::Controllers::MotionController& motionController;
        const Pinetime::Drivers::Cst816S& touchPanel;
        const Pinetime::Drivers::SpiNorFlash& spiNorFlash;
        const Pinetime::System::SystemTask& systemTask;

        static constexpr uint8_t ScreenCount = 10;
        ScreenList<ScreenCount> screens;

        static bool sortById(const TaskStatus_t& lhs, const TaskStatus_t& rhs);

        std::unique_ptr<Screen> CreateScreen1();
        std::unique_ptr<Screen> CreateScreen2();
        std::unique_ptr<Screen> CreateScreen3();
        std::unique_ptr<Screen> CreateScreen4();
        std::unique_ptr<Screen> CreateScreen5();
        std::unique_ptr<Screen> CreateScreen6();
        std::unique_ptr<Screen> CreateScreen7();
        std::unique_ptr<Screen> CreateScreen8();
        std::unique_ptr<Screen> CreateScreen9();
        std::unique_ptr<Screen> CreateScreen10();
      };
    }
  }
}
