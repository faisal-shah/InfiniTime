#pragma once

#include <cstdint>

#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_uuid.h>
#undef max
#undef min

namespace Pinetime {
  namespace Controllers {
    // 128-bit UUID 00ssyyxx-78fc-48fe-8e23-433b3a1942d0 for this fork's
    // companion services. `serviceByte` (ss) selects the service — 0x06
    // schedule, 0x07 prayer, 0x08 beacon, 0x09 multi-alarm, 0x0a task — and
    // x/y select the characteristic (0x00,0x00 is the service UUID itself).
    constexpr ble_uuid128_t CustomCharUuid(uint8_t serviceByte, uint8_t x, uint8_t y) {
      return ble_uuid128_t {.u = {.type = BLE_UUID_TYPE_128},
                            .value = {0xd0, 0x42, 0x19, 0x3a, 0x3b, 0x43, 0x23, 0x8e, 0xfe, 0x48, 0xfc, 0x78, y, x, serviceByte, 0x00}};
    }
  }
}
