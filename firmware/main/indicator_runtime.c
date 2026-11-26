#include "indicator_runtime.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "firmware_update_runtime.h"
#include "operational_runtime.h"
#include "panel_runtime.h"
#include "pin_map.h"
#include "supervisor_runtime.h"
#include "hx711_runtime.h"
#include "tare_watch.h"
#include "zero_cross_runtime.h"

/*
 * Periodo de refresco.
 *
 * Tiene que dividir con holgura la mitad del parpadeo más corto —125 ms del
 * rápido— o el LED se vería irregular. A 25 ms hay cinco refrescos por semiciclo,
 * suficiente para que el desfase no sea perceptible.
 */
#define INDICATOR_PERIOD_MS 25U
#define INDICATOR_TASK_STACK_BYTES 3072U
/* Baja: encender un LED nunca debe retrasar al control ni al supervisor. */
#define INDICATOR_TASK_PRIORITY (tskIDLE_PRIORITY + 2U)

static const char *TAG = "indicators";

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static indicator_status_t s_status;

static void drive(gpio_num_t pin, indicator_pattern_t pattern, uint64_t now_ms)
{
    (void)gpio_set_level(pin, indicator_pattern_is_lit(pattern, now_ms) ? 1 : 0);
}

static void indicator_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG, "Indicadores iniciados; refresco=%ums", (unsigned)INDICATOR_PERIOD_MS);
    tare_watch_t tare_watch = {0};
    uint64_t seen_tare_us = 0U;
    uint64_t drift_since_us = 0U;

    while (true) {
        indicator_request_t request = {
            /* El cero de cada enumerado es el estado conservador, así que una
             * consulta fallida deja «reposo» y «nadie ha decidido», no «lista». */
            .state = OP_STATE_SAFE_INACTIVE,
            .pump = ACTUATION_UNDECIDED,
            .heater = ACTUATION_UNDECIDED,
        };
        supervisor_status_t supervisor = {0};
        if (supervisor_runtime_get_status(&supervisor)) {
            request.state = supervisor.state;
            request.pump = supervisor.actuation.pump;
            request.heater = supervisor.actuation.heater;
        }
        request.mains_absent = !zero_cross_runtime_usable();
        request.firmware_updating = firmware_update_runtime_busy();
        panel_status_t panel_status = {0};
        if (panel_runtime_get_status(&panel_status)) {
            request.panel_held = panel_status.held.power || panel_status.held.brew ||
                                 panel_status.held.tare || panel_status.held.purge;
            /*
             * El panel responde al último gesto del barista: verde si la tara
             * se hizo, rojo si algo se negó —una tara sin báscula o con agua
             * cayendo, un encendido sin red o cualquier botón que la máquina de
             * estados rechazó porque no estaba lista (usuario, 2026-10-17)—.
             * Gana el más reciente.
             */
            uint64_t refused_us = panel_status.last_power_refused_us;
            const uint64_t rejected_us = operational_runtime_last_rejected_press_us();
            if (rejected_us > refused_us) {
                refused_us = rejected_us;
            }
            /* Un gesto de firmware rechazado, o una actualización que falló,
             * es un botón negado más (ADR-021). */
            const uint64_t firmware_us = firmware_update_runtime_last_refused_us();
            if (firmware_us > refused_us) {
                refused_us = firmware_us;
            }
            uint64_t taken_us = 0U;
            if (panel_status.last_tare == PANEL_TARE_TAKEN) {
                taken_us = panel_status.last_tare_us;
            } else if (panel_status.last_tare == PANEL_TARE_REFUSED &&
                       panel_status.last_tare_us > refused_us) {
                refused_us = panel_status.last_tare_us;
            }
            const uint64_t latest_us = taken_us > refused_us ? taken_us : refused_us;
            if (latest_us != 0U) {
                const uint64_t age_us = (uint64_t)esp_timer_get_time() - latest_us;
                request.tare = taken_us > refused_us ? INDICATOR_TARE_TAKEN
                                                     : INDICATOR_TARE_REFUSED;
                /* Saturado: un gesto de hace horas no debe dar la vuelta y destellar. */
                request.tare_age_ms = age_us / 1000U > UINT32_MAX ? UINT32_MAX
                                                                  : (uint32_t)(age_us / 1000U);
            }
        }

        scale_status_t scale = {0};
        const bool scale_known = hx711_runtime_get_status(&scale);
        const bool new_tare = panel_status.last_tare == PANEL_TARE_TAKEN &&
                              panel_status.last_tare_us != seen_tare_us;
        if (new_tare) {
            seen_tare_us = panel_status.last_tare_us;
        }
        const tare_watch_input_t watch_input = {
            .tare_taken = new_tare,
            .state = request.state,
            .mass_available = scale_known && scale.reading.mass_available &&
                              signal_quality_is_usable(scale.reading.quality),
            .grams = scale.reading.grams,
        };
        request.tare_drifted = tare_watch_update(&tare_watch, &watch_input);
        const uint64_t now_us = (uint64_t)esp_timer_get_time();
        if (!request.tare_drifted) {
            drift_since_us = 0U;
        } else if (drift_since_us == 0U) {
            drift_since_us = now_us;
        }
        if (request.tare_drifted) {
            const uint64_t drift_ms = (now_us - drift_since_us) / 1000U;
            request.tare_drift_age_ms = drift_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)drift_ms;
        }

        const indicator_panel_t panel = indicator_panel_evaluate(&request);
        /* Tiempo monotónico absoluto: la fase del parpadeo no se reinicia al
         * cambiar de estado, o un estado que oscilara deprisa produciría un LED
         * aparentemente fijo. */
        const uint64_t now_ms = (uint64_t)esp_timer_get_time() / UINT64_C(1000);

        drive(PIN_LED_SAFE, panel.green, now_ms);
        drive(PIN_LED_READY, panel.blue, now_ms);
        drive(PIN_LED_BREW, panel.yellow, now_ms);
        drive(PIN_LED_ALARM, panel.red, now_ms);

        portENTER_CRITICAL(&s_lock);
        s_status.cycles++;
        s_status.panel = panel;
        s_status.state = request.state;
        s_status.fault_warned = panel.red == INDICATOR_SLOW;
        portEXIT_CRITICAL(&s_lock);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(INDICATOR_PERIOD_MS));
    }
}

esp_err_t indicator_runtime_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = OP_STATE_SAFE_INACTIVE;

    const BaseType_t created = xTaskCreate(indicator_task, "indicators",
                                           INDICATOR_TASK_STACK_BYTES, NULL,
                                           INDICATOR_TASK_PRIORITY, &s_task);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_status.running = true;
    return ESP_OK;
}

bool indicator_runtime_get_status(indicator_status_t *status)
{
    if (status == NULL || s_task == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return true;
}
