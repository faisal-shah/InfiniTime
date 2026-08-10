#pragma once

#include <FreeRTOS.h>
#include <timers.h>

namespace Pinetime::Controllers {
  /**
   * Owns a FreeRTOS software timer without using the heap.
   *
   * FreeRTOS timer commands cross a queue boundary and can fail even after a
   * timer was created.  Keep those results visible to callers instead of
   * turning an optional feature into a null dereference or a blocked task.
   */
  class StaticTimer {
  public:
    StaticTimer() = default;
    StaticTimer(const StaticTimer&) = delete;
    StaticTimer& operator=(const StaticTimer&) = delete;

    bool Create(const char* name, TickType_t period, UBaseType_t autoReload, void* id, TimerCallbackFunction_t callback) {
      if (handle != nullptr) {
        return true;
      }
      handle = xTimerCreateStatic(name, period, autoReload, id, callback, &storage);
      return handle != nullptr;
    }

    [[nodiscard]] bool IsCreated() const {
      return handle != nullptr;
    }

    bool Start(TickType_t wait = 0) {
      return handle != nullptr && xTimerStart(handle, wait) == pdPASS;
    }

    bool Reset(TickType_t wait = 0) {
      return handle != nullptr && xTimerReset(handle, wait) == pdPASS;
    }

    bool Stop(TickType_t wait = 0) {
      return handle != nullptr && xTimerStop(handle, wait) == pdPASS;
    }

    bool ChangePeriod(TickType_t period, TickType_t wait = 0) {
      return handle != nullptr && xTimerChangePeriod(handle, period, wait) == pdPASS;
    }

  private:
    TimerHandle_t handle = nullptr;
    StaticTimer_t storage {};
  };
}
