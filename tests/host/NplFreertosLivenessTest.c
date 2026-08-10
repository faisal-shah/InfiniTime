#include "nimble/nimble_npl.h"

#include <stdio.h>
#include <string.h>

struct TestTimer {
  void* id;
  TimerCallbackFunction_t callback;
};

SCB_Type nplTestScb;

static int failures;
static BaseType_t queueSendResult = pdPASS;
static BaseType_t timerCommandResult = pdPASS;
static int queueSendCalls;
static int queueSendFromIsrCalls;
static TickType_t lastQueueWait;
static int timerChangeCalls;
static int timerChangeFromIsrCalls;
static int timerStopCalls;
static int timerStopFromIsrCalls;
static TickType_t lastTimerPeriod;
static TickType_t lastTimerWait;
static int yieldCalls;
static int timerDaemonHandleCalls;
static struct TestTimer timerStorage;
static BaseType_t schedulerState = taskSCHEDULER_RUNNING;
static TaskHandle_t currentTask = (TaskHandle_t) (uintptr_t) 2;
static TaskHandle_t timerDaemonTask = (TaskHandle_t) (uintptr_t) 3;

static void Check(bool condition, const char* description) {
  if (!condition) {
    failures++;
    printf("FAIL: %s\n", description);
  }
}

static void ResetObservations(void) {
  queueSendCalls = 0;
  queueSendFromIsrCalls = 0;
  lastQueueWait = portMAX_DELAY;
  timerChangeCalls = 0;
  timerChangeFromIsrCalls = 0;
  timerStopCalls = 0;
  timerStopFromIsrCalls = 0;
  lastTimerPeriod = 0;
  lastTimerWait = portMAX_DELAY;
  yieldCalls = 0;
  timerDaemonHandleCalls = 0;
  nplTestScb.ICSR = 0;
  ble_npl_in_critical = 0;
  queueSendResult = pdPASS;
  timerCommandResult = pdPASS;
  schedulerState = taskSCHEDULER_RUNNING;
  currentTask = (TaskHandle_t) (uintptr_t) 2;
  timerDaemonTask = (TaskHandle_t) (uintptr_t) 3;
}

void NplTestYieldFromIsr(BaseType_t woken) {
  (void) woken;
  yieldCalls++;
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize) {
  (void) length;
  (void) itemSize;
  return (QueueHandle_t) (uintptr_t) 1;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t wait) {
  (void) queue;
  (void) item;
  (void) wait;
  return errQUEUE_EMPTY;
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t queue, void* item, BaseType_t* woken) {
  (void) queue;
  (void) item;
  *woken = pdFALSE;
  return errQUEUE_EMPTY;
}

BaseType_t xQueueSendToBack(QueueHandle_t queue, const void* item, TickType_t wait) {
  (void) queue;
  (void) item;
  queueSendCalls++;
  lastQueueWait = wait;
  return queueSendResult;
}

BaseType_t xQueueSendToBackFromISR(QueueHandle_t queue, const void* item, BaseType_t* woken) {
  (void) queue;
  (void) item;
  queueSendFromIsrCalls++;
  *woken = pdFALSE;
  return queueSendResult;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue) {
  (void) queue;
  return 0;
}

UBaseType_t uxQueueMessagesWaitingFromISR(QueueHandle_t queue) {
  (void) queue;
  return 0;
}

BaseType_t xQueueIsQueueEmptyFromISR(QueueHandle_t queue) {
  (void) queue;
  return pdTRUE;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) {
  return (SemaphoreHandle_t) (uintptr_t) 1;
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t maximum, UBaseType_t initial) {
  (void) maximum;
  (void) initial;
  return (SemaphoreHandle_t) (uintptr_t) 1;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t semaphore, TickType_t wait) {
  (void) semaphore;
  (void) wait;
  return pdPASS;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t semaphore) {
  (void) semaphore;
  return pdPASS;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait) {
  (void) semaphore;
  (void) wait;
  return pdPASS;
}

BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken) {
  (void) semaphore;
  *woken = pdFALSE;
  return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  (void) semaphore;
  return pdPASS;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t* woken) {
  (void) semaphore;
  *woken = pdFALSE;
  return pdPASS;
}

UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t semaphore) {
  (void) semaphore;
  return 0;
}

TimerHandle_t xTimerCreate(const char* name,
                          TickType_t period,
                          UBaseType_t autoReload,
                          void* id,
                          TimerCallbackFunction_t callback) {
  (void) name;
  (void) period;
  (void) autoReload;
  timerStorage.id = id;
  timerStorage.callback = callback;
  return &timerStorage;
}

BaseType_t xTimerChangePeriod(TimerHandle_t timer, TickType_t period, TickType_t wait) {
  (void) timer;
  timerChangeCalls++;
  lastTimerPeriod = period;
  lastTimerWait = wait;
  return timerCommandResult;
}

BaseType_t xTimerChangePeriodFromISR(TimerHandle_t timer, TickType_t period, BaseType_t* woken) {
  (void) timer;
  timerChangeFromIsrCalls++;
  lastTimerPeriod = period;
  *woken = pdFALSE;
  return timerCommandResult;
}

BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait) {
  (void) timer;
  timerStopCalls++;
  lastTimerWait = wait;
  return timerCommandResult;
}

BaseType_t xTimerStopFromISR(TimerHandle_t timer, BaseType_t* woken) {
  (void) timer;
  timerStopFromIsrCalls++;
  *woken = pdFALSE;
  return timerCommandResult;
}

BaseType_t xTimerIsTimerActive(TimerHandle_t timer) {
  (void) timer;
  return pdFALSE;
}

TickType_t xTimerGetExpiryTime(TimerHandle_t timer) {
  (void) timer;
  return 0;
}

TaskHandle_t xTimerGetTimerDaemonTaskHandle(void) {
  timerDaemonHandleCalls++;
  return timerDaemonTask;
}

void* pvTimerGetTimerID(TimerHandle_t timer) {
  return timer->id;
}

BaseType_t xTaskGetSchedulerState(void) {
  return schedulerState;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
  return currentTask;
}

TickType_t xTaskGetTickCountFromISR(void) {
  return 0;
}

void vTaskDelay(TickType_t ticks) {
  (void) ticks;
}

void vPortEnterCritical(void) {
}

void vPortExitCritical(void) {
}

uint32_t npl_freertos_hw_enter_critical(void) {
  return 0;
}

void npl_freertos_hw_exit_critical(uint32_t context) {
  (void) context;
}

static void EventCallback(struct ble_npl_event* event) {
  (void) event;
}

static void TestEventQueueBounds(void) {
  struct ble_npl_eventq queue = {.q = (QueueHandle_t) (uintptr_t) 1};
  struct ble_npl_event event;

  ResetObservations();
  ble_npl_event_init(&event, EventCallback, NULL);
  npl_freertos_eventq_put(&queue, &event);
  Check(queueSendCalls == 1 && queueSendFromIsrCalls == 0, "task event enqueue uses the task queue API");
  Check(lastQueueWait == pdMS_TO_TICKS(10), "task event enqueue is bounded to 10 ms");
  Check(event.queued, "successful event enqueue records queued state");
  npl_freertos_eventq_put(&queue, &event);
  Check(queueSendCalls == 1, "an already queued event is coalesced");

  ResetObservations();
  queueSendResult = pdFAIL;
  ble_npl_event_init(&event, EventCallback, NULL);
  npl_freertos_eventq_put(&queue, &event);
  Check(!event.queued, "a saturated queue leaves the event retryable");
  Check(lastQueueWait != portMAX_DELAY, "a saturated queue cannot wait forever");

  ResetObservations();
  queueSendResult = pdFAIL;
  ble_npl_in_critical = 1;
  ble_npl_event_init(&event, EventCallback, NULL);
  npl_freertos_eventq_put(&queue, &event);
  Check(lastQueueWait == 0, "critical-section event enqueue is non-blocking");
  Check(!event.queued, "failed critical-section enqueue remains retryable");

  ResetObservations();
  schedulerState = taskSCHEDULER_NOT_STARTED;
  ble_npl_event_init(&event, EventCallback, NULL);
  npl_freertos_eventq_put(&queue, &event);
  Check(lastQueueWait == 0, "pre-scheduler event enqueue is non-blocking");
  Check(timerDaemonHandleCalls == 0, "pre-scheduler enqueue does not query an uncreated timer task");

  ResetObservations();
  schedulerState = taskSCHEDULER_SUSPENDED;
  ble_npl_event_init(&event, EventCallback, NULL);
  npl_freertos_eventq_put(&queue, &event);
  Check(lastQueueWait == 0, "scheduler-suspended event enqueue is non-blocking");
  Check(timerDaemonHandleCalls == 0, "scheduler-suspended enqueue does not query the timer task");

  ResetObservations();
  currentTask = timerDaemonTask;
  ble_npl_event_init(&event, EventCallback, NULL);
  npl_freertos_eventq_put(&queue, &event);
  Check(lastQueueWait == 0, "timer-daemon event enqueue is non-blocking");

  ResetObservations();
  queueSendResult = pdFAIL;
  nplTestScb.ICSR = 1;
  ble_npl_event_init(&event, EventCallback, NULL);
  npl_freertos_eventq_put(&queue, &event);
  Check(queueSendCalls == 0 && queueSendFromIsrCalls == 1, "ISR event enqueue uses the ISR queue API");
  Check(!event.queued, "failed ISR enqueue remains retryable");
  Check(yieldCalls == 1, "ISR event enqueue performs the requested yield check");
}

static void TestCalloutCommands(void) {
  struct ble_npl_callout callout = {.handle = &timerStorage};

  ResetObservations();
  Check(npl_freertos_callout_reset(&callout, 0) == BLE_NPL_OK, "zero-tick callout reset succeeds");
  Check(timerChangeCalls == 1 && timerChangeFromIsrCalls == 0, "task reset emits one change-period command");
  Check(lastTimerPeriod == 1, "zero-tick callout reset is normalized to one tick");
  Check(lastTimerWait == pdMS_TO_TICKS(10), "task timer command is bounded to 10 ms");

  ResetObservations();
  timerCommandResult = pdFAIL;
  Check(npl_freertos_callout_reset(&callout, 7) == BLE_NPL_ERROR, "timer-command saturation is reported");
  Check(timerChangeCalls == 1 && lastTimerWait != portMAX_DELAY, "failed reset emits one bounded command");

  ResetObservations();
  ble_npl_in_critical = 1;
  Check(npl_freertos_callout_reset(&callout, 7) == BLE_NPL_OK, "critical-section reset can enqueue immediately");
  Check(timerChangeCalls == 1 && lastTimerWait == 0, "critical-section timer command is non-blocking");

  ResetObservations();
  schedulerState = taskSCHEDULER_NOT_STARTED;
  Check(npl_freertos_callout_reset(&callout, 7) == BLE_NPL_OK, "pre-scheduler reset can enqueue immediately");
  Check(timerChangeCalls == 1 && lastTimerWait == 0, "pre-scheduler timer command is non-blocking");

  ResetObservations();
  currentTask = timerDaemonTask;
  Check(npl_freertos_callout_reset(&callout, 7) == BLE_NPL_OK, "timer-daemon reset can enqueue immediately");
  Check(timerChangeCalls == 1 && lastTimerWait == 0, "timer-daemon command is non-blocking");

  ResetObservations();
  nplTestScb.ICSR = 1;
  Check(npl_freertos_callout_reset(&callout, 7) == BLE_NPL_OK, "ISR reset succeeds");
  Check(timerChangeCalls == 0 && timerChangeFromIsrCalls == 1, "ISR reset emits one ISR change-period command");
  Check(yieldCalls == 1, "ISR timer command performs the requested yield check");

  ResetObservations();
  ble_npl_callout_stop(&callout);
  Check(timerStopCalls == 1 && lastTimerWait == pdMS_TO_TICKS(10), "callout stop is bounded to 10 ms");

  ResetObservations();
  ble_npl_in_critical = 1;
  ble_npl_callout_stop(&callout);
  Check(timerStopCalls == 1 && lastTimerWait == 0, "critical-section callout stop is non-blocking");

  ResetObservations();
  nplTestScb.ICSR = 1;
  Check(npl_freertos_callout_stop(&callout) == BLE_NPL_OK, "ISR callout stop succeeds");
  Check(timerStopCalls == 0 && timerStopFromIsrCalls == 1, "ISR stop uses the ISR timer API");
  Check(yieldCalls == 1, "ISR stop performs the requested yield check");

  ResetObservations();
  timerCommandResult = pdFAIL;
  Check(npl_freertos_callout_stop(&callout) == BLE_NPL_ERROR, "stop reports timer-command saturation");
}

static void TestTimerCallbackBound(void) {
  struct ble_npl_eventq queue = {.q = (QueueHandle_t) (uintptr_t) 1};
  struct ble_npl_callout callout = {0};

  ResetObservations();
  Check(npl_freertos_callout_init(&callout, &queue, EventCallback, NULL) == BLE_NPL_OK,
        "callout initialization captures a timer callback");
  Check(timerStorage.callback != NULL, "callout initialization supplies a timer callback");

  queueSendResult = pdFAIL;
  currentTask = timerDaemonTask;
  timerStorage.callback(&timerStorage);
  Check(queueSendCalls == 1, "expired callout attempts one event enqueue");
  Check(lastQueueWait == 0, "timer-daemon event enqueue is non-blocking");
  Check(!callout.ev.queued, "failed callout enqueue leaves its event retryable");
  Check(timerChangeCalls == 1 && lastTimerPeriod == 1 && lastTimerWait == 0,
        "failed callout enqueue rearms a one-tick non-blocking retry");

  ResetObservations();
  currentTask = timerDaemonTask;
  timerStorage.callback(&timerStorage);
  Check(queueSendCalls == 1 && callout.ev.queued, "successful callout enqueue publishes the event");
  Check(timerChangeCalls == 0, "successful callout enqueue needs no retry command");
}

int main(void) {
  TestEventQueueBounds();
  TestCalloutCommands();
  TestTimerCallbackBound();

  printf("%d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
