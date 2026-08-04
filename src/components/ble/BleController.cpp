#include "components/ble/BleController.h"
#include "main.h"

using namespace Pinetime::Controllers;

bool Ble::IsConnected() const {
  return isConnected;
}

void Ble::Connect() {
  isConnected = true;
}

void Ble::Disconnect() {
  isConnected = false;
}

bool Ble::IsRadioEnabled() const {
  return isRadioEnabled;
}

void Ble::EnableRadio() {
  isRadioEnabled = true;
}

void Ble::DisableRadio() {
  isRadioEnabled = false;
}

void Ble::StartFirmwareUpdate() {
  isFirmwareUpdating = true;
}

void Ble::StopFirmwareUpdate() {
  isFirmwareUpdating = false;
}

void Ble::FirmwareUpdateTotalBytes(uint32_t totalBytes) {
  firmwareUpdateTotalBytes = totalBytes;
}

void Ble::FirmwareUpdateCurrentBytes(uint32_t currentBytes) {
  firmwareUpdateCurrentBytes = currentBytes;
}

void Ble::RecordAdvertisingRecovery() {
  if (NoInit_AdvRecoveries < UINT16_MAX) {
    NoInit_AdvRecoveries++;
  }
}

uint16_t Ble::AdvertisingRecoveries() const {
  return NoInit_AdvRecoveries;
}

void Ble::RadioDiagnostics(BleRadioStateMachine::DesiredMode desired,
                           BleRadioStateMachine::Mode actual,
                           int lastStartResult,
                           int lastStopResult,
                           int lastTerminateResult,
                           uint8_t retryCount) {
  radioDesiredMode = desired;
  radioActualMode = actual;
  radioLastStartResult = lastStartResult;
  radioLastStopResult = lastStopResult;
  radioLastTerminateResult = lastTerminateResult;
  radioRetryCount = retryCount;
}

BleRadioStateMachine::DesiredMode Ble::RadioDesiredMode() const {
  return radioDesiredMode;
}

BleRadioStateMachine::Mode Ble::RadioActualMode() const {
  return radioActualMode;
}

int Ble::RadioLastStartResult() const {
  return radioLastStartResult;
}

int Ble::RadioLastStopResult() const {
  return radioLastStopResult;
}

int Ble::RadioLastTerminateResult() const {
  return radioLastTerminateResult;
}

uint8_t Ble::RadioRetryCount() const {
  return radioRetryCount;
}

void Ble::BondDiagnostics(const BondPersistenceCoordinator::Diagnostics& diagnostics) {
  bondDiagnostics = diagnostics;
}

const BondPersistenceCoordinator::Diagnostics& Ble::BondDiagnostics() const {
  return bondDiagnostics;
}

void Ble::CompanionStatus(const CompanionManagementStatus& status) {
  companionStatus = status;
}

const CompanionManagementStatus& Ble::CompanionStatus() const {
  return companionStatus;
}
