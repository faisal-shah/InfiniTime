/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define NIMBLE_LL_STACK_WORDS   (configMINIMAL_STACK_SIZE + 200)
#define NIMBLE_HOST_STACK_WORDS (configMINIMAL_STACK_SIZE + 600)

#if NIMBLE_CFG_CONTROLLER
static TaskHandle_t ll_task_h;
static StaticTask_t ll_task_buffer;
#endif
static TaskHandle_t host_task_h;
static StaticTask_t host_task_buffer;
static StackType_t *task_stack_block;
static bool tasks_started;

nimble_port_freertos_result_t
nimble_port_freertos_init(TaskFunction_t host_task_fn)
{
    StackType_t *host_stack;

    if (host_task_fn == NULL) {
        return NIMBLE_PORT_FREERTOS_INVALID_ARGUMENT;
    }
    if (tasks_started) {
        return NIMBLE_PORT_FREERTOS_ALREADY_STARTED;
    }

    task_stack_block = pvPortMalloc(
        (NIMBLE_LL_STACK_WORDS + NIMBLE_HOST_STACK_WORDS) *
        sizeof(StackType_t));
    if (task_stack_block == NULL) {
        return NIMBLE_PORT_FREERTOS_NO_TASK_MEMORY;
    }
    host_stack = task_stack_block + NIMBLE_LL_STACK_WORDS;

    /*
     * Suspend scheduling across both static task creations.  Their stacks are
     * one checked allocation, so the radio either gets both tasks or neither;
     * the higher-priority LL task cannot run between the two creations.
     */
    vTaskSuspendAll();
#if NIMBLE_CFG_CONTROLLER
    /*
     * Create task where NimBLE LL will run. This one is required as LL has its
     * own event queue and should have highest priority. The task function is
     * provided by NimBLE and in case of FreeRTOS it does not need to be wrapped
     * since it has compatible prototype.
     */
    ll_task_h = xTaskCreateStatic(nimble_port_ll_task_func, "ll",
                                 NIMBLE_LL_STACK_WORDS, NULL, 2,
                                 task_stack_block, &ll_task_buffer);
    if (ll_task_h == NULL) {
        xTaskResumeAll();
        vPortFree(task_stack_block);
        task_stack_block = NULL;
        return NIMBLE_PORT_FREERTOS_TASK_CREATE_FAILED;
    }
#endif

    /*
     * Create task where NimBLE host will run. It is not strictly necessary to
     * have separate task for NimBLE host, but since something needs to handle
     * default queue it is just easier to make separate task which does this.
     */
    host_task_h = xTaskCreateStatic(host_task_fn, "ble",
                                   NIMBLE_HOST_STACK_WORDS, NULL, 1,
                                   host_stack, &host_task_buffer);
    if (host_task_h == NULL) {
#if NIMBLE_CFG_CONTROLLER
        vTaskDelete(ll_task_h);
        ll_task_h = NULL;
#endif
        xTaskResumeAll();
        vPortFree(task_stack_block);
        task_stack_block = NULL;
        return NIMBLE_PORT_FREERTOS_TASK_CREATE_FAILED;
    }

    tasks_started = true;
    xTaskResumeAll();
    return NIMBLE_PORT_FREERTOS_OK;
}

void
nimble_port_freertos_stop(void)
{
    if (!tasks_started) {
        return;
    }

#if NIMBLE_CFG_CONTROLLER
    if (ll_task_h != NULL) {
        vTaskSuspend(ll_task_h);
    }
#endif
    if (host_task_h != NULL) {
        vTaskSuspend(host_task_h);
    }
}
