#pragma once

#include <FreeRTOS.h>
#include <timers.h>
#include <nrfx_gpiote.h>

#include "components/ble/NimbleStartup.h"

void nrfx_gpiote_evt_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action);
void DebounceTimerCallback(TimerHandle_t xTimer);

extern int mallocFailedCount;
extern int stackOverflowCount;
extern uint16_t NoInit_AdvRecoveries;

namespace Pinetime::Controllers {
  [[nodiscard]] bool NimblePortInit();
  [[nodiscard]] bool NimblePortStart();
  NimblePortError NimblePortGetError();
  NimbleHostState NimblePortGetHostState();
  int NimblePortGetHostError();
}
