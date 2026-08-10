#pragma once

#include <atomic>
#include <cstdint>
#include <components/ble/HeartRateService.h>

namespace Pinetime {
  namespace Applications {
    class HeartRateTask;
  }

  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class HeartRateController {
    public:
      enum class States : uint8_t { Stopped, NotEnoughData, NoTouch, Running };

      HeartRateController() = default;
      bool Enable();
      void Disable();
      void Update(States newState, uint8_t heartRate);

      void SetHeartRateTask(Applications::HeartRateTask* task);

      States State() const {
        return state.load(std::memory_order_relaxed);
      }

      uint8_t HeartRate() const {
        return heartRate.load(std::memory_order_relaxed);
      }

      void SetService(Pinetime::Controllers::HeartRateService* service);

    private:
      Applications::HeartRateTask* task = nullptr;
      std::atomic<States> state {States::Stopped};
      std::atomic<uint8_t> heartRate {0};
      // Set before a Disable message is queued so an in-flight sensor sample
      // cannot republish Running while the worker has not consumed it yet.
      std::atomic<bool> stopRequested {false};
      Pinetime::Controllers::HeartRateService* service = nullptr;
    };
  }
}
