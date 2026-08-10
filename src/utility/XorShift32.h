#pragma once

#include <cstdint>

namespace Pinetime::Utility {
  // A four-byte pseudo-random generator for non-cryptographic UI features.
  // std::mt19937 retains 2.5 KiB of state, enough to exhaust the watch heap
  // merely by opening Dice while BLE and the heart-rate task are resident.
  class XorShift32 {
  public:
    explicit XorShift32(uint32_t seed) : state {seed == 0 ? DefaultSeed : seed} {
    }

    uint32_t Next() {
      uint32_t value = state;
      value ^= value << 13;
      value ^= value >> 17;
      value ^= value << 5;
      state = value;
      return value;
    }

    // Uniform in [0, upperExclusive). Rejection avoids modulo bias for dice
    // whose number of sides does not divide the generator's 2^32 period.
    uint32_t Uniform(uint32_t upperExclusive) {
      const uint32_t threshold = static_cast<uint32_t>(-upperExclusive) % upperExclusive;
      uint32_t value;
      do {
        value = Next();
      } while (value < threshold);
      return value % upperExclusive;
    }

  private:
    static constexpr uint32_t DefaultSeed = 0x9e3779b9;
    uint32_t state;
  };
}
