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

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "nimble/nimble_npl.h"

volatile int ble_npl_in_critical = 0;
static const TickType_t npl_command_timeout = pdMS_TO_TICKS(10);
static volatile npl_freertos_alloc_failure_t alloc_failure =
    NPL_FREERTOS_ALLOC_NONE;

static void
record_alloc_failure(npl_freertos_alloc_failure_t failure)
{
    if (alloc_failure == NPL_FREERTOS_ALLOC_NONE) {
        alloc_failure = failure;
    }
}

static TickType_t
command_timeout(void)
{
    /* A positive block time cannot expire with PRIMASK set, and the timer
     * daemon must never block while trying to enqueue work for itself. */
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
        ble_npl_hw_is_in_critical() ||
        xTaskGetCurrentTaskHandle() == xTimerGetTimerDaemonTaskHandle()) {
        return 0;
    }
    return npl_command_timeout;
}

void
npl_freertos_reset_alloc_failure(void)
{
    alloc_failure = NPL_FREERTOS_ALLOC_NONE;
}

npl_freertos_alloc_failure_t
npl_freertos_get_alloc_failure(void)
{
    return alloc_failure;
}

ble_npl_error_t
npl_freertos_eventq_init(struct ble_npl_eventq *evq)
{
    if (evq == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }

    evq->q = xQueueCreate(32, sizeof(struct ble_npl_event *));
    if (evq->q == NULL) {
        record_alloc_failure(NPL_FREERTOS_ALLOC_EVENT_QUEUE);
        return BLE_NPL_ENOMEM;
    }

    return BLE_NPL_OK;
}

static inline bool
in_isr(void)
{
    /* XXX hw specific! */
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk) != 0;
}

struct ble_npl_event *
npl_freertos_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t tmo)
{
    struct ble_npl_event *ev = NULL;
    BaseType_t woken = pdFALSE;
    BaseType_t ret;

    if (evq == NULL || evq->q == NULL) {
        return NULL;
    }

    if (in_isr()) {
        if (tmo != 0) {
            return NULL;
        }
        ret = xQueueReceiveFromISR(evq->q, &ev, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        ret = xQueueReceive(evq->q, &ev, tmo);
    }
    if (ret != pdPASS && ret != errQUEUE_EMPTY) {
        return NULL;
    }

    if (ev) {
        ev->queued = false;
    }

    return ev;
}

static bool
eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    BaseType_t woken = pdFALSE;
    BaseType_t ret;

    if (evq == NULL || evq->q == NULL || ev == NULL) {
        return false;
    }

    if (ev->queued) {
        return true;
    }

    ev->queued = true;

    if (in_isr()) {
        ret = xQueueSendToBackFromISR(evq->q, &ev, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        /*
         * The timer daemon posts expired callouts to this queue.  Waiting
         * forever here can deadlock it against a host task that is itself
         * waiting for room on the timer-command queue.  Failure leaves the
         * event unqueued so a producer can retry; it must never freeze the
         * application watchdog feeder.
         */
        ret = xQueueSendToBack(evq->q, &ev, command_timeout());
    }

    if (ret != pdPASS) {
        ev->queued = false;
        return false;
    }
    return true;
}

void
npl_freertos_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    (void)eventq_put(evq, ev);
}

void
npl_freertos_eventq_remove(struct ble_npl_eventq *evq,
                      struct ble_npl_event *ev)
{
    struct ble_npl_event *tmp_ev;
    BaseType_t ret;
    int i;
    int count;
    BaseType_t woken, woken2;

    if (evq == NULL || evq->q == NULL || ev == NULL || !ev->queued) {
        return;
    }

    /*
     * XXX We cannot extract element from inside FreeRTOS queue so as a quick
     * workaround we'll just remove all elements and add them back except the
     * one we need to remove. This is silly, but works for now - we probably
     * better use counting semaphore with os_queue to handle this in future.
     */

    if (in_isr()) {
        woken = pdFALSE;

        count = uxQueueMessagesWaitingFromISR(evq->q);
        for (i = 0; i < count; i++) {
            woken2 = pdFALSE;
            ret = xQueueReceiveFromISR(evq->q, &tmp_ev, &woken2);
            if (ret != pdPASS) {
                break;
            }
            woken |= woken2;

            if (tmp_ev == ev) {
                continue;
            }

            woken2 = pdFALSE;
            ret = xQueueSendToBackFromISR(evq->q, &tmp_ev, &woken2);
            if (ret != pdPASS) {
                tmp_ev->queued = false;
                break;
            }
            woken |= woken2;
        }

        portYIELD_FROM_ISR(woken);
    } else {
        vPortEnterCritical();

        count = uxQueueMessagesWaiting(evq->q);
        for (i = 0; i < count; i++) {
            ret = xQueueReceive(evq->q, &tmp_ev, 0);
            if (ret != pdPASS) {
                break;
            }

            if (tmp_ev == ev) {
                continue;
            }

            ret = xQueueSendToBack(evq->q, &tmp_ev, 0);
            if (ret != pdPASS) {
                tmp_ev->queued = false;
                break;
            }
        }

        vPortExitCritical();
    }

    ev->queued = 0;
}

ble_npl_error_t
npl_freertos_mutex_init(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

    mu->handle = xSemaphoreCreateRecursiveMutex();
    if (mu->handle == NULL) {
        record_alloc_failure(NPL_FREERTOS_ALLOC_MUTEX);
        return BLE_NPL_ENOMEM;
    }

    return BLE_NPL_OK;
}

ble_npl_error_t
npl_freertos_mutex_pend(struct ble_npl_mutex *mu, ble_npl_time_t timeout)
{
    BaseType_t ret;

    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }
    if (mu->handle == NULL) {
        return BLE_NPL_ENOMEM;
    }

    if (in_isr()) {
        return BLE_NPL_ERROR;
    }
    ret = xSemaphoreTakeRecursive(mu->handle, timeout);

    return ret == pdPASS ? BLE_NPL_OK : BLE_NPL_TIMEOUT;
}

ble_npl_error_t
npl_freertos_mutex_release(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }
    if (mu->handle == NULL) {
        return BLE_NPL_ENOMEM;
    }

    if (in_isr()) {
        return BLE_NPL_ERROR;
    }
    if (xSemaphoreGiveRecursive(mu->handle) != pdPASS) {
        return BLE_NPL_BAD_MUTEX;
    }

    return BLE_NPL_OK;
}

ble_npl_error_t
npl_freertos_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    if (!sem || tokens > 128) {
        return BLE_NPL_INVALID_PARAM;
    }

    sem->handle = xSemaphoreCreateCounting(128, tokens);
    if (sem->handle == NULL) {
        record_alloc_failure(NPL_FREERTOS_ALLOC_SEMAPHORE);
        return BLE_NPL_ENOMEM;
    }

    return BLE_NPL_OK;
}

ble_npl_error_t
npl_freertos_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout)
{
    BaseType_t woken = pdFALSE;
    BaseType_t ret;

    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (sem->handle == NULL) {
        return BLE_NPL_ENOMEM;
    }

    if (in_isr()) {
        if (timeout != 0) {
            return BLE_NPL_INVALID_PARAM;
        }
        ret = xSemaphoreTakeFromISR(sem->handle, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        ret = xSemaphoreTake(sem->handle, timeout);
    }

    return ret == pdPASS ? BLE_NPL_OK : BLE_NPL_TIMEOUT;
}

ble_npl_error_t
npl_freertos_sem_release(struct ble_npl_sem *sem)
{
    BaseType_t ret;
    BaseType_t woken = pdFALSE;

    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (sem->handle == NULL) {
        return BLE_NPL_ENOMEM;
    }

    if (in_isr()) {
        ret = xSemaphoreGiveFromISR(sem->handle, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        ret = xSemaphoreGive(sem->handle);
    }

    return ret == pdPASS ? BLE_NPL_OK : BLE_NPL_ERROR;
}

static void
os_callout_timer_cb(TimerHandle_t timer)
{
    struct ble_npl_callout *co;

    co = pvTimerGetTimerID(timer);
    if (co == NULL || co->ev.fn == NULL) {
        return;
    }

    if (co->evq) {
        if (!eventq_put(co->evq, &co->ev)) {
            /* The timer daemon must not block on the host queue. Retry the
             * one-shot callout one tick later; a saturated timer-command queue
             * may still reject this best-effort rearm, but cannot deadlock. */
            (void)xTimerChangePeriod(timer, 1, 0);
        }
    } else {
        co->ev.fn(&co->ev);
    }
}

ble_npl_error_t
npl_freertos_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq,
                     ble_npl_event_fn *ev_cb, void *ev_arg)
{
    if (co == NULL || ev_cb == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (co->handle == NULL) {
        memset(co, 0, sizeof(*co));
        co->handle = xTimerCreate("co", 1, pdFALSE, co, os_callout_timer_cb);
        if (co->handle == NULL) {
            record_alloc_failure(NPL_FREERTOS_ALLOC_CALLOUT);
        }
    }
    co->evq = evq;
    ble_npl_event_init(&co->ev, ev_cb, ev_arg);

    return co->handle == NULL ? BLE_NPL_ENOMEM : BLE_NPL_OK;
}

ble_npl_error_t
npl_freertos_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks)
{
    BaseType_t woken = pdFALSE;
    BaseType_t result;

    if (co == NULL || co->handle == NULL) {
        return BLE_NPL_ENOMEM;
    }

    if (ticks == 0) {
        ticks = 1;
    }

    if (in_isr()) {
        result = xTimerChangePeriodFromISR(co->handle, ticks, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        /* xTimerChangePeriod starts a dormant timer and restarts an active
         * one. One bounded command is both atomic and sufficient; the old
         * Stop+ChangePeriod+Reset sequence tripled queue pressure and could
         * participate in a circular timer/event-queue deadlock. */
        result = xTimerChangePeriod(co->handle, ticks, command_timeout());
    }

    return result == pdPASS ? BLE_NPL_OK : BLE_NPL_ERROR;
}

ble_npl_error_t
npl_freertos_callout_stop(struct ble_npl_callout *co)
{
    BaseType_t result;
    BaseType_t woken = pdFALSE;

    if (co == NULL || co->handle == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (in_isr()) {
        result = xTimerStopFromISR(co->handle, &woken);
        portYIELD_FROM_ISR(woken);
    } else {
        result = xTimerStop(co->handle, command_timeout());
    }

    return result == pdPASS ? BLE_NPL_OK : BLE_NPL_ERROR;
}

ble_npl_time_t
npl_freertos_callout_remaining_ticks(struct ble_npl_callout *co,
                                     ble_npl_time_t now)
{
    ble_npl_time_t rt;
    uint32_t exp;

    if (co == NULL || co->handle == NULL) {
        return 0;
    }

    exp = xTimerGetExpiryTime(co->handle);

    if (exp > now) {
        rt = exp - now;
    } else {
        rt = 0;
    }

    return rt;
}

ble_npl_error_t
npl_freertos_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks)
{
    uint64_t ticks;

    if (out_ticks == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }

    ticks = ((uint64_t)ms * configTICK_RATE_HZ) / 1000;
    if (ticks > UINT32_MAX) {
        return BLE_NPL_EINVAL;
    }

    *out_ticks = ticks;

    return BLE_NPL_OK;
}

ble_npl_error_t
npl_freertos_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out_ms)
{
    uint64_t ms;

    if (out_ms == NULL) {
        return BLE_NPL_INVALID_PARAM;
    }

    ms = ((uint64_t)ticks * 1000) / configTICK_RATE_HZ;
    if (ms > UINT32_MAX) {
        return BLE_NPL_EINVAL;
     }

    *out_ms = ms;

    return BLE_NPL_OK;
}
