#pragma once
#include <cstdint>

// Pure policy and evidence for the bounded external-flash SPI transactions in
// SpiMaster. No FreeRTOS, no hardware registers, so the accounting and the
// deadline maths can be exercised on the build host.

namespace Pinetime {
  namespace Drivers {

    // A wedged external-flash transaction is retried exactly once: deassert CS,
    // stop/disable/reinitialize the SPIM, then run the whole transaction again.
    // The second failure is reported, not retried, so a dead chip surfaces an
    // I/O error in a bounded time instead of a hang that starves the watchdog.
    inline constexpr uint8_t maxTransactionAttempts = 2;

    // Where a bounded transaction gave up. Compact enough to drop into Sys Info
    // later without pulling the driver in.
    enum class SpiTransactionError : uint8_t {
      None = 0,
      MutexTimeout = 1,      // the bus could not be acquired within the bound
      CompletionTimeout = 2, // EVENTS_END never fired, even after recovery+retry
    };

    // Deadline test that stays correct across TickType_t wraparound: the
    // subtraction is done in the unsigned tick type, so the elapsed count is
    // right even when `now` has wrapped past `start`. Templated so a host test
    // can drive it with a synthetic 32-bit clock.
    template <typename Tick>
    constexpr bool TickTimeoutExpired(Tick start, Tick now, Tick timeout) {
      return static_cast<Tick>(now - start) >= timeout;
    }

    // Saturating evidence counters for the flash SPI path. No heap, fixed size,
    // safe to read from Sys Info. "recoveries" counts transactions that only
    // completed after the single retry; "failures" counts the ones that did not
    // complete even then.
    struct SpiTransactionStats {
      uint16_t transactions = 0;  // bus transactions that acquired the mutex
      uint16_t timeouts = 0;      // completion waits that hit the bound
      uint16_t retries = 0;       // recovery+retry cycles run
      uint16_t recoveries = 0;    // transactions that completed only after a retry
      uint16_t failures = 0;      // transactions that failed after the retry
      uint16_t mutexTimeouts = 0; // bus acquisitions that timed out
      SpiTransactionError lastError = SpiTransactionError::None;

      static void Bump(uint16_t& counter) {
        if (counter != UINT16_MAX) {
          ++counter;
        }
      }

      void OnMutexTimeout() {
        Bump(mutexTimeouts);
        lastError = SpiTransactionError::MutexTimeout;
      }

      // One bounded completion wait timed out (a transaction may see two).
      void OnCompletionTimeout() {
        Bump(timeouts);
        lastError = SpiTransactionError::CompletionTimeout;
      }

      // A recovery+retry cycle was started.
      void OnRetry() {
        Bump(retries);
      }

      // Closes out one bus transaction. `attempts` is how many attempts ran
      // (1 or 2); `completed` is whether it finally succeeded.
      void OnTransactionEnd(bool completed, uint8_t attempts) {
        Bump(transactions);
        if (completed) {
          if (attempts > 1) {
            Bump(recoveries);
          }
        } else {
          Bump(failures);
        }
      }
    };
  }
}
