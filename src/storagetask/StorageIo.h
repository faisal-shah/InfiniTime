#pragma once

#include <cstddef>
#include <cstdint>

namespace Pinetime::System {
  struct StorageIoPolicy {
    static constexpr size_t MaxPathLength = 255;
    static constexpr size_t FileTransferChunkSize = 512;

    static bool ValidPath(const char* path) {
      if (path == nullptr) {
        return false;
      }
      for (size_t index = 0; index <= MaxPathLength; index++) {
        if (path[index] == '\0') {
          return true;
        }
      }
      return false;
    }

    static constexpr bool ValidChunk(size_t size) {
      return size <= FileTransferChunkSize;
    }

    template <typename Tick>
    static constexpr Tick RemainingTicks(Tick started, Tick now, Tick budget) {
      const Tick elapsed = now - started;
      return elapsed >= budget ? 0 : budget - elapsed;
    }
  };

  // The semaphore is only a wake-up hint. This state machine is the source of
  // truth, so a late completion or stale semaphore token can never complete a
  // newer request. StorageTask serializes calls to these methods with a
  // FreeRTOS critical section.
  class StorageIoLifecycle {
  public:
    enum class Phase : uint8_t {
      Idle,
      Queued,
      Running,
      Completed,
      Abandoned,
    };

    enum class TimeoutResult : uint8_t {
      Completed,
      Abandoned,
      Stale,
    };

    struct FinishAction {
      bool signalWaiter = false;
      bool releaseAccess = false;
    };

    uint32_t Begin(bool asynchronous) {
      if (phase != Phase::Idle) {
        return 0;
      }
      generation++;
      if (generation == 0) {
        generation++;
      }
      this->asynchronous = asynchronous;
      phase = Phase::Queued;
      return generation;
    }

    bool Start(uint32_t requestGeneration) {
      if (!Matches(requestGeneration) || (phase != Phase::Queued && phase != Phase::Abandoned)) {
        return false;
      }
      if (phase == Phase::Queued) {
        phase = Phase::Running;
      }
      return true;
    }

    bool Cancel(uint32_t requestGeneration) {
      if (!Matches(requestGeneration) || phase != Phase::Queued) {
        return false;
      }
      Reset();
      return true;
    }

    bool IsCompleted(uint32_t requestGeneration) const {
      return Matches(requestGeneration) && phase == Phase::Completed;
    }

    TimeoutResult Timeout(uint32_t requestGeneration) {
      if (!Matches(requestGeneration)) {
        return TimeoutResult::Stale;
      }
      if (phase == Phase::Completed) {
        return TimeoutResult::Completed;
      }
      if (phase == Phase::Queued || phase == Phase::Running) {
        phase = Phase::Abandoned;
        return TimeoutResult::Abandoned;
      }
      return TimeoutResult::Stale;
    }

    FinishAction Finish(uint32_t requestGeneration) {
      if (!Matches(requestGeneration) || (phase != Phase::Running && phase != Phase::Abandoned)) {
        return {};
      }
      if (asynchronous || phase == Phase::Abandoned) {
        Reset();
        return {.releaseAccess = true};
      }
      phase = Phase::Completed;
      return {.signalWaiter = true};
    }

    bool ReleaseCompleted(uint32_t requestGeneration) {
      if (!IsCompleted(requestGeneration)) {
        return false;
      }
      Reset();
      return true;
    }

    Phase CurrentPhase() const {
      return phase;
    }

    uint32_t CurrentGeneration() const {
      return generation;
    }

  private:
    bool Matches(uint32_t requestGeneration) const {
      return requestGeneration != 0 && requestGeneration == generation;
    }

    void Reset() {
      phase = Phase::Idle;
      asynchronous = false;
    }

    uint32_t generation = 0;
    Phase phase = Phase::Idle;
    bool asynchronous = false;
  };
}
