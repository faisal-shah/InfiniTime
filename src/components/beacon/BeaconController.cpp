#include "components/beacon/BeaconController.h"
#include "components/beacon/BeaconRules.h"
#include "components/fs/Crc32.h"
#include "storagetask/StorageTask.h"
#include <cstring>
#include <libraries/log/nrf_log.h>

using namespace Pinetime::Controllers;

BeaconController::BeaconController(System::StorageTask& storageTask)
  : storageTask {storageTask} {
}

const FamilyState& BeaconController::Active() const {
  return storageTask.ActiveState();
}

bool BeaconController::HasKey() const {
  return Active().findMyKeyPresent;
}

uint32_t BeaconController::MutationToken(const uint8_t key[KeySize]) {
  const uint32_t token = Crc32::Compute(key, KeySize);
  return token == 0 ? 1 : token;
}

bool BeaconController::StageKey(const uint8_t key[KeySize]) {
  const uint32_t token = MutationToken(key);
  if (!storageTask.BeginFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::BeaconKey,
        token)) {
    return false;
  }
  auto* candidate = storageTask.MutableCandidate(
    CompanionProtocol::FamilyStateOperation::BeaconKey,
    token);
  if (candidate == nullptr) {
    storageTask.CancelFamilyStateMutation(
      CompanionProtocol::FamilyStateOperation::BeaconKey,
      token);
    return false;
  }
  candidate->findMyKeyPresent = true;
  std::memcpy(candidate->findMyKey.data(), key, KeySize);
  std::memcpy(stagedKey, key, KeySize);
  stagedValid = true;
  pendingToken = token;
  return true;
}

void BeaconController::CommitStagedKey() {
  if (!stagedValid || pendingToken != MutationToken(stagedKey)) {
    if (pendingToken != 0) {
      storageTask.CancelFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::BeaconKey,
        pendingToken);
    }
    stagedValid = false;
    pendingToken = 0;
    return;
  }
  stagedValid = false;
  if (!storageTask.CommitFamilyStateMutation(
        CompanionProtocol::FamilyStateOperation::BeaconKey,
        pendingToken)) {
    pendingToken = 0;
  }
}

void BeaconController::OnPersisted(uint32_t token, bool success) {
  if (token != pendingToken) {
    return;
  }
  pendingToken = 0;
  if (success) {
    NRF_LOG_INFO("[BeaconController] Advertisement key committed");
  }
}

void BeaconController::BuildAddress(uint8_t out[6]) const {
  BeaconRules::BuildAddress(Active().findMyKey.data(), out);
}

void BeaconController::BuildPayload(uint8_t out[31]) const {
  BeaconRules::BuildPayload(Active().findMyKey.data(), out);
}
