#include "nimble/nimble_port_freertos.h"

#include <stdint.h>
#include <stdio.h>

#ifndef NIMBLE_PORT_TEST_SCENARIO
  #error NIMBLE_PORT_TEST_SCENARIO must select a fault scenario
#endif

enum {
  ScenarioAllocationFailure = 1,
  ScenarioLlCreationFailure = 2,
  ScenarioHostCreationFailure = 3,
  ScenarioSuccess = 4,
  ScenarioInvalidArgument = 5,
};

static StackType_t allocation[(configMINIMAL_STACK_SIZE + 200) + (configMINIMAL_STACK_SIZE + 600)];
static int allocationCalls;
static int freeCalls;
static int createCalls;
static int suspendSchedulerCalls;
static int resumeSchedulerCalls;
static int deleteCalls;
static int suspendTaskCalls;
static StackType_t* taskStacks[2];
static uint32_t taskDepths[2];
static uint32_t taskPriorities[2];
static int failures;

static void Check(bool condition, const char* description) {
  if (!condition) {
    failures++;
    printf("FAIL: %s\n", description);
  }
}

void* pvPortMalloc(size_t size) {
  allocationCalls++;
  Check(size == sizeof(allocation), "single allocation reserves both stacks");
  if (NIMBLE_PORT_TEST_SCENARIO == ScenarioAllocationFailure) {
    return NULL;
  }
  return allocation;
}

void vPortFree(void* memory) {
  freeCalls++;
  Check(memory == allocation, "failure releases the combined stack block");
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t function,
                               const char* name,
                               uint32_t stackDepth,
                               void* argument,
                               uint32_t priority,
                               StackType_t* stack,
                               StaticTask_t* taskBuffer) {
  (void) function;
  (void) name;
  (void) argument;
  Check(taskBuffer != NULL, "each task has a static TCB");
  taskStacks[createCalls] = stack;
  taskDepths[createCalls] = stackDepth;
  taskPriorities[createCalls] = priority;
  createCalls++;

  if (NIMBLE_PORT_TEST_SCENARIO == ScenarioLlCreationFailure && createCalls == 1) {
    return NULL;
  }
  if (NIMBLE_PORT_TEST_SCENARIO == ScenarioHostCreationFailure && createCalls == 2) {
    return NULL;
  }
  return (TaskHandle_t) (uintptr_t) createCalls;
}

void vTaskSuspendAll(void) {
  suspendSchedulerCalls++;
}

BaseType_t xTaskResumeAll(void) {
  resumeSchedulerCalls++;
  return pdFALSE;
}

void vTaskDelete(TaskHandle_t task) {
  deleteCalls++;
  Check(task == (TaskHandle_t) (uintptr_t) 1, "host creation failure removes the LL task");
}

void vTaskSuspend(TaskHandle_t task) {
  suspendTaskCalls++;
  Check(task == (TaskHandle_t) (uintptr_t) 1 || task == (TaskHandle_t) (uintptr_t) 2, "stop suspends only a created NimBLE task");
}

void nimble_port_ll_task_func(void* argument) {
  (void) argument;
}

static void HostTask(void* argument) {
  (void) argument;
}

int main(void) {
  nimble_port_freertos_result_t result;

  if (NIMBLE_PORT_TEST_SCENARIO == ScenarioInvalidArgument) {
    result = nimble_port_freertos_init(NULL);
    Check(result == NIMBLE_PORT_FREERTOS_INVALID_ARGUMENT, "null host entry point is rejected");
    Check(allocationCalls == 0 && createCalls == 0, "invalid input creates no tasks and allocates nothing");
  } else {
    result = nimble_port_freertos_init(HostTask);
  }

  if (NIMBLE_PORT_TEST_SCENARIO == ScenarioAllocationFailure) {
    Check(result == NIMBLE_PORT_FREERTOS_NO_TASK_MEMORY, "combined stack allocation failure is reported");
    Check(createCalls == 0 && suspendSchedulerCalls == 0, "allocation failure leaves no task and never suspends scheduling");
  } else if (NIMBLE_PORT_TEST_SCENARIO == ScenarioLlCreationFailure) {
    Check(result == NIMBLE_PORT_FREERTOS_TASK_CREATE_FAILED, "LL creation failure is reported");
    Check(createCalls == 1 && freeCalls == 1 && deleteCalls == 0, "LL creation failure leaves no task and releases its stack block");
    Check(suspendSchedulerCalls == 1 && resumeSchedulerCalls == 1, "LL creation failure restores scheduling");
  } else if (NIMBLE_PORT_TEST_SCENARIO == ScenarioHostCreationFailure) {
    Check(result == NIMBLE_PORT_FREERTOS_TASK_CREATE_FAILED, "host creation failure is reported");
    Check(createCalls == 2 && deleteCalls == 1 && freeCalls == 1, "host creation failure deletes LL and releases both stacks");
    Check(suspendSchedulerCalls == 1 && resumeSchedulerCalls == 1, "host creation failure restores scheduling");
  } else if (NIMBLE_PORT_TEST_SCENARIO == ScenarioSuccess) {
    Check(result == NIMBLE_PORT_FREERTOS_OK, "both NimBLE tasks start together");
    Check(createCalls == 2 && freeCalls == 0 && deleteCalls == 0, "successful startup retains both tasks and their stacks");
    Check(taskStacks[0] == allocation && taskStacks[1] == allocation + configMINIMAL_STACK_SIZE + 200,
          "host and LL use disjoint parts of one stack block");
    Check(taskDepths[0] == configMINIMAL_STACK_SIZE + 200 && taskDepths[1] == configMINIMAL_STACK_SIZE + 600,
          "task stack depths preserve the measured firmware sizes");
    Check(taskPriorities[0] == 2 && taskPriorities[1] == 1, "LL remains above the host priority");
    Check(suspendSchedulerCalls == 1 && resumeSchedulerCalls == 1, "neither task can run before both exist");

    result = nimble_port_freertos_init(HostTask);
    Check(result == NIMBLE_PORT_FREERTOS_ALREADY_STARTED, "a second startup cannot duplicate the tasks");
    Check(allocationCalls == 1 && createCalls == 2, "a second startup performs no work");

    nimble_port_freertos_stop();
    Check(suspendTaskCalls == 2, "failure stop suspends both NimBLE tasks");
  }

  printf("scenario %d: %d failures\n", NIMBLE_PORT_TEST_SCENARIO, failures);
  return failures == 0 ? 0 : 1;
}
