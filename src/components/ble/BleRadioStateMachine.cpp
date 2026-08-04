#include "components/ble/BleRadioStateMachine.h"

using namespace Pinetime::Controllers;

void BleRadioStateMachine::SetIdentityAddressIsRandom(bool value) {
  identityAddressIsRandom = value;
}

void BleRadioStateMachine::SetDesiredMode(DesiredMode mode) {
  if (desired == mode) {
    return;
  }

  desired = mode;
  ResetRetry();
  if (mode == DesiredMode::Connectable) {
    fastTimeoutExpired = false;
    startSlowNext = false;
    forceFast = true;
  }
}

void BleRadioStateMachine::RequestFastConnectable() {
  fastTimeoutExpired = false;
  startSlowNext = false;
  forceFast = true;
}

void BleRadioStateMachine::OnHostSync() {
  hostSynced = true;
  pendingCommand = Command::None;
}

void BleRadioStateMachine::OnHostReset() {
  hostSynced = false;
  pendingCommand = Command::None;
  advertising = Advertising::None;
  address = Address::Identity;
  connected = false;
  terminationPending = false;
  actual = Mode::Off;
  fastTimeoutExpired = false;
  startSlowNext = false;
  forceFast = desired == DesiredMode::Connectable;
  ResetRetry();
}

void BleRadioStateMachine::OnConnected() {
  connected = true;
  terminationPending = false;
  advertising = Advertising::None;
  pendingCommand = Command::None;
  actual = Mode::Connected;
  fastTimeoutExpired = false;
  startSlowNext = false;
  forceFast = false;
  retryWaiting = false;
}

void BleRadioStateMachine::OnConnectionFailed() {
  ResetDisconnectedState();
}

void BleRadioStateMachine::OnDisconnected() {
  ResetDisconnectedState();
}

void BleRadioStateMachine::OnAdvertisingComplete() {
  if (advertising != Advertising::None) {
    recoveryCount++;
  }
  advertising = Advertising::None;
  pendingCommand = Command::None;
  actual = Mode::Off;
  fastTimeoutExpired = false;
  startSlowNext = false;
  forceFast = desired == DesiredMode::Connectable;
  retryWaiting = false;
}

void BleRadioStateMachine::OnAdvertisingHealthCheck(bool active) {
  if (active || !ExpectsAdvertising()) {
    return;
  }

  recoveryCount++;
  advertising = Advertising::None;
  pendingCommand = Command::None;
  actual = Mode::Off;
  fastTimeoutExpired = false;
  startSlowNext = false;
  forceFast = desired == DesiredMode::Connectable;
  retryWaiting = false;
}

void BleRadioStateMachine::OnFastTimeout() {
  if (advertising == Advertising::Fast) {
    fastTimeoutExpired = true;
    startSlowNext = true;
  }
}

void BleRadioStateMachine::OnRetryTimeout() {
  retryWaiting = false;
}

void BleRadioStateMachine::OnAddressChanged(bool beaconAddress, int result) {
  lastAddressResult = result;
  if (result == 0) {
    address = beaconAddress ? Address::Beacon : Address::Identity;
  }
}

BleRadioStateMachine::Action BleRadioStateMachine::Step() {
  if (!hostSynced || pendingCommand != Command::None || retryWaiting) {
    return {};
  }

  if (connected) {
    actual = terminationPending ? Mode::Stopping : Mode::Connected;
    if (desired == DesiredMode::Connectable || terminationPending) {
      return {};
    }
    terminationPending = true;
    return Begin(Command::TerminateConnection);
  }

  if (desired == DesiredMode::Off) {
    if (advertising != Advertising::None) {
      return Begin(Command::StopAdvertising);
    }
    if (address == Address::Beacon) {
      if (identityAddressIsRandom) {
        return Begin(Command::RestoreIdentityAddress);
      }
      address = Address::Identity;
    }
    actual = Mode::Off;
    return {};
  }

  if (desired == DesiredMode::Beacon) {
    if (advertising == Advertising::Beacon) {
      actual = Mode::Beacon;
      return {};
    }
    if (advertising != Advertising::None) {
      return Begin(Command::StopAdvertising);
    }
    return Begin(Command::StartBeaconAdvertising);
  }

  if (advertising == Advertising::Beacon) {
    return Begin(Command::StopAdvertising);
  }
  if (advertising == Advertising::Unknown) {
    return Begin(Command::StopAdvertising);
  }
  if (address == Address::Beacon && !identityAddressIsRandom) {
    address = Address::Identity;
  }
  if (advertising == Advertising::Fast) {
    actual = Mode::FastConnectable;
    if (fastTimeoutExpired) {
      startSlowNext = true;
      return Begin(Command::StopAdvertising);
    }
    return {};
  }
  if (advertising == Advertising::Slow) {
    actual = Mode::SlowConnectable;
    if (forceFast) {
      startSlowNext = false;
      return Begin(Command::StopAdvertising);
    }
    return {};
  }

  forceFast = false;
  return Begin(startSlowNext ? Command::StartSlowAdvertising : Command::StartFastAdvertising);
}

void BleRadioStateMachine::Complete(Command command, int result, Result classification) {
  if (command != pendingCommand || command == Command::None) {
    return;
  }

  pendingCommand = Command::None;
  if (IsStart(command)) {
    lastStartResult = result;
  } else if (command == Command::StopAdvertising) {
    lastStopResult = result;
  } else if (command == Command::TerminateConnection) {
    lastTerminateResult = result;
  } else if (command == Command::RestoreIdentityAddress) {
    lastAddressResult = result;
  }

  if (classification == Result::AdvertisingActive) {
    advertising = Advertising::Unknown;
    actual = Mode::Off;
    recoveryCount++;
    if (desired == DesiredMode::Connectable) {
      fastTimeoutExpired = false;
      startSlowNext = false;
      forceFast = true;
    }
    if (retryCount < MaxRetries) {
      retryCount++;
      retryWaiting = true;
    } else {
      recoveryBackoff = true;
      retryWaiting = true;
    }
    return;
  }

  if (classification == Result::Failed) {
    FailCommand(command);
    return;
  }

  switch (command) {
    case Command::StopAdvertising:
      advertising = Advertising::None;
      actual = Mode::Off;
      break;
    case Command::StartFastAdvertising:
      advertising = Advertising::Fast;
      address = Address::Identity;
      actual = Mode::FastConnectable;
      fastTimeoutExpired = false;
      startSlowNext = false;
      forceFast = false;
      ResetRetry();
      break;
    case Command::StartSlowAdvertising:
      advertising = Advertising::Slow;
      address = Address::Identity;
      actual = Mode::SlowConnectable;
      fastTimeoutExpired = false;
      startSlowNext = false;
      forceFast = false;
      ResetRetry();
      break;
    case Command::StartBeaconAdvertising:
      advertising = Advertising::Beacon;
      address = Address::Beacon;
      actual = Mode::Beacon;
      ResetRetry();
      break;
    case Command::TerminateConnection:
      if (classification == Result::AlreadyInactive) {
        ResetDisconnectedState();
      } else {
        actual = Mode::Stopping;
      }
      break;
    case Command::RestoreIdentityAddress:
      address = Address::Identity;
      actual = Mode::Off;
      break;
    case Command::None:
      break;
  }
}

BleRadioStateMachine::DesiredMode BleRadioStateMachine::Desired() const {
  return desired;
}

BleRadioStateMachine::Mode BleRadioStateMachine::Actual() const {
  return actual;
}

BleRadioStateMachine::Command BleRadioStateMachine::PendingCommand() const {
  return pendingCommand;
}

bool BleRadioStateMachine::RetryWaiting() const {
  return retryWaiting;
}

bool BleRadioStateMachine::RecoveryBackoff() const {
  return recoveryBackoff;
}

uint32_t BleRadioStateMachine::RetryDelayMs() const {
  if (!retryWaiting) {
    return 0;
  }
  if (recoveryBackoff) {
    return ExhaustedRetryDelayMs;
  }
  if (retryCount == 0) {
    return 0;
  }
  constexpr uint32_t delays[MaxRetries] = {250, 1000, 4000};
  return delays[retryCount - 1];
}

uint8_t BleRadioStateMachine::RetryCount() const {
  return retryCount;
}

uint32_t BleRadioStateMachine::RecoveryCount() const {
  return recoveryCount;
}

int BleRadioStateMachine::LastStartResult() const {
  return lastStartResult;
}

int BleRadioStateMachine::LastStopResult() const {
  return lastStopResult;
}

int BleRadioStateMachine::LastTerminateResult() const {
  return lastTerminateResult;
}

int BleRadioStateMachine::LastAddressResult() const {
  return lastAddressResult;
}

bool BleRadioStateMachine::BeaconAddressActive() const {
  return address == Address::Beacon;
}

bool BleRadioStateMachine::ExpectsAdvertising() const {
  if (connected) {
    return false;
  }
  if (desired == DesiredMode::Connectable) {
    return advertising == Advertising::Fast || advertising == Advertising::Slow;
  }
  return desired == DesiredMode::Beacon && advertising == Advertising::Beacon;
}

BleRadioStateMachine::Action BleRadioStateMachine::Begin(Command command) {
  pendingCommand = command;
  modeBeforeCommand = actual;
  if (command == Command::StopAdvertising || command == Command::TerminateConnection) {
    actual = Mode::Stopping;
  } else if (IsStart(command)) {
    actual = Mode::Starting;
  }
  return {command};
}

void BleRadioStateMachine::FailCommand(Command command) {
  actual = modeBeforeCommand;
  if (IsStart(command)) {
    advertising = Advertising::None;
    actual = Mode::Off;
    recoveryCount++;
    if (desired == DesiredMode::Connectable) {
      fastTimeoutExpired = false;
      startSlowNext = false;
      forceFast = true;
    }
  } else if (command == Command::TerminateConnection) {
    terminationPending = false;
    actual = Mode::Connected;
  }

  if (retryCount < MaxRetries) {
    retryCount++;
    retryWaiting = true;
  } else {
    recoveryBackoff = true;
    retryWaiting = true;
  }
}

void BleRadioStateMachine::ResetRetry() {
  retryCount = 0;
  retryWaiting = false;
  recoveryBackoff = false;
}

void BleRadioStateMachine::ResetDisconnectedState() {
  connected = false;
  terminationPending = false;
  advertising = Advertising::None;
  pendingCommand = Command::None;
  actual = Mode::Off;
  fastTimeoutExpired = false;
  startSlowNext = false;
  forceFast = desired == DesiredMode::Connectable;
  retryWaiting = false;
}

bool BleRadioStateMachine::IsStart(Command command) {
  return command == Command::StartFastAdvertising || command == Command::StartSlowAdvertising || command == Command::StartBeaconAdvertising;
}
