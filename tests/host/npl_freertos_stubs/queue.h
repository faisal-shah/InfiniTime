#pragma once

#include "FreeRTOS.h"

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait);
BaseType_t xQueueReceiveFromISR(QueueHandle_t queue, void* item, BaseType_t* woken);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t wait);
BaseType_t xQueueSendToBackFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
UBaseType_t uxQueueMessagesWaitingFromISR(QueueHandle_t queue);
BaseType_t xQueueIsQueueEmptyFromISR(QueueHandle_t queue);

