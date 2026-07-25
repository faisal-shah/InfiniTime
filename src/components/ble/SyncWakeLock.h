#pragma once

#include <cstdint>

namespace Pinetime {
  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    // The SPI-flash wake lock a staging-sync service holds for a whole
    // begin..commit transaction: the flash sleeps with the system, and staging
    // writes it, so BeginSync takes the lock (StartFileTransfer) and keeps the
    // flash powered until SystemTask has committed. Shared by ScheduleService
    // and TaskService. Unlike FSService's open-ended wait, WaitUntilAwake is
    // bounded so a misbehaving wake path fails the request instead of stalling
    // the BLE host task forever.
    class SyncWakeLock {
    public:
      explicit SyncWakeLock(System::SystemTask& systemTask);

      // Take the lock (idempotent: a re-Begin restarts the transaction but the
      // lock carries over). Returns false if the system won't wake in time.
      bool Acquire();
      void Release();
      bool Held() const {
        return held;
      }
      // Bounded wait until the system is awake (flash powered).
      bool WaitUntilAwake();

    private:
      System::SystemTask& systemTask;
      bool held = false;
    };
  }
}
