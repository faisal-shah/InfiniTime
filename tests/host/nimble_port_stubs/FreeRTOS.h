#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t StackType_t;
typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;

typedef struct {
  uintptr_t opaque[8];
} StaticTask_t;

#define configMINIMAL_STACK_SIZE 120
#define pdFALSE                  0
#define pdTRUE                   1

void* pvPortMalloc(size_t size);
void vPortFree(void* memory);
