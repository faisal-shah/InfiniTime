#pragma once

#include <cstdint>

#include "host/ble_uuid.h"

struct ble_gatt_access_ctxt;

using ble_gatt_access_fn = int(uint16_t, uint16_t, struct ble_gatt_access_ctxt*, void*);

struct ble_gatt_chr_def {
  const ble_uuid_t* uuid = nullptr;
  ble_gatt_access_fn* access_cb = nullptr;
  void* arg = nullptr;
  uint16_t flags = 0;
  uint16_t* val_handle = nullptr;
};

struct ble_gatt_svc_def {
  uint8_t type = 0;
  const ble_uuid_t* uuid = nullptr;
  const struct ble_gatt_chr_def* characteristics = nullptr;
};

#define BLE_GATT_CHR_F_WRITE      1
#define BLE_GATT_SVC_TYPE_PRIMARY 1
