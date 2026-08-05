#pragma once

#include <cstdint>

namespace Pinetime::Controllers {
  // Coalescing, non-blocking latch for watch-originated bond notices. It exists
  // because they are raised on the NimBLE host task, which must never block on
  // the SystemTask queue: a full queue would stall the BLE stack. Instead the
  // notice is latched and delivered best-effort, so it is never dropped and the
  // host task never busy-waits.
  //
  // Coalescing: any number of evictions between two deliveries collapse into a
  // single notice -- the wearer only needs to know that room was made, not how
  // many times. Delivery is injected as a predicate returning whether the
  // bounded, non-blocking queue accepted the message, which keeps this class
  // free of any RTOS or NimBLE dependency and fully host-testable.
  class BondNoticeQueue {
  public:
    enum class Notice : uint8_t {
      FormatInitialized,
      ForgetAllComplete,
      Eviction,
    };

    void LatchFormatInitialized() {
      formatInitializedPending = true;
    }

    void LatchEviction() {
      evictionPending = true;
    }

    void LatchForgetAllComplete() {
      forgetAllCompletePending = true;
    }

    bool Pending() const {
      return formatInitializedPending || evictionPending || forgetAllCompletePending;
    }

    bool FormatInitializedPending() const {
      return formatInitializedPending;
    }

    bool EvictionPending() const {
      return evictionPending;
    }

    bool ForgetAllCompletePending() const {
      return forgetAllCompletePending;
    }

    // Attempt to deliver every pending notice via `deliver`, which returns true
    // when the message was accepted by the queue. A notice is cleared only on
    // acceptance, so a full queue leaves it latched for the next attempt.
    // Returns true when anything is still pending, so the caller can schedule a
    // low-frequency retry. Format initialization and Forget-All completion are
    // offered before eviction so the more significant events win scarce queue
    // slots.
    template <typename Deliver>
    bool Flush(Deliver&& deliver) {
      if (formatInitializedPending && deliver(Notice::FormatInitialized)) {
        formatInitializedPending = false;
      }
      if (forgetAllCompletePending && deliver(Notice::ForgetAllComplete)) {
        forgetAllCompletePending = false;
      }
      if (evictionPending && deliver(Notice::Eviction)) {
        evictionPending = false;
      }
      return Pending();
    }

  private:
    bool formatInitializedPending = false;
    bool evictionPending = false;
    bool forgetAllCompletePending = false;
  };
}
