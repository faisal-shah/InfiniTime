#pragma once

#include "FreeRTOS.h"

TimerHandle_t xTimerCreateStatic(const char* name,
                                 TickType_t period,
                                 UBaseType_t autoReload,
                                 void* id,
                                 TimerCallbackFunction_t callback,
                                 StaticTimer_t* storage);
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerReset(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t wait);
