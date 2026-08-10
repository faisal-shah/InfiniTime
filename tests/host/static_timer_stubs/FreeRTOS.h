#pragma once

#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;

struct StaticTimer_t {
  uintptr_t opaque[8];
};

using TimerHandle_t = void*;
using TimerCallbackFunction_t = void (*)(TimerHandle_t);

#define pdFALSE 0
#define pdTRUE  1
#define pdFAIL  0
#define pdPASS  1
