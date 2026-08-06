#pragma once

#include "components/ble/generated/CompanionProtocol.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {
  struct FamilyStateStatus {
    using StorageState = CompanionProtocol::FamilyStateStorageState;
    using Operation = CompanionProtocol::FamilyStateOperation;
    using Error = CompanionProtocol::FamilyStateError;

    static constexpr size_t Size = CompanionProtocol::FamilyStateStatusSize;

    StorageState state = StorageState::Idle;
    Operation operation = Operation::None;
    Error error = Error::None;
    uint8_t flags = 0;
    uint32_t token = 0;
    uint32_t activeGeneration = 0;
    uint8_t retryCount = 0;

    std::array<uint8_t, Size> Encode() const;
    static bool Decode(const uint8_t* data, size_t size, FamilyStateStatus& output);
  };
}
