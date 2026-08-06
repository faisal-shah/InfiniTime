#include "components/motion/StepRecoveryState.h"

#include <cstdio>

using Pinetime::Controllers::StepRecoveryState;

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
  constexpr auto state = StepRecoveryState::Capture(20260806, 4321);
  static_assert(state.Valid());
  Check(state.Restore(20260806).value_or(0) == 4321, "same-day total restores");
  Check(!state.Restore(20260807).has_value(), "different day does not restore");

  auto corrupt = state;
  corrupt.total++;
  Check(!corrupt.Valid(), "corrupt total rejected");
  corrupt = state;
  corrupt.check ^= 1;
  Check(!corrupt.Valid(), "corrupt check rejected");
  corrupt = state;
  corrupt.magic = 0;
  Check(!corrupt.Valid(), "cold-boot record rejected");

  auto cleared = state;
  cleared.Clear(20260807);
  Check(cleared.Valid(), "midnight reset record remains valid");
  Check(cleared.dayKey == 20260807 && cleared.total == 0, "midnight reset advances day and clears total");

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
