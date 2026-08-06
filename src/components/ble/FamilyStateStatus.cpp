#include "components/ble/FamilyStateStatus.h"

using Pinetime::Controllers::CompanionProtocol::FamilyStateProtocolVersion;
using Pinetime::Controllers::CompanionProtocol::FamilyStateSnapshotSchemaVersion;
using Pinetime::Controllers::FamilyStateStatus;

namespace {
  void WriteU32Le(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
  }

  uint32_t ReadU32Le(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           (static_cast<uint32_t>(input[1]) << 8) |
           (static_cast<uint32_t>(input[2]) << 16) |
           (static_cast<uint32_t>(input[3]) << 24);
  }
}

std::array<uint8_t, FamilyStateStatus::Size> FamilyStateStatus::Encode() const {
  std::array<uint8_t, Size> output {};
  output[0] = FamilyStateProtocolVersion;
  output[1] = FamilyStateSnapshotSchemaVersion;
  output[2] = static_cast<uint8_t>(state);
  output[3] = static_cast<uint8_t>(operation);
  output[4] = static_cast<uint8_t>(error);
  output[5] = flags;
  WriteU32Le(&output[6], token);
  WriteU32Le(&output[10], activeGeneration);
  output[14] = retryCount;
  return output;
}

bool FamilyStateStatus::Decode(const uint8_t* data, size_t size, FamilyStateStatus& output) {
  if (data == nullptr || size != Size ||
      data[0] != FamilyStateProtocolVersion ||
      data[1] != FamilyStateSnapshotSchemaVersion ||
      data[15] != 0) {
    return false;
  }

  if (data[2] > static_cast<uint8_t>(StorageState::Failed) ||
      data[3] > static_cast<uint8_t>(Operation::BootInitialization) ||
      data[4] > static_cast<uint8_t>(Error::Unsupported) ||
      (data[5] & ~CompanionProtocol::FamilyStateStorageWarningFlag) != 0) {
    return false;
  }

  output.state = static_cast<StorageState>(data[2]);
  output.operation = static_cast<Operation>(data[3]);
  output.error = static_cast<Error>(data[4]);
  output.flags = data[5];
  output.token = ReadU32Le(&data[6]);
  output.activeGeneration = ReadU32Le(&data[10]);
  output.retryCount = data[14];
  return true;
}
