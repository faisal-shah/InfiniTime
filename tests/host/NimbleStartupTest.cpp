#include "components/ble/NimbleStartup.h"

#include <cstdint>
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
  Check(!NimbleStartupTimedOut<uint32_t>(100, 119, 20), "startup remains inside its timeout");
  Check(NimbleStartupTimedOut<uint32_t>(100, 120, 20), "startup expires exactly at its timeout");

  constexpr uint32_t nearWrap = 0xfffffff0u;
  Check(!NimbleStartupTimedOut<uint32_t>(nearWrap, nearWrap + 19u, 20), "startup timeout is wrap-safe before expiry");
  Check(NimbleStartupTimedOut<uint32_t>(nearWrap, nearWrap + 20u, 20), "startup timeout is wrap-safe at expiry");
  Check(NimbleStartupTimedOut<uint32_t>(nearWrap, 5u, 20), "startup timeout is wrap-safe after zero");

  Check(NimblePortError::None != NimblePortError::HostStartFailed, "host failure is distinguishable from success");
  Check(NimbleHostState::Starting != NimbleHostState::Running, "starting is distinguishable from running");
  Check(NimbleHostState::Failed != NimbleHostState::Running, "failure is distinguishable from running");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
