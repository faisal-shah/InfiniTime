#include "utility/XorShift32.h"

#include <array>
#include <cstdint>
#include <cstdio>

using Pinetime::Utility::XorShift32;

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
  static_assert(sizeof(XorShift32) == sizeof(uint32_t));

  XorShift32 first {1};
  XorShift32 second {1};
  for (int i = 0; i < 100; i++) {
    Check(first.Next() == second.Next(), "equal seeds produce equal sequences");
  }

  XorShift32 zeroSeed {0};
  Check(zeroSeed.Next() != 0, "zero seed is replaced with a nonzero state");

  XorShift32 dice {0x12345678};
  std::array<uint32_t, 6> counts {};
  for (int i = 0; i < 6000; i++) {
    const uint32_t value = dice.Uniform(counts.size());
    Check(value < counts.size(), "bounded result stays in range");
    counts[value]++;
  }
  for (const uint32_t count : counts) {
    Check(count > 800 && count < 1200, "six-sided sample has no gross skew");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
