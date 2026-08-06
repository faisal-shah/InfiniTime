#include "storagetask/StorageCoordinator.h"

#include <cstdio>

using Pinetime::System::StorageCoordinator;

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
}

int main() {
  StorageCoordinator coordinator;
  using Error = StorageCoordinator::Error;
  using Operation = StorageCoordinator::Operation;
  using State = StorageCoordinator::StorageState;

  coordinator.RecordBoot(4);
  Check(coordinator.GetStatus().state == State::Succeeded, "healthy boot recorded");
  Check(coordinator.GetStatus().activeGeneration == 4, "boot generation recorded");

  Check(coordinator.Begin(Operation::Schedule, 10), "first mutation begins");
  Check(coordinator.Busy(), "pending mutation is busy");
  Check(!coordinator.Begin(Operation::Tasks, 11), "second mutation rejected while busy");
  coordinator.Complete(true, Error::None, 5, 1);
  Check(coordinator.GetStatus().state == State::Succeeded, "success recorded");
  Check(coordinator.GetStatus().operation == Operation::Schedule, "operation retained");
  Check(coordinator.GetStatus().token == 10, "token retained");
  Check(coordinator.GetStatus().activeGeneration == 5, "generation advances on success");
  Check(coordinator.GetStatus().retryCount == 1, "retry evidence retained");

  Check(coordinator.Begin(Operation::Tasks, 12), "next mutation begins after success");
  coordinator.Cancel();
  Check(coordinator.GetStatus().state == State::Idle, "cancel returns to idle");
  Check(coordinator.Begin(Operation::Tasks, 12), "mutation may restart after cancel");
  coordinator.Complete(false, Error::Spi, 5, 1);
  Check(coordinator.GetStatus().state == State::Failed, "failure recorded");
  Check(coordinator.GetStatus().error == Error::Spi, "failure error retained");
  Check((coordinator.GetStatus().flags & Pinetime::Controllers::CompanionProtocol::FamilyStateStorageWarningFlag) != 0,
        "failure latches warning");
  coordinator.AcknowledgeWarning();
  Check(coordinator.GetStatus().flags == 0, "warning acknowledgement clears flag");

  Check(!coordinator.Begin(Operation::None, 13), "none operation rejected");
  Check(!coordinator.Begin(Operation::Schedule, 0), "zero token rejected");

  coordinator.RecordBoot(0, Error::Crc);
  Check(coordinator.GetStatus().state == State::Failed, "corrupt boot is failed");
  Check(coordinator.GetStatus().operation == Operation::BootInitialization, "corrupt boot operation recorded");
  Check(coordinator.GetStatus().error == Error::Crc, "corrupt boot error recorded");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
