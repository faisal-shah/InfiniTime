#pragma once
#include <FreeRTOS.h>
#include <atomic>
#include <cstdint>
#include <optional>
#include <task.h>
#include <queue.h>
#include <components/heartrate/Ppg.h>
#include "components/settings/Settings.h"

namespace Pinetime {
  namespace Drivers {
    class Hrs3300;
  }

  namespace Controllers {
    class HeartRateController;
  }

  namespace Applications {
    class HeartRateTask {
    public:
      enum class Messages : uint8_t { GoToSleep, WakeUp, Enable, Disable };

      explicit HeartRateTask(Drivers::Hrs3300& heartRateSensor,
                             Controllers::HeartRateController& controller,
                             Controllers::Settings& settings);
      [[nodiscard]] bool Start();

      void SetSensorAvailable(bool available) {
        sensorAvailable.store(available, std::memory_order_release);
      }

      [[nodiscard]] bool Started() const {
        return taskHandle.load(std::memory_order_acquire) != nullptr;
      }

      void Work();
      // Task-context API. Returns false when lazy startup or the non-blocking
      // queue send fails; ISR callers must not allocate/start this optional
      // task and therefore need a separate explicit path if one is ever added.
      bool PushMessage(Messages msg);

    private:
      enum class States : uint8_t { Disabled, Waiting, BackgroundMeasuring, ForegroundMeasuring };
      static void Process(void* instance);
      void HandleSensorData();
      void StartMeasurement();
      void StopMeasurement();

      [[nodiscard]] bool BackgroundMeasurementNeeded() const;
      [[nodiscard]] std::optional<TickType_t> BackgroundMeasurementInterval() const;
      TickType_t CurrentTaskDelay();

      std::atomic<TaskHandle_t> taskHandle {nullptr};
      QueueHandle_t messageQueue = nullptr;
      std::atomic<bool> sensorAvailable {false};
      bool valueCurrentlyShown;
      bool measurementSucceeded;
      States state = States::Disabled;
      uint16_t count;
      Drivers::Hrs3300& heartRateSensor;
      Controllers::HeartRateController& controller;
      Controllers::Settings& settings;
      Controllers::Ppg ppg;
      TickType_t lastMeasurementTime;
      TickType_t measurementStartTime;
    };

  }
}
