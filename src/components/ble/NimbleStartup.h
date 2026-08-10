#pragma once

#include <cstdint>

namespace Pinetime::Controllers {
  enum class NimblePortError : uint8_t {
    None,
    SchedulerNotRunning,
    LowFrequencyClockUnavailable,
    InitializationAlreadyAttempted,
    EventQueueAllocationFailed,
    MutexAllocationFailed,
    SemaphoreAllocationFailed,
    CalloutAllocationFailed,
    HalTimerInitializationFailed,
    CpuTimeInitializationFailed,
    TaskMemoryAllocationFailed,
    TaskCreationFailed,
    HostStartFailed,
  };

  enum class NimbleHostState : uint8_t {
    Stopped,
    Starting,
    Running,
    Failed,
  };

  template <typename Tick>
  constexpr bool NimbleStartupTimedOut(Tick started, Tick now, Tick timeout) {
    return static_cast<Tick>(now - started) >= timeout;
  }
}
