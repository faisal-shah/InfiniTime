#pragma once

#include "FreeRTOS.h"

BaseType_t
xTaskCreate(TaskFunction_t function, const char* name, uint16_t stackDepth, void* argument, UBaseType_t priority, TaskHandle_t* handle);
TickType_t xTaskGetTickCount();
void vTaskDelay(TickType_t ticks);
