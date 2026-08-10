#pragma once

#include "FreeRTOS.h"

TimerHandle_t xTimerCreate(const char* name,
                          TickType_t period,
                          UBaseType_t autoReload,
                          void* id,
                          TimerCallbackFunction_t callback);
BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t wait);
BaseType_t xTimerChangePeriodFromISR(TimerHandle_t timer, TickType_t period, BaseType_t* woken);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerStopFromISR(TimerHandle_t timer, BaseType_t* woken);
BaseType_t xTimerIsTimerActive(TimerHandle_t timer);
TickType_t xTimerGetExpiryTime(TimerHandle_t timer);
TaskHandle_t xTimerGetTimerDaemonTaskHandle(void);
void* pvTimerGetTimerID(TimerHandle_t timer);
