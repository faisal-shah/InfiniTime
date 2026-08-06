#pragma once

#include "components/ble/FamilyStateStatus.h"

#include <cstdint>

namespace Pinetime::System {
  class StorageCoordinator {
  public:
    using Status = Controllers::FamilyStateStatus;
    using StorageState = Status::StorageState;
    using Operation = Status::Operation;
    using Error = Status::Error;

    bool Begin(Operation operation, uint32_t token) {
      if (status.state == StorageState::Pending ||
          operation == Operation::None ||
          token == 0) {
        return false;
      }
      status.state = StorageState::Pending;
      status.operation = operation;
      status.error = Error::None;
      status.token = token;
      status.retryCount = 0;
      return true;
    }

    void Complete(bool success, Error error, uint32_t activeGeneration, uint8_t retryCount) {
      if (status.state != StorageState::Pending) {
        return;
      }

      status.state = success ? StorageState::Succeeded : StorageState::Failed;
      status.error = success ? Error::None : error;
      status.activeGeneration = activeGeneration;
      status.retryCount = retryCount;
      if (success) {
        status.flags &= ~Controllers::CompanionProtocol::FamilyStateStorageWarningFlag;
      } else {
        status.flags |= Controllers::CompanionProtocol::FamilyStateStorageWarningFlag;
      }
    }

    void Cancel() {
      if (status.state != StorageState::Pending) {
        return;
      }
      status.state = StorageState::Idle;
      status.operation = Operation::None;
      status.error = Error::None;
      status.token = 0;
      status.retryCount = 0;
    }

    void RecordBoot(uint32_t activeGeneration, Error error = Error::None) {
      status.state = error == Error::None ? StorageState::Succeeded : StorageState::Failed;
      status.operation = Operation::BootInitialization;
      status.error = error;
      status.token = 0;
      status.activeGeneration = activeGeneration;
      status.retryCount = 0;
      if (error != Error::None) {
        status.flags |= Controllers::CompanionProtocol::FamilyStateStorageWarningFlag;
      }
    }

    void AcknowledgeWarning() {
      status.flags &= ~Controllers::CompanionProtocol::FamilyStateStorageWarningFlag;
    }

    bool Busy() const {
      return status.state == StorageState::Pending;
    }

    const Status& GetStatus() const {
      return status;
    }

  private:
    Status status;
  };
}
