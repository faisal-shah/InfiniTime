#pragma once

#include <cstdint>

#define BLE_UUID_TYPE_128 128

struct ble_uuid {
  uint8_t type;
};

using ble_uuid_t = struct ble_uuid;

struct ble_uuid128_t {
  ble_uuid_t u;
  uint8_t value[16];
};
