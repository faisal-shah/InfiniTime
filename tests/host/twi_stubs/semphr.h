#pragma once

#include "FreeRTOS.h"

using SemaphoreHandle_t = StaticSemaphore_t*;

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* storage);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
