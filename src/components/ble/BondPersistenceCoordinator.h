#pragma once

#include "components/ble/BondStoreCodec.h"
#include "components/ble/BondStorePolicy.h"

#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {
  // Host-task scheduler plus the single fixed-size encoded snapshot buffer.
  // Once a snapshot is pending or in flight, the buffer is immutable until the
  // SystemTask completion is handled back on the host task.
  class BondPersistenceCoordinator {
  public:
    static constexpr uint32_t CriticalSettleMs = 250;
    static constexpr uint32_t UsageDisconnectSettleMs = 250;
    static constexpr uint32_t UsageLongDelayMs = 30u * 60u * 1000u;
    static constexpr uint32_t QueueRetryMs = 100;
    static constexpr uint32_t UnstableRetryBaseMs = 250;
    static constexpr uint32_t UnstableRetryMaxMs = 2000;
    static constexpr uint32_t FailureRetryBaseMs = 10u * 1000u;
    static constexpr uint32_t FailureRetryMaxMs = 5u * 60u * 1000u;

    enum class Action : uint8_t {
      None,
      Capture,
      QueueWrite,
    };

    enum class BootState : uint8_t {
      Unknown,
      Restoring,
      Restored,
      InitializingEmpty,
      InitializedEmpty,
      Missing,
      Invalid,
      RestoreFailed,
      HandshakeFailed,
    };

    struct Diagnostics {
      uint64_t storeGeneration = 0;
      bool criticalDirty = false;
      bool usageDirty = false;
      bool pending = false;
      bool inFlight = false;
      uint32_t writeSuccesses = 0;
      uint32_t writeFailures = 0;
      uint32_t lastWriteDurationMs = 0;
      uint32_t decodeFailures = 0;
      uint32_t crcFailures = 0;
      uint32_t unstableCaptureRetries = 0;
      uint32_t queueRetries = 0;
      uint32_t flashWriteCount = 0;
      uint32_t flashBytes = 0;
      uint32_t legacyResetCount = 0;
      bool legacyResetThisBoot = false;
      BondStoreCodec::DecodeError lastDecodeError = BondStoreCodec::DecodeError::None;
      BootState bootState = BootState::Unknown;
    };

    struct WriteView {
      const uint8_t* data = nullptr;
      size_t size = 0;
      uint64_t generation = 0;

      explicit operator bool() const {
        return data != nullptr && size != 0;
      }
    };

    void ObserveDirty(BondStorePolicy::DirtyState dirty, uint32_t nowMs, bool connected);
    void OnDisconnect(BondStorePolicy::DirtyState dirty, uint32_t nowMs);
    Action Poll(uint32_t nowMs) const;
    uint32_t DelayUntilAction(uint32_t nowMs) const;

    bool Capture(const NimbleBondStoreSnapshot& snapshot);
    void CaptureUnstable(uint32_t nowMs);
    void QueueFailed(uint32_t nowMs);
    void MarkWriteQueued();
    WriteView CurrentWrite() const;

    void WriteCompleted(bool success,
                        uint32_t durationMs,
                        uint32_t bytes,
                        BondStorePolicy::DirtyState dirtyAfterCompletion,
                        uint32_t nowMs,
                        bool connected);

    BondStoreCodec::Buffer& BootBuffer() {
      return buffer;
    }

    void RecordBoot(BootState state,
                    BondStoreCodec::DecodeError decodeError = BondStoreCodec::DecodeError::None,
                    bool legacyReset = false);

    // Whether an empty-initialization boot should announce on the watch that it
    // cleared a prior pairing. True only when a real prior store was discarded:
    // a legacy bond file existed, or a valid pre-marker new-format store was
    // intentionally reset. A truly fresh watch -- no prior file of any kind --
    // returns false, so it never shows "Update cleared old pairings". Pure and
    // host-testable.
    static constexpr bool AnnouncesLegacyReset(bool legacyFilePresent, bool preMarkerStoreDiscarded) {
      return legacyFilePresent || preMarkerStoreDiscarded;
    }

    const Diagnostics& GetDiagnostics() const {
      return diagnostics;
    }

  private:
    static bool Reached(uint32_t nowMs, uint32_t deadlineMs);
    static uint32_t SaturatingDouble(uint32_t value, uint32_t maximum);
    void Schedule(uint32_t deadlineMs);
    void UpdateDirty(BondStorePolicy::DirtyState dirty);

    BondStoreCodec::Buffer buffer {};
    size_t encodedSize = 0;
    uint64_t capturedGeneration = 0;
    uint32_t deadlineMs = 0;
    uint32_t unstableRetryDelayMs = UnstableRetryBaseMs;
    uint32_t failureRetryDelayMs = FailureRetryBaseMs;
    bool deadlineSet = false;
    bool pending = false;
    bool inFlight = false;
    bool retryRequired = false;
    Diagnostics diagnostics {};
  };
}
