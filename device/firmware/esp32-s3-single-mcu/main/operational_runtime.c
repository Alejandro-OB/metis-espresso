#include "operational_runtime.h"

#include <inttypes.h>
#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define OPERATIONAL_EVENT_QUEUE_LENGTH 16U
#define OPERATIONAL_TRANSITION_QUEUE_LENGTH 16U
#define OPERATIONAL_TASK_STACK_BYTES 4096U
#define OPERATIONAL_TASK_PRIORITY (tskIDLE_PRIORITY + 8U)

static const char *TAG = "operational_task";

static QueueHandle_t s_event_queue;
static QueueHandle_t s_transition_queue;
static TaskHandle_t s_task_handle;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static operational_state_t s_published_state = OP_STATE_SAFE_INACTIVE;
static uint64_t s_last_rejected_press_us;

static void publish_state(operational_state_t state)
{
    portENTER_CRITICAL(&s_state_lock);
    s_published_state = state;
    portEXIT_CRITICAL(&s_state_lock);
}

static void operational_task(void *argument)
{
    (void)argument;
    operational_fsm_t fsm;
    operational_fsm_init(&fsm);
    publish_state(fsm.state);
    ESP_LOGI(TAG, "Tarea iniciada en %s; núcleo=%d", operational_state_name(fsm.state),
             (int)xPortGetCoreID());

    while (true) {
        uint32_t pending_events = 0U;
        if (xQueueReceive(s_event_queue, &pending_events, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const operational_transition_t transition =
            operational_fsm_dispatch_highest_priority(&fsm, pending_events);
        publish_state(fsm.state);
        if (!transition.accepted && operational_event_is_operator_press(transition.event)) {
            portENTER_CRITICAL(&s_state_lock);
            s_last_rejected_press_us = (uint64_t)esp_timer_get_time();
            portEXIT_CRITICAL(&s_state_lock);
        }

        ESP_LOGI(TAG, "%s --%s/%s--> %s",
                 operational_state_name(transition.from),
                 operational_event_name(transition.event),
                 transition.accepted ? "aceptado" : "rechazado",
                 operational_state_name(transition.to));

        if (xQueueSend(s_transition_queue, &transition, 0U) != pdTRUE) {
            ESP_LOGE(TAG, "Cola de transiciones llena; se perdió un registro");
        }
    }
}

esp_err_t operational_runtime_start(void)
{
    if (s_task_handle != NULL || s_event_queue != NULL ||
        s_transition_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_event_queue = xQueueCreate(OPERATIONAL_EVENT_QUEUE_LENGTH,
                                 sizeof(uint32_t));
    s_transition_queue = xQueueCreate(OPERATIONAL_TRANSITION_QUEUE_LENGTH,
                                      sizeof(operational_transition_t));
    if (s_event_queue == NULL || s_transition_queue == NULL) {
        if (s_event_queue != NULL) {
            vQueueDelete(s_event_queue);
            s_event_queue = NULL;
        }
        if (s_transition_queue != NULL) {
            vQueueDelete(s_transition_queue);
            s_transition_queue = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        operational_task, "operational_fsm", OPERATIONAL_TASK_STACK_BYTES,
        NULL, OPERATIONAL_TASK_PRIORITY, &s_task_handle, 1);
    if (created != pdPASS) {
        vQueueDelete(s_event_queue);
        vQueueDelete(s_transition_queue);
        s_event_queue = NULL;
        s_transition_queue = NULL;
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

bool operational_runtime_post_event(operational_event_t event,
                                    TickType_t wait_ticks)
{
    if (event < 0 || event >= OP_EVENT_COUNT) {
        return false;
    }
    return operational_runtime_post_events(OP_EVENT_MASK(event), wait_ticks);
}

bool operational_runtime_post_events(uint32_t pending_events,
                                     TickType_t wait_ticks)
{
    const uint32_t valid_events =
        (UINT32_C(1) << (uint32_t)OP_EVENT_COUNT) - UINT32_C(1);
    if (s_event_queue == NULL || pending_events == 0U ||
        (pending_events & ~valid_events) != 0U) {
        return false;
    }

    return xQueueSend(s_event_queue, &pending_events, wait_ticks) == pdTRUE;
}

bool operational_runtime_receive_transition(operational_transition_t *transition,
                                            TickType_t wait_ticks)
{
    if (s_transition_queue == NULL || transition == NULL) {
        return false;
    }
    return xQueueReceive(s_transition_queue, transition, wait_ticks) == pdTRUE;
}

operational_state_t operational_runtime_state(void)
{
    portENTER_CRITICAL(&s_state_lock);
    const operational_state_t state = s_published_state;
    portEXIT_CRITICAL(&s_state_lock);
    return state;
}

operational_authorization_t operational_runtime_authorization(void)
{
    /* La autorización depende solo del estado, de modo que se deriva del estado
     * publicado sin volver a tomar el cerrojo de la tarea. */
    const operational_fsm_t view = {.state = operational_runtime_state()};
    return operational_fsm_authorization(&view);
}

uint64_t operational_runtime_last_rejected_press_us(void)
{
    portENTER_CRITICAL(&s_state_lock);
    const uint64_t at = s_last_rejected_press_us;
    portEXIT_CRITICAL(&s_state_lock);
    return at;
}
