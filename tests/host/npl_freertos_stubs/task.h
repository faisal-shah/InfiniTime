#pragma once

#include "FreeRTOS.h"

BaseType_t xTaskGetSchedulerState(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCountFromISR(void);
void vTaskDelay(TickType_t ticks);
void vPortEnterCritical(void);
void vPortExitCritical(void);

