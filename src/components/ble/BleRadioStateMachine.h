#pragma once

#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    class BleRadioStateMachine {
    public:
      enum class DesiredMode : uint8_t { Off, Connectable, Beacon };
      enum class Mode : uint8_t { Off, FastConnectable, SlowConnectable, Connected, Beacon, Stopping, Starting };
      enum class Command : uint8_t {
        None,
        StopAdvertising,
        StartFastAdvertising,
        StartSlowAdvertising,
        StartBeaconAdvertising,
        TerminateConnection,
        RestoreIdentityAddress,
      };
      enum class Result : uint8_t { Success, AlreadyInactive, AdvertisingActive, Failed };

      struct Action {
        Command command = Command::None;
      };

      static constexpr uint16_t FastIntervalMin = 32;
      static constexpr uint16_t FastIntervalMax = 47;
      static constexpr uint16_t SlowIntervalMin = 1636;
      static constexpr uint16_t SlowIntervalMax = 1651;
      static constexpr uint32_t FastDurationMs = 30000;
      static constexpr uint8_t MaxRetries = 3;
      static constexpr uint32_t ExhaustedRetryDelayMs = 60000;
      static constexpr uint32_t HealthCheckIntervalMs = 60000;

      void SetIdentityAddressIsRandom(bool value);
      void SetDesiredMode(DesiredMode mode);
      void RequestFastConnectable();

      void OnHostSync();
      void OnHostReset();
      void OnConnected();
      void OnConnectionFailed();
      void OnDisconnected();
      void OnAdvertisingComplete();
      void OnAdvertisingHealthCheck(bool active);
      void OnFastTimeout();
      void OnRetryTimeout();
      void OnAddressChanged(bool beaconAddress, int result);

      Action Step();
      void Complete(Command command, int result, Result classification);

      DesiredMode Desired() const;
      Mode Actual() const;
      Command PendingCommand() const;
      bool RetryWaiting() const;
      bool RecoveryBackoff() const;
      uint32_t RetryDelayMs() const;
      uint8_t RetryCount() const;
      uint32_t RecoveryCount() const;
      int LastStartResult() const;
      int LastStopResult() const;
      int LastTerminateResult() const;
      int LastAddressResult() const;
      bool BeaconAddressActive() const;
      bool ExpectsAdvertising() const;

      static const char* ToString(DesiredMode mode) {
        switch (mode) {
          case DesiredMode::Off:
            return "Off";
          case DesiredMode::Connectable:
            return "Connect";
          case DesiredMode::Beacon:
            return "Beacon";
        }
        return "?";
      }

      static const char* ToString(Mode mode) {
        switch (mode) {
          case Mode::Off:
            return "Off";
          case Mode::FastConnectable:
            return "Fast";
          case Mode::SlowConnectable:
            return "Slow";
          case Mode::Connected:
            return "Connected";
          case Mode::Beacon:
            return "Beacon";
          case Mode::Stopping:
            return "Stopping";
          case Mode::Starting:
            return "Starting";
        }
        return "?";
      }

    private:
      enum class Advertising : uint8_t { None, Fast, Slow, Beacon, Unknown };
      enum class Address : uint8_t { Identity, Beacon };

      Action Begin(Command command);
      void FailCommand(Command command);
      void ResetRetry();
      void ResetDisconnectedState();
      static bool IsStart(Command command);

      DesiredMode desired = DesiredMode::Connectable;
      Mode actual = Mode::Off;
      Mode modeBeforeCommand = Mode::Off;
      Advertising advertising = Advertising::None;
      Address address = Address::Identity;
      Command pendingCommand = Command::None;

      bool identityAddressIsRandom = true;
      bool hostSynced = false;
      bool connected = false;
      bool terminationPending = false;
      bool fastTimeoutExpired = false;
      bool startSlowNext = false;
      bool forceFast = false;
      bool retryWaiting = false;
      bool recoveryBackoff = false;

      uint8_t retryCount = 0;
      uint32_t recoveryCount = 0;
      int lastStartResult = 0;
      int lastStopResult = 0;
      int lastTerminateResult = 0;
      int lastAddressResult = 0;
    };
  }
}
