#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef void* TaskHandle_t;

typedef struct TestTimer* TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t timer);

typedef struct {
  uint32_t ICSR;
} SCB_Type;

extern SCB_Type nplTestScb;
void NplTestYieldFromIsr(BaseType_t woken);

#define SCB                         (&nplTestScb)
#define SCB_ICSR_VECTACTIVE_Msk     UINT32_C(0x1ff)
#define configTICK_RATE_HZ          UINT32_C(1024)
#define pdFALSE                     0
#define pdTRUE                      1
#define pdFAIL                      0
#define pdPASS                      1
#define errQUEUE_EMPTY              0
#define portMAX_DELAY               UINT32_MAX
#define taskSCHEDULER_NOT_STARTED   0
#define taskSCHEDULER_RUNNING       1
#define taskSCHEDULER_SUSPENDED     2
#define pdMS_TO_TICKS(milliseconds) ((TickType_t) (((milliseconds) * configTICK_RATE_HZ) / 1000U))
#define portYIELD_FROM_ISR(woken)   NplTestYieldFromIsr(woken)
