#include "components/ble/generated/CompanionProtocol.h"

#include <cstdio>

using namespace Pinetime::Controllers;

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
  Check(CompanionProtocol::ActiveConnections == 1, "one active BLE connection");
  Check(CompanionProtocol::RetainedPeers == 5, "five retained peers");
  Check(CompanionProtocol::ResolvingListEntries >= CompanionProtocol::RetainedPeers,
        "resolving list covers retained peers");
  Check(CompanionProtocol::PersistedNotifyCharacteristics == 8, "eight persisted notify characteristics");
  Check(CompanionProtocol::MaxCccds ==
          CompanionProtocol::RetainedPeers * CompanionProtocol::PersistedNotifyCharacteristics,
        "CCCD capacity is derived from peers and characteristics");
  Check(CompanionProtocol::ScheduleRecordVersion == 2, "schedule record version");
  Check(CompanionProtocol::ScheduleRecordSize == 43, "schedule record size");
  Check(CompanionProtocol::TaskRecordVersion == 1, "task record version");
  Check(CompanionProtocol::TaskRecordSize == 31, "task record size");
  Check(CompanionProtocol::CompanionManagementProtocolVersion == 1, "companion management protocol version");
  Check(CompanionProtocol::CompanionManagementStatusSize == 20, "companion management status size");
  Check(CompanionProtocol::CompanionManagementLruPolicy == 1, "companion management LRU policy code");
  Check(static_cast<uint8_t>(CompanionProtocol::BridgeChar::ScheduleSync) == 0, "first bridge ID");
  Check(static_cast<uint8_t>(CompanionProtocol::BridgeChar::CompanionStatus) == 33, "companion status bridge ID");
  Check(static_cast<uint8_t>(CompanionProtocol::BridgeChar::CompanionVerify) == 34, "last bridge ID");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
