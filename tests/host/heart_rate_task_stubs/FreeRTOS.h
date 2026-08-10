#pragma once

#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using TaskHandle_t = void*;
using QueueHandle_t = void*;
using TaskFunction_t = void (*)(void*);

#define configTICK_RATE_HZ 100
#define portMAX_DELAY      UINT32_MAX
#define pdFALSE            0
#define pdTRUE             1
#define pdFAIL             0
#define pdPASS             1
