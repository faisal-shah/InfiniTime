#include "components/ble/BondPersistenceCoordinator.h"

#include <cstdint>
#include <cstdio>

using Pinetime::Controllers::BondPersistenceCoordinator;
using Pinetime::Controllers::BondRegistry;
using Pinetime::Controllers::BondStorePolicy;
using Pinetime::Controllers::NimbleBondStoreSnapshot;

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* description) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", description);
    }
  }

  BondStorePolicy::DirtyState Critical(uint64_t generation) {
    return {true, false, generation};
  }

  BondStorePolicy::DirtyState Usage(uint64_t generation) {
    return {false, true, generation};
  }

  NimbleBondStoreSnapshot Empty(uint64_t generation) {
    NimbleBondStoreSnapshot snapshot;
    snapshot.generation = generation;
    return snapshot;
  }

  BondRegistry::PeerIdentity Peer(uint8_t value) {
    return {1, {value, static_cast<uint8_t>(value + 1), 2, 3, 4, 5}};
  }
}

int main() {
  using Action = BondPersistenceCoordinator::Action;

  {
    BondPersistenceCoordinator coordinator;
    coordinator.ObserveDirty(Critical(1), 1000, true);
    Check(coordinator.Poll(1249) == Action::None, "critical write waits for the short settle window");
    Check(coordinator.Poll(1250) == Action::Capture, "critical write captures promptly after settling");
    Check(coordinator.Capture(Empty(1)), "stable snapshot enters the fixed buffer");
    Check(coordinator.Poll(1250) == Action::QueueWrite, "captured buffer is ready to queue");
    coordinator.MarkWriteQueued();
    Check(coordinator.CurrentWrite().generation == 1, "in-flight message references captured generation");
    Check(coordinator.GetDiagnostics().inFlight && !coordinator.GetDiagnostics().pending,
          "diagnostics distinguish pending and in-flight");
    coordinator.WriteCompleted(true, 12, 84, {false, false, 1}, 1300, true);
    Check(coordinator.Poll(1300) == Action::None, "successful acknowledged write becomes clean");
    Check(coordinator.GetDiagnostics().writeSuccesses == 1 &&
            coordinator.GetDiagnostics().flashWriteCount == 1 &&
            coordinator.GetDiagnostics().flashBytes == 84,
          "successful write counters and bytes are exposed");
  }

  {
    BondPersistenceCoordinator coordinator;
    coordinator.ObserveDirty(Usage(4), 1000, true);
    Check(coordinator.Poll(1000 + BondPersistenceCoordinator::UsageLongDelayMs - 1) == Action::None,
          "usage-only touch is held for the long timer while connected");
    coordinator.ObserveDirty(Usage(5), 5000, true);
    Check(coordinator.Poll(1000 + BondPersistenceCoordinator::UsageLongDelayMs) == Action::Capture,
          "repeated usage touches coalesce without extending the original long timer");
  }

  {
    BondPersistenceCoordinator coordinator;
    coordinator.ObserveDirty(Usage(8), 1000, true);
    coordinator.OnDisconnect(Usage(8), 2000);
    Check(coordinator.Poll(2249) == Action::None, "disconnect usage flush keeps a settle window");
    Check(coordinator.Poll(2250) == Action::Capture, "disconnect expedites usage-only persistence");
  }

  {
    BondPersistenceCoordinator coordinator;
    BondStorePolicy policy;
    policy.OnBondEstablished(Peer(1));
    policy.OnBondEstablished(Peer(2));
    policy.AcknowledgePersisted(policy.Generation());

    const uint64_t generation = policy.Generation();
    Check(policy.OnKnownConnection(Peer(2)), "battery read identifies the current-MRU peer as retained");
    Check(policy.Generation() == generation && !policy.Dirty().usage,
          "redundant battery read leaves usage persistence clean");
    coordinator.ObserveDirty(policy.Dirty(), 1000, true);
    coordinator.OnDisconnect(policy.Dirty(), 2000);
    Check(coordinator.Poll(2250) == Action::None,
          "redundant battery read followed by disconnect schedules no persistence action");

    Check(policy.OnKnownConnection(Peer(1)), "older peer is retained on connection");
    Check(policy.Generation() == generation + 1 && policy.Dirty().usage,
          "actual LRU order change marks usage persistence dirty");
    coordinator.ObserveDirty(policy.Dirty(), 3000, true);
    coordinator.OnDisconnect(policy.Dirty(), 4000);
    Check(coordinator.Poll(4249) == Action::None && coordinator.Poll(4250) == Action::Capture,
          "actual LRU order change still schedules the disconnect usage write");
  }

  {
    BondPersistenceCoordinator coordinator;
    coordinator.ObserveDirty(Critical(2), 0, false);
    coordinator.CaptureUnstable(250);
    Check(coordinator.Poll(499) == Action::None && coordinator.Poll(500) == Action::Capture,
          "unstable capture retries after bounded base delay");
    coordinator.CaptureUnstable(500);
    Check(coordinator.Poll(1000) == Action::Capture, "unstable retry delay backs off");
    coordinator.CaptureUnstable(1000);
    coordinator.CaptureUnstable(2000);
    Check(coordinator.Poll(3999) == Action::None && coordinator.Poll(4000) == Action::Capture,
          "unstable retry delay is bounded at two seconds");
    Check(coordinator.GetDiagnostics().unstableCaptureRetries == 4,
          "unstable capture retries are counted");
  }

  {
    BondPersistenceCoordinator coordinator;
    coordinator.ObserveDirty(Critical(3), 0, false);
    coordinator.Capture(Empty(3));
    coordinator.MarkWriteQueued();
    coordinator.QueueFailed(250);
    Check(coordinator.Poll(349) == Action::None && coordinator.Poll(350) == Action::QueueWrite,
          "full SystemTask queue retries without recapturing");
    Check(coordinator.GetDiagnostics().queueRetries == 1, "queue retry is diagnosed");
    coordinator.MarkWriteQueued();
    coordinator.WriteCompleted(false, 9, 84, Critical(3), 400, false);
    Check(coordinator.Poll(10399) == Action::None && coordinator.Poll(10400) == Action::Capture,
          "write failure retains dirty state and uses low-frequency retry");
    Check(coordinator.GetDiagnostics().writeFailures == 1, "write failure is counted");
  }

  {
    BondPersistenceCoordinator coordinator;
    coordinator.ObserveDirty(Critical(10), 0, false);
    coordinator.Capture(Empty(10));
    coordinator.MarkWriteQueued();
    coordinator.WriteCompleted(true, 5, 84, Critical(11), 100, false);
    Check(coordinator.Poll(100) == Action::Capture,
          "change arriving during write is captured immediately after acknowledgement");
  }

  {
    BondPersistenceCoordinator coordinator;
    coordinator.RecordBoot(BondPersistenceCoordinator::BootState::Invalid,
                           Pinetime::Controllers::BondStoreCodec::DecodeError::Crc,
                           true);
    const auto& diagnostics = coordinator.GetDiagnostics();
    Check(diagnostics.decodeFailures == 1 && diagnostics.crcFailures == 1,
          "decode and CRC diagnostics are separate");
    Check(diagnostics.legacyResetThisBoot && diagnostics.legacyResetCount == 1,
          "legacy reset seam is exposed");
  }

  // The legacy-reset announcement decision: only a genuinely discarded prior
  // store announces itself; a fresh watch stays silent.
  {
    Check(!BondPersistenceCoordinator::AnnouncesLegacyReset(false, false),
          "a fresh watch with no prior store announces nothing");
    Check(BondPersistenceCoordinator::AnnouncesLegacyReset(true, false),
          "a discarded legacy bond file announces a reset");
    Check(BondPersistenceCoordinator::AnnouncesLegacyReset(false, true),
          "a discarded pre-marker new-format store announces a reset");
    Check(BondPersistenceCoordinator::AnnouncesLegacyReset(true, true),
          "both a legacy file and a pre-marker store still announce a reset");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
