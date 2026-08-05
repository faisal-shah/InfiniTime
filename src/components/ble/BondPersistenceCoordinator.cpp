#include "components/ble/BondPersistenceCoordinator.h"

#include <limits>

using Pinetime::Controllers::BondPersistenceCoordinator;
using Pinetime::Controllers::BondStoreCodec;
using Pinetime::Controllers::BondStorePolicy;
using Pinetime::Controllers::NimbleBondStoreSnapshot;

bool BondPersistenceCoordinator::Reached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

uint32_t BondPersistenceCoordinator::SaturatingDouble(uint32_t value, uint32_t maximum) {
  if (value >= maximum / 2) {
    return maximum;
  }
  return value * 2;
}

void BondPersistenceCoordinator::Schedule(uint32_t deadline) {
  if (!deadlineSet || Reached(deadlineMs, deadline)) {
    deadlineMs = deadline;
    deadlineSet = true;
  }
}

void BondPersistenceCoordinator::UpdateDirty(BondStorePolicy::DirtyState dirty) {
  diagnostics.storeGeneration = dirty.generation;
  diagnostics.criticalDirty = dirty.critical || retryRequired;
  diagnostics.usageDirty = dirty.usage;
}

void BondPersistenceCoordinator::ObserveDirty(BondStorePolicy::DirtyState dirty, uint32_t nowMs, bool connected) {
  UpdateDirty(dirty);
  if (!dirty.critical && !dirty.usage) {
    return;
  }
  if (pending || inFlight) {
    return;
  }
  if (dirty.critical) {
    Schedule(nowMs + CriticalSettleMs);
  } else if (!connected) {
    Schedule(nowMs + UsageDisconnectSettleMs);
  } else if (!deadlineSet) {
    Schedule(nowMs + UsageLongDelayMs);
  }
}

void BondPersistenceCoordinator::OnDisconnect(BondStorePolicy::DirtyState dirty, uint32_t nowMs) {
  UpdateDirty(dirty);
  if ((dirty.critical || dirty.usage) && !pending && !inFlight) {
    deadlineMs = nowMs + UsageDisconnectSettleMs;
    deadlineSet = true;
  }
}

BondPersistenceCoordinator::Action BondPersistenceCoordinator::Poll(uint32_t nowMs) const {
  if (pending) {
    return !deadlineSet || Reached(nowMs, deadlineMs) ? Action::QueueWrite : Action::None;
  }
  if (inFlight || (!diagnostics.criticalDirty && !diagnostics.usageDirty)) {
    return Action::None;
  }
  return deadlineSet && Reached(nowMs, deadlineMs) ? Action::Capture : Action::None;
}

uint32_t BondPersistenceCoordinator::DelayUntilAction(uint32_t nowMs) const {
  if (!deadlineSet) {
    return std::numeric_limits<uint32_t>::max();
  }
  if (Reached(nowMs, deadlineMs)) {
    return 0;
  }
  return deadlineMs - nowMs;
}

bool BondPersistenceCoordinator::Capture(const NimbleBondStoreSnapshot& snapshot) {
  if (pending || inFlight) {
    return false;
  }
  if (!BondStoreCodec::Encode(snapshot, buffer, encodedSize)) {
    return false;
  }
  capturedGeneration = snapshot.generation;
  diagnostics.storeGeneration = snapshot.generation;
  pending = true;
  diagnostics.pending = true;
  deadlineSet = false;
  unstableRetryDelayMs = UnstableRetryBaseMs;
  return true;
}

void BondPersistenceCoordinator::CaptureUnstable(uint32_t nowMs) {
  diagnostics.unstableCaptureRetries++;
  deadlineMs = nowMs + unstableRetryDelayMs;
  deadlineSet = true;
  unstableRetryDelayMs = SaturatingDouble(unstableRetryDelayMs, UnstableRetryMaxMs);
}

void BondPersistenceCoordinator::QueueFailed(uint32_t nowMs) {
  if (inFlight) {
    inFlight = false;
    pending = true;
    diagnostics.inFlight = false;
    diagnostics.pending = true;
  }
  diagnostics.queueRetries++;
  deadlineMs = nowMs + QueueRetryMs;
  deadlineSet = true;
}

void BondPersistenceCoordinator::MarkWriteQueued() {
  if (!pending) {
    return;
  }
  pending = false;
  inFlight = true;
  diagnostics.pending = false;
  diagnostics.inFlight = true;
  deadlineSet = false;
}

BondPersistenceCoordinator::WriteView BondPersistenceCoordinator::CurrentWrite() const {
  if (!inFlight) {
    return {};
  }
  return {buffer.data(), encodedSize, capturedGeneration};
}

void BondPersistenceCoordinator::WriteCompleted(bool success,
                                                uint32_t durationMs,
                                                uint32_t bytes,
                                                BondStorePolicy::DirtyState dirtyAfterCompletion,
                                                uint32_t nowMs,
                                                bool connected) {
  if (!inFlight) {
    return;
  }
  inFlight = false;
  diagnostics.inFlight = false;
  diagnostics.lastWriteDurationMs = durationMs;

  if (success) {
    diagnostics.writeSuccesses++;
    diagnostics.flashWriteCount++;
    diagnostics.flashBytes += bytes;
    failureRetryDelayMs = FailureRetryBaseMs;
    retryRequired = false;
  } else {
    diagnostics.writeFailures++;
    retryRequired = true;
  }

  UpdateDirty(dirtyAfterCompletion);
  if (success && !dirtyAfterCompletion.critical && !dirtyAfterCompletion.usage) {
    deadlineSet = false;
    return;
  }

  if (success) {
    // A change that arrived while the immutable buffer was being written is
    // captured next without another debounce window.
    deadlineMs = nowMs;
    deadlineSet = true;
    return;
  }

  // A failed write is never clean, even if the adapter's dirty baseline was
  // clean (the boot format-initialization commit is the important example).
  diagnostics.criticalDirty = true;
  deadlineMs = nowMs + failureRetryDelayMs;
  deadlineSet = true;
  failureRetryDelayMs = SaturatingDouble(failureRetryDelayMs, FailureRetryMaxMs);
  if (!dirtyAfterCompletion.critical && connected) {
    Schedule(nowMs + UsageLongDelayMs);
  }
}

void BondPersistenceCoordinator::RecordBoot(BootState state,
                                            BondStoreCodec::DecodeError decodeError,
                                            bool legacyReset) {
  diagnostics.bootState = state;
  diagnostics.lastDecodeError = decodeError;
  if (decodeError != BondStoreCodec::DecodeError::None) {
    diagnostics.decodeFailures++;
    if (decodeError == BondStoreCodec::DecodeError::Crc) {
      diagnostics.crcFailures++;
    }
  }
  if (legacyReset && !diagnostics.legacyResetThisBoot) {
    diagnostics.legacyResetCount++;
  }
  diagnostics.legacyResetThisBoot = diagnostics.legacyResetThisBoot || legacyReset;
}
