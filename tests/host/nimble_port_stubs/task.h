#pragma once

#include "FreeRTOS.h"

TaskHandle_t xTaskCreateStatic(TaskFunction_t function,
                               const char* name,
                               uint32_t stackDepth,
                               void* argument,
                               uint32_t priority,
                               StackType_t* stack,
                               StaticTask_t* taskBuffer);
void vTaskSuspendAll(void);
BaseType_t xTaskResumeAll(void);
void vTaskDelete(TaskHandle_t task);
void vTaskSuspend(TaskHandle_t task);
