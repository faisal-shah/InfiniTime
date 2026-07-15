// Host-side unit tests for BeaconRules.h — the pure FindMy advertisement
// construction. Runs on the build machine, no firmware or simulator needed:
//
//   g++ -std=c++20 -o /tmp/beacon_rules_test src/components/beacon/BeaconRulesTest.cpp && /tmp/beacon_rules_test
//
// The golden vectors are frozen; the same numbers back the plan and can be
// recomputed from the openhaystack format. Vector B exercises the |0xC0 and
// >>6 edges and the reversed little-endian address.

#include "BeaconRules.h"
#include <cstdint>
#include <cstdio>

using namespace Pinetime::Controllers::BeaconRules;

namespace {
  int failures = 0;
  int checks = 0;

  void checkBytes(const uint8_t* got, const uint8_t* want, size_t n, const char* what) {
    checks++;
    for (size_t i = 0; i < n; i++) {
      if (got[i] != want[i]) {
        failures++;
        printf("FAIL: %s: byte %zu got %02x want %02x\n", what, i, got[i], want[i]);
        return;
      }
    }
  }
}

int main() {
  // Vector A: advKey = 00 01 02 ... 1b
  {
    uint8_t advKey[28];
    for (int i = 0; i < 28; i++) {
      advKey[i] = static_cast<uint8_t>(i);
    }
    const uint8_t wantAddr[6] = {0x05, 0x04, 0x03, 0x02, 0x01, 0xc0};
    const uint8_t wantPayload[31] = {0x1e, 0xff, 0x4c, 0x00, 0x12, 0x19, 0x00, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
                                     0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x00, 0x00};
    uint8_t addr[6];
    uint8_t payload[31];
    BuildAddress(advKey, addr);
    BuildPayload(advKey, payload);
    checkBytes(addr, wantAddr, 6, "vector A address");
    checkBytes(payload, wantPayload, 31, "vector A payload");
  }

  // Vector B: advKey = e5 11 22 33 44 55 06 07 ... 1b
  {
    uint8_t advKey[28] = {0xe5, 0x11, 0x22, 0x33, 0x44, 0x55};
    for (int i = 6; i < 28; i++) {
      advKey[i] = static_cast<uint8_t>(i);
    }
    const uint8_t wantAddr[6] = {0x55, 0x44, 0x33, 0x22, 0x11, 0xe5};
    const uint8_t wantPayload[31] = {0x1e, 0xff, 0x4c, 0x00, 0x12, 0x19, 0x00, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
                                     0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x03, 0x00};
    uint8_t addr[6];
    uint8_t payload[31];
    BuildAddress(advKey, addr);
    BuildPayload(advKey, payload);
    checkBytes(addr, wantAddr, 6, "vector B address");
    checkBytes(payload, wantPayload, 31, "vector B payload");
  }

  // Structural: |0xC0 always sets the top two bits of the address MSB.
  {
    uint8_t advKey[28] = {0x05};
    uint8_t addr[6];
    BuildAddress(advKey, addr);
    checks++;
    if ((addr[5] & 0xC0) != 0xC0) {
      failures++;
      printf("FAIL: address MSB not static-random (got %02x)\n", addr[5]);
    }
  }

  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
