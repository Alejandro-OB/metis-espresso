#include "panel_runtime.h"

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "button_debounce.h"
#include "button_hold.h"
#include "firmware_update_runtime.h"
#include "hx711_runtime.h"
#include "operational_runtime.h"
#include "panel_gesture.h"
#include "pin_map.h"
#include "supervisor_runtime.h"
#include "zero_cross_runtime.h"

#define PANEL_TASK_STACK_BYTES 3072U
/* Por encima del registro y de los indicadores, por debajo del control: una
 * orden del barista no debe esperar a una traza, ni retrasar un lazo. */
#define PANEL_TASK_PRIORITY (tskIDLE_PRIORITY + 4U)

static const char *TAG = "panel";

typedef enum {
    PANEL_POWER = 0,
    PANEL_BREW,
    PANEL_TARE,
    PANEL_PURGE,
    PANEL_BUTTON_COUNT,
} panel_button_t;

static const gpio_num_t s_pins[PANEL_BUTTON_COUNT] = {
    [PANEL_POWER] = PIN_BTN_POWER_N,
    [PANEL_BREW] = PIN_BTN_BREW_N,
    /* Cruzados respecto al nombre de la red: el usuario quiso la tara al final
     * del panel (ADR-006, revisión del 2026-10-07). */
    [PANEL_TARE] = PIN_BTN_PURGE_N,
    [PANEL_PURGE] = PIN_BTN_STEAM_N,
};

static const char *s_names[PANEL_BUTTON_COUNT] = {
    [PANEL_POWER] = "encendido",
    [PANEL_BREW] = "café",
    [PANEL_TARE] = "tara",
    [PANEL_PURGE] = "purgado",
};

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static panel_status_t s_status;
static button_debounce_t s_buttons[PANEL_BUTTON_COUNT];
static button_hold_t s_holds[PANEL_BUTTON_COUNT];
static panel_gesture_t s_gesture;

/*
 * El pulsador cierra a masa contra un *pull-up*: nivel bajo es pulsado.
 *
 * La inversión vive aquí y no en el módulo puro, que recibe el nivel ya
 * interpretado. Un cable suelto deja la entrada en alto, es decir, en «no
 * pulsado»: un mando desconectado no puede ordenar nada.
 */
static bool is_pressed(panel_button_t button)
{
    return gpio_get_level(s_pins[button]) == 0;
}

static void publish(operational_event_t event, panel_button_t button, const char *action)
{
    /* Sin espera: la cola tiene holgura de sobra para la cadencia de un dedo, y
     * bloquear aquí retrasaría la lectura de los demás pulsadores. */
    const bool accepted = operational_runtime_post_event(event, 0U);

    portENTER_CRITICAL(&s_lock);
    if (accepted) {
        s_status.published_events++;
    } else {
        s_status.dropped_events++;
    }
    portEXIT_CRITICAL(&s_lock);

    if (accepted) {
        ESP_LOGI(TAG, "Botón de %s: %s", s_names[button], action);
    } else {
        ESP_LOGE(TAG, "La cola no admitió el botón de %s al %s", s_names[button], action);
    }
}

/*
 * La tara no pasa por la cola de la máquina de estados: no es una orden sobre
 * la cafetera sino sobre la báscula. Se decide con el estado del momento y se
 * cuenta en ambos sentidos, porque una tara rechazada en silencio dejaría al
 * barista creyendo que la taza ya está descontada.
 */
static void take_tare(void)
{
    const operational_state_t state = operational_runtime_state();
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (operational_fsm_tare_allowed(state)) {
        result = hx711_runtime_tare();
    }

    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    if (result == ESP_OK) {
        s_status.tares_taken++;
    } else {
        s_status.tares_refused++;
    }
    /* El panel lo confirma con un destello (indicator_panel). */
    s_status.last_tare = result == ESP_OK ? PANEL_TARE_TAKEN : PANEL_TARE_REFUSED;
    s_status.last_tare_us = now_us;
    portEXIT_CRITICAL(&s_lock);

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Botón de tara: báscula a cero");
    } else if (!operational_fsm_tare_allowed(state)) {
        ESP_LOGW(TAG, "Botón de tara rechazado en %s: hay agua cayendo en la taza",
                 operational_state_name(state));
    } else {
        ESP_LOGW(TAG, "Botón de tara rechazado: báscula ausente o lectura sin asentar");
    }
}

static void handle(panel_button_t button, button_edge_t edge)
{
    if (edge == BUTTON_EDGE_NONE) {
        return;
    }

    switch (button) {
    case PANEL_POWER:
        if (edge == BUTTON_EDGE_PRESSED) {
            supervisor_status_t supervisor = {0};
            const bool measuring =
                supervisor_runtime_get_status(&supervisor) && supervisor.observing;
            const bool mains = zero_cross_runtime_usable();
            if (panel_power_on_allowed(operational_runtime_state(), mains, measuring)) {
                publish(OP_EVENT_LOGICAL_POWER_TOGGLE, button, "pulsar");
            } else {
                portENTER_CRITICAL(&s_lock);
                s_status.last_power_refused_us = (uint64_t)esp_timer_get_time();
                s_status.power_refusals++;
                portEXIT_CRITICAL(&s_lock);
                ESP_LOGW(TAG, "Botón de encendido: %s; la máquina sigue en reposo",
                         !mains ? "no hay red" : "todavía sin medidas aptas del proceso");
            }
        }
        break;
    case PANEL_BREW:
        if (edge == BUTTON_EDGE_PRESSED) {
            publish(OP_EVENT_BREW_BUTTON_PRESSED, button, "pulsar");
        }
        break;
    case PANEL_TARE:
        if (edge == BUTTON_EDGE_PRESSED) {
            take_tare();
        }
        break;
    case PANEL_PURGE:
        /*
         * El único que publica los dos flancos: el agua sale mientras el botón
         * esté apretado, de modo que soltarlo es tan orden como pulsarlo. Sin el
         * flanco de soltar, el purgado no terminaría hasta agotar su tiempo
         * máximo.
         */
        publish(edge == BUTTON_EDGE_PRESSED ? OP_EVENT_PURGE_BUTTON_PRESSED
                                            : OP_EVENT_PURGE_BUTTON_RELEASED,
                button, edge == BUTTON_EDGE_PRESSED ? "pulsar" : "soltar");
        break;
    case PANEL_BUTTON_COUNT:
    default:
        break;
    }
}

static void panel_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG,
             "Mando físico activo; muestreo=%ums; %u muestras de acuerdo (%ums de "
             "antirrebote)",
             (unsigned)PANEL_SAMPLE_PERIOD_MS, (unsigned)PANEL_REQUIRED_SAMPLES,
             (unsigned)(PANEL_SAMPLE_PERIOD_MS * PANEL_REQUIRED_SAMPLES));

    while (true) {
        for (uint32_t index = 0U; index < (uint32_t)PANEL_BUTTON_COUNT; index++) {
            const panel_button_t button = (panel_button_t)index;
            const button_edge_t edge =
                button_debounce_update(&s_buttons[index], is_pressed(button));
            handle(button, edge);
            if (button_hold_update(&s_holds[index], button_debounce_pressed(&s_buttons[index]),
                                   button_debounce_pressed_ms(&s_buttons[index]))) {
                ESP_LOGW(TAG, "Botón de %s retenido: no responde hasta que se suelte",
                         s_names[button]);
            }
        }

        const panel_gesture_input_t gesture_input = {
            .state = operational_runtime_state(),
            .power_pressed = button_debounce_pressed(&s_buttons[PANEL_POWER]),
            .power_pressed_ms = button_debounce_pressed_ms(&s_buttons[PANEL_POWER]),
            .power_held = button_hold_is_held(&s_holds[PANEL_POWER]),
            .tare_pressed = button_debounce_pressed(&s_buttons[PANEL_TARE]),
            .tare_pressed_ms = button_debounce_pressed_ms(&s_buttons[PANEL_TARE]),
            .tare_held = button_hold_is_held(&s_holds[PANEL_TARE]),
            .purge_pressed = button_debounce_pressed(&s_buttons[PANEL_PURGE]),
            .purge_pressed_ms = button_debounce_pressed_ms(&s_buttons[PANEL_PURGE]),
            .purge_held = button_hold_is_held(&s_holds[PANEL_PURGE]),
        };
        switch (panel_gesture_update(&s_gesture, &gesture_input)) {
        case PANEL_GESTURE_CLEAR_ALARM:
            ESP_LOGI(TAG, "Encendido sostenido en alarma: se pide despejarla");
            supervisor_runtime_request_alarm_clear();
            break;
        case PANEL_GESTURE_RESTART:
            /* Las salidas de potencia arrancan apagadas (RNF-002), así que el
             * reinicio deja la máquina en reposo. La pausa solo deja salir esta
             * línea por la consola antes de cortar. */
            ESP_LOGW(TAG, "Encendido y tara sostenidos: reinicio de la placa pedido desde el mando");
            vTaskDelay(pdMS_TO_TICKS(100U));
            esp_restart();
            break;
        case PANEL_GESTURE_FIRMWARE_UPDATE:
            /* Solo se pide: `firmware_update` decide si las condiciones lo
             * admiten, y el panel muestra el resultado. */
            ESP_LOGW(TAG, "Tara y purgado sostenidos: se pide buscar firmware nuevo");
            firmware_update_runtime_request();
            break;
        case PANEL_GESTURE_NONE:
        default:
            break;
        }

        portENTER_CRITICAL(&s_lock);
        s_status.cycles++;
        s_status.pressed.power = button_debounce_pressed(&s_buttons[PANEL_POWER]);
        s_status.pressed.brew = button_debounce_pressed(&s_buttons[PANEL_BREW]);
        s_status.pressed.tare = button_debounce_pressed(&s_buttons[PANEL_TARE]);
        s_status.pressed.purge = button_debounce_pressed(&s_buttons[PANEL_PURGE]);
        s_status.purge_held_ms = button_debounce_pressed_ms(&s_buttons[PANEL_PURGE]);
        s_status.held.power = button_hold_is_held(&s_holds[PANEL_POWER]);
        s_status.held.brew = button_hold_is_held(&s_holds[PANEL_BREW]);
        s_status.held.tare = button_hold_is_held(&s_holds[PANEL_TARE]);
        s_status.held.purge = button_hold_is_held(&s_holds[PANEL_PURGE]);
        s_status.held_occurrences = 0U;
        for (uint32_t index = 0U; index < (uint32_t)PANEL_BUTTON_COUNT; index++) {
            s_status.held_occurrences += button_hold_occurrences(&s_holds[index]);
        }
        portEXIT_CRITICAL(&s_lock);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PANEL_SAMPLE_PERIOD_MS));
    }
}

esp_err_t panel_runtime_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_status, 0, sizeof(s_status));
    for (uint32_t index = 0U; index < (uint32_t)PANEL_BUTTON_COUNT; index++) {
        if (!button_debounce_configure(&s_buttons[index], PANEL_REQUIRED_SAMPLES,
                                       PANEL_SAMPLE_PERIOD_MS)) {
            return ESP_ERR_INVALID_ARG;
        }
        /* El purgado se usa manteniéndolo: solo le aplica la regla del arranque. */
        button_hold_configure(&s_holds[index],
                              index == (uint32_t)PANEL_PURGE ? 0U : PANEL_HELD_LIMIT_MS);
    }

    const BaseType_t created = xTaskCreate(panel_task, "panel", PANEL_TASK_STACK_BYTES,
                                           NULL, PANEL_TASK_PRIORITY, &s_task);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool panel_runtime_get_status(panel_status_t *status)
{
    if (status == NULL || s_task == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return true;
}
