#include "BleRadioStateMachine.h"

#include <cstdio>
#include <vector>

using Radio = Pinetime::Controllers::BleRadioStateMachine;
using Command = Radio::Command;
using Desired = Radio::DesiredMode;
using Mode = Radio::Mode;
using Result = Radio::Result;

namespace {
  int checks = 0;
  int failures = 0;

  void Check(bool condition, const char* message) {
    checks++;
    if (!condition) {
      failures++;
      std::printf("FAIL: %s\n", message);
    }
  }

  Command Step(Radio& radio) {
    return radio.Step().command;
  }

  void Succeed(Radio& radio, Command command) {
    radio.Complete(command, 0, Result::Success);
  }

  void BootFast(Radio& radio) {
    radio.OnHostSync();
    const auto command = Step(radio);
    Check(command == Command::StartFastAdvertising, "boot emits fast start");
    Succeed(radio, command);
    Check(radio.Actual() == Mode::FastConnectable, "boot reaches fast connectable");
  }

  bool IsStart(Command command) {
    return command == Command::StartFastAdvertising || command == Command::StartSlowAdvertising ||
           command == Command::StartBeaconAdvertising;
  }

  bool IsStop(Command command) {
    return command == Command::StopAdvertising || command == Command::TerminateConnection;
  }

}

int main() {
  {
    Radio radio;
    BootFast(radio);
  }

  {
    Radio radio;
    BootFast(radio);
    radio.OnFastTimeout();
    auto command = Step(radio);
    Check(command == Command::StopAdvertising, "fast timeout stops advertising");
    Succeed(radio, command);
    command = Step(radio);
    Check(command == Command::StartSlowAdvertising, "fast timeout starts slow in the next event");
    Succeed(radio, command);
    Check(radio.Actual() == Mode::SlowConnectable, "slow advertising runs forever");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.OnConnected();
    Check(radio.Actual() == Mode::Connected, "connect event records connected");
    radio.OnDisconnected();
    const auto command = Step(radio);
    Check(command == Command::StartFastAdvertising, "disconnect restarts fast advertising");
  }

  {
    Radio radio;
    radio.OnHostSync();
    auto command = Step(radio);
    radio.Complete(command, 30, Result::Failed);
    Check(radio.RetryWaiting(), "failed start schedules retry");
    Check(radio.RetryCount() == 1, "failed start records retry count");
    Check(radio.RetryDelayMs() == 250, "first retry is delayed");
    Check(Step(radio) == Command::None, "retry does not poll while waiting");
    radio.OnRetryTimeout();
    command = Step(radio);
    Check(command == Command::StartFastAdvertising, "retry event reissues failed start");
    Succeed(radio, command);
    Check(radio.RetryCount() == 0, "successful retry clears consecutive count");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.OnFastTimeout();
    auto command = Step(radio);
    Succeed(radio, command);
    command = Step(radio);
    Check(command == Command::StartSlowAdvertising, "slow transition attempts slow start");
    radio.Complete(command, 30, Result::Failed);
    radio.OnRetryTimeout();
    Check(Step(radio) == Command::StartFastAdvertising, "failed slow start recovers at the fast interval");
  }

  {
    Radio radio;
    radio.OnHostSync();
    constexpr uint32_t expectedDelays[] = {250, 1000, 4000};
    for (uint8_t retry = 0; retry < Radio::MaxRetries; retry++) {
      const auto command = Step(radio);
      Check(command == Command::StartFastAdvertising, "bounded retry emits only a start attempt");
      radio.Complete(command, 30, Result::Failed);
      Check(radio.RetryDelayMs() == expectedDelays[retry], "bounded retry uses increasing backoff");
      radio.OnRetryTimeout();
    }
    auto command = Step(radio);
    radio.Complete(command, 30, Result::Failed);
    Check(radio.RecoveryBackoff(), "exhausted retries enter low-frequency recovery");
    Check(radio.RetryDelayMs() == Radio::ExhaustedRetryDelayMs, "exhausted retry waits one minute");
    Check(Step(radio) == Command::None, "low-frequency recovery does not churn while waiting");
    radio.OnRetryTimeout();
    command = Step(radio);
    radio.Complete(command, 30, Result::Failed);
    Check(radio.RetryCount() == Radio::MaxRetries, "low-frequency recovery preserves the retry budget");
    Check(radio.RetryDelayMs() == Radio::ExhaustedRetryDelayMs, "persistent failure stays at one-minute cadence");
  }

  {
    Radio radio;
    radio.OnHostSync();
    constexpr uint32_t expectedDelays[] = {250, 1000, 4000, Radio::ExhaustedRetryDelayMs};
    for (uint8_t attempt = 0; attempt <= Radio::MaxRetries; attempt++) {
      auto command = Step(radio);
      Check(command == Command::StartFastAdvertising, "EALREADY recovery attempts a start");
      radio.Complete(command, 2, Result::AdvertisingActive);
      Check(radio.RetryDelayMs() == expectedDelays[attempt], "EALREADY preserves bounded backoff");
      if (attempt == Radio::MaxRetries) {
        break;
      }

      radio.OnRetryTimeout();
      command = Step(radio);
      Check(command == Command::StopAdvertising, "EALREADY recovery stops in a later event");
      radio.Complete(command, attempt % 2 == 0 ? 0 : 2, attempt % 2 == 0 ? Result::Success : Result::AlreadyInactive);
      Check(radio.RetryCount() == attempt + 1, "intervening stop preserves the recovery budget");
    }

    Check(radio.RecoveryBackoff(), "persistent EALREADY reaches low-frequency recovery");
    radio.OnRetryTimeout();
    auto command = Step(radio);
    Check(command == Command::StopAdvertising, "low-frequency EALREADY recovery starts with stop");
    radio.Complete(command, 2, Result::AlreadyInactive);
    command = Step(radio);
    Check(command == Command::StartFastAdvertising, "low-frequency EALREADY recovery starts in the next event");
    radio.Complete(command, 2, Result::AdvertisingActive);
    Check(radio.RetryCount() == Radio::MaxRetries, "low-frequency EALREADY recovery remains bounded");
    Check(radio.RetryDelayMs() == Radio::ExhaustedRetryDelayMs, "EALREADY cannot return to 250 ms churn");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.SetDesiredMode(Desired::Off);
    auto command = Step(radio);
    Check(command == Command::StopAdvertising, "radio off stops connectable advertising");
    Succeed(radio, command);
    Check(Step(radio) == Command::None && radio.Actual() == Mode::Off, "radio remains off");
    radio.SetDesiredMode(Desired::Connectable);
    Check(Step(radio) == Command::StartFastAdvertising, "radio on requests fast advertising");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.OnConnected();
    radio.SetDesiredMode(Desired::Off);
    const auto command = Step(radio);
    Check(command == Command::TerminateConnection, "radio off terminates a connection");
    Succeed(radio, command);
    Check(Step(radio) == Command::None, "terminate waits for disconnect before reconciling");
    radio.OnDisconnected();
    Check(Step(radio) == Command::None && radio.Actual() == Mode::Off, "disconnect honors off intent");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.SetDesiredMode(Desired::Off);
    const auto command = Step(radio);
    radio.Complete(command, 2, Result::AlreadyInactive);
    Check(radio.Actual() == Mode::Off, "already-inactive stop reaches off");
    Check(Step(radio) == Command::None, "already-inactive stop does not retry");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.OnConnected();
    radio.SetDesiredMode(Desired::Off);
    const auto command = Step(radio);
    radio.Complete(command, 7, Result::AlreadyInactive);
    Check(radio.Actual() == Mode::Off, "already-inactive terminate clears connected state");
    Check(Step(radio) == Command::None, "already-inactive terminate follows off intent");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.OnConnected();
    radio.SetDesiredMode(Desired::Beacon);
    auto command = Step(radio);
    Check(command == Command::TerminateConnection, "beacon request terminates a connection first");
    Succeed(radio, command);
    radio.OnDisconnected();
    command = Step(radio);
    Check(command == Command::StartBeaconAdvertising, "disconnect follows current beacon intent");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.SetDesiredMode(Desired::Beacon);
    std::vector<Command> enter;
    auto command = Step(radio);
    enter.push_back(command);
    Succeed(radio, command);
    command = Step(radio);
    enter.push_back(command);
    Succeed(radio, command);
    Check(enter == std::vector<Command>({Command::StopAdvertising, Command::StartBeaconAdvertising}),
          "normal to beacon uses separate stop and start events");
    Check(radio.Actual() == Mode::Beacon, "beacon reaches active mode");

    radio.SetDesiredMode(Desired::Connectable);
    std::vector<Command> exit;
    command = Step(radio);
    exit.push_back(command);
    Succeed(radio, command);
    command = Step(radio);
    exit.push_back(command);
    Succeed(radio, command);
    Check(exit == std::vector<Command>({Command::StopAdvertising, Command::StartFastAdvertising}),
          "beacon to normal uses separate stop and start events");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.SetDesiredMode(Desired::Beacon);
    auto command = Step(radio);
    Succeed(radio, command);
    command = Step(radio);
    Succeed(radio, command);

    radio.SetDesiredMode(Desired::Off);
    command = Step(radio);
    Check(command == Command::StopAdvertising, "radio off stops beacon");
    Succeed(radio, command);
    command = Step(radio);
    Check(command == Command::RestoreIdentityAddress, "radio off restores identity");
    Succeed(radio, command);
    Check(Step(radio) == Command::None, "radio off never starts normal advertising");
  }

  {
    Radio radio;
    BootFast(radio);
    radio.OnFastTimeout();
    const Command stopEvent = Step(radio);
    Succeed(radio, stopEvent);
    const Command startEvent = Step(radio);
    Check(IsStop(stopEvent) && !IsStart(stopEvent), "stop event emits no start command");
    Check(IsStart(startEvent) && !IsStop(startEvent), "following event emits only the start command");
  }

  {
    Radio radio;
    BootFast(radio);
    Check(radio.ExpectsAdvertising(), "running fast mode expects advertising");
    radio.OnAdvertisingHealthCheck(true);
    Check(Step(radio) == Command::None, "healthy advertising is never stopped or restarted");
    radio.OnAdvertisingHealthCheck(false);
    Check(Step(radio) == Command::StartFastAdvertising, "inactive health sample recovers at fast interval");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
