#include "control_log_runtime.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "control_log";

/* Periodo de vaciado. Con el lazo de presión a 50 Hz llegan uno o dos registros
 * por vuelta, de modo que el lote es holgura para recuperarse de una pausa. */
#define DRAIN_PERIOD_MS 20U
#define DRAIN_BATCH 64U
#define DRAIN_TASK_STACK 4096U
/* Por debajo de las tareas de control y del supervisor a propósito: si el
 * puerto serie se atasca, quien espera es el registro y nunca el lazo. */
#define DRAIN_TASK_PRIORITY 2U

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static control_log_ring_t s_ring;
static control_log_record_t *s_storage;
static uint32_t s_capacity;
static uint32_t s_sequence[2];
static uint32_t s_scale_sequence;
static bool s_active;

static void push_locked(const control_log_record_t *record)
{
    portENTER_CRITICAL(&s_lock);
    (void)control_log_ring_push(&s_ring, record);
    portEXIT_CRITICAL(&s_lock);
}

void control_log_runtime_sample(const control_log_sample_t *sample)
{
    if (!s_active || sample == NULL) {
        return;
    }
    control_log_record_t record = {0};
    record.kind = CONTROL_LOG_RECORD_SAMPLE;
    record.as.sample = *sample;
    record.as.sample.monotonic_us = (uint64_t)esp_timer_get_time();

    const size_t index = (sample->loop == CONTROL_LOG_LOOP_TEMPERATURE) ? 1U : 0U;
    portENTER_CRITICAL(&s_lock);
    record.as.sample.sequence = s_sequence[index]++;
    (void)control_log_ring_push(&s_ring, &record);
    portEXIT_CRITICAL(&s_lock);
}

void control_log_runtime_scale(const control_log_scale_t *reading)
{
    if (!s_active || reading == NULL) {
        return;
    }
    control_log_record_t record = {0};
    record.kind = CONTROL_LOG_RECORD_SCALE;
    record.as.scale = *reading;
    record.as.scale.monotonic_us = (uint64_t)esp_timer_get_time();

    portENTER_CRITICAL(&s_lock);
    record.as.scale.sequence = s_scale_sequence++;
    (void)control_log_ring_push(&s_ring, &record);
    portEXIT_CRITICAL(&s_lock);
}

void control_log_runtime_event(control_log_event_kind_t kind,
                               control_log_loop_t loop,
                               uint32_t detail)
{
    if (!s_active) {
        return;
    }
    control_log_record_t record = {0};
    record.kind = CONTROL_LOG_RECORD_EVENT;
    record.as.event.monotonic_us = (uint64_t)esp_timer_get_time();
    record.as.event.kind = kind;
    record.as.event.loop = loop;
    record.as.event.detail = detail;
    push_locked(&record);
}

bool control_log_runtime_statistics(uint32_t *pending, uint32_t *accepted, uint32_t *lost_total)
{
    if (!s_active) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    const uint32_t pending_now = control_log_ring_pending(&s_ring);
    const uint32_t accepted_now = control_log_ring_accepted(&s_ring);
    const uint32_t lost_now = control_log_ring_lost_total(&s_ring);
    portEXIT_CRITICAL(&s_lock);
    if (pending != NULL) {
        *pending = pending_now;
    }
    if (accepted != NULL) {
        *accepted = accepted_now;
    }
    if (lost_total != NULL) {
        *lost_total = lost_now;
    }
    return true;
}

uint32_t control_log_runtime_capacity(void)
{
    return s_capacity;
}

bool control_log_runtime_active(void)
{
    return s_active;
}

/*
 * El aviso de pérdida se escribe directamente y no se encola.
 *
 * Encolarlo sería contradictorio: el momento en que hace falta es exactamente
 * aquel en que la cola está llena, de modo que el aviso sería lo primero en
 * perderse.
 */
static void announce_losses(uint32_t lost)
{
    control_log_record_t notice = {0};
    notice.kind = CONTROL_LOG_RECORD_EVENT;
    notice.as.event.monotonic_us = (uint64_t)esp_timer_get_time();
    notice.as.event.kind = CONTROL_LOG_EVENT_RECORDS_LOST;
    notice.as.event.loop = CONTROL_LOG_LOOP_PRESSURE;
    notice.as.event.detail = lost;

    char line[CONTROL_LOG_LINE_BYTES];
    if (control_log_format(&notice, line, sizeof(line)) > 0) {
        fputs(line, stdout);
    }
    ESP_LOGW(TAG, "el registro perdió %" PRIu32 " muestras: la consola no da abasto", lost);
}

static void drain_task(void *argument)
{
    (void)argument;

    /* Las cabeceras se emiten una sola vez, al principio del flujo. */
    fputs(control_log_header_event(), stdout);
    fputs(control_log_header_pressure(), stdout);
    fputs(control_log_header_temperature(), stdout);
    fputs(control_log_header_scale(), stdout);

    char line[CONTROL_LOG_LINE_BYTES];
    while (true) {
        for (uint32_t written = 0U; written < DRAIN_BATCH; written++) {
            control_log_record_t record = {0};
            portENTER_CRITICAL(&s_lock);
            const bool has_record = control_log_ring_pop(&s_ring, &record);
            portEXIT_CRITICAL(&s_lock);
            if (!has_record) {
                break;
            }
            /* El formateo queda fuera de la sección crítica a propósito: es la
             * parte cara y no necesita la cola bloqueada. */
            if (control_log_format(&record, line, sizeof(line)) > 0) {
                fputs(line, stdout);
            }
        }

        portENTER_CRITICAL(&s_lock);
        const uint32_t lost = control_log_ring_take_lost(&s_ring);
        portEXIT_CRITICAL(&s_lock);
        if (lost > 0U) {
            announce_losses(lost);
        }

        vTaskDelay(pdMS_TO_TICKS(DRAIN_PERIOD_MS));
    }
}

esp_err_t control_log_runtime_start(uint32_t capacity)
{
    if (s_active) {
        return ESP_OK;
    }
    if (capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t bytes = (size_t)capacity * sizeof(control_log_record_t);
    s_storage = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_storage == NULL) {
        ESP_LOGE(TAG, "la PSRAM no admite %u bytes para el registro del lazo",
                 (unsigned)bytes);
        return ESP_ERR_NO_MEM;
    }
    memset(s_storage, 0, bytes);

    if (!control_log_ring_init(&s_ring, s_storage, capacity)) {
        heap_caps_free(s_storage);
        s_storage = NULL;
        return ESP_ERR_INVALID_ARG;
    }
    s_capacity = capacity;
    s_sequence[0] = 0U;
    s_sequence[1] = 0U;
    s_active = true;

    if (xTaskCreate(drain_task, "control_log", DRAIN_TASK_STACK, NULL,
                    DRAIN_TASK_PRIORITY, NULL) != pdPASS) {
        s_active = false;
        heap_caps_free(s_storage);
        s_storage = NULL;
        s_capacity = 0U;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "registro del lazo activo: %" PRIu32 " registros en PSRAM (%u bytes)",
             capacity, (unsigned)bytes);
    return ESP_OK;
}
