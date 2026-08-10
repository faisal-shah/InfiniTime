#pragma once

#include "FreeRTOS.h"

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t maximum, UBaseType_t initial);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t wait);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait);
BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken);
UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t semaphore);

