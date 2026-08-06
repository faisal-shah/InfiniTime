#include "components/ble/FamilyStateStatus.h"

#include <cstdio>

using Pinetime::Controllers::CompanionProtocol::FamilyStateStorageWarningFlag;
using Pinetime::Controllers::FamilyStateStatus;

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
  FamilyStateStatus status;
  status.state = FamilyStateStatus::StorageState::Failed;
  status.operation = FamilyStateStatus::Operation::Schedule;
  status.error = FamilyStateStatus::Error::Spi;
  status.flags = FamilyStateStorageWarningFlag;
  status.token = 0x01020304;
  status.activeGeneration = 0xa1b2c3d4;
  status.retryCount = 1;

  const auto encoded = status.Encode();
  const uint8_t expected[FamilyStateStatus::Size] = {
    1, 1, 3, 1, 4, 1, 4, 3, 2, 1, 0xd4, 0xc3, 0xb2, 0xa1, 1, 0,
  };
  for (size_t index = 0; index < encoded.size(); index++) {
    Check(encoded[index] == expected[index], "golden status byte");
  }

  FamilyStateStatus decoded;
  Check(FamilyStateStatus::Decode(encoded.data(), encoded.size(), decoded), "golden status decodes");
  Check(decoded.state == status.state, "state round trips");
  Check(decoded.operation == status.operation, "operation round trips");
  Check(decoded.error == status.error, "error round trips");
  Check(decoded.flags == status.flags, "flags round trip");
  Check(decoded.token == status.token, "token round trips");
  Check(decoded.activeGeneration == status.activeGeneration, "generation round trips");
  Check(decoded.retryCount == status.retryCount, "retry count round trips");

  auto invalid = encoded;
  invalid[0]++;
  Check(!FamilyStateStatus::Decode(invalid.data(), invalid.size(), decoded), "wrong protocol rejected");
  invalid = encoded;
  invalid[15] = 1;
  Check(!FamilyStateStatus::Decode(invalid.data(), invalid.size(), decoded), "nonzero reserved byte rejected");
  Check(!FamilyStateStatus::Decode(encoded.data(), encoded.size() - 1, decoded), "wrong length rejected");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
