#include "hx711_runtime.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "control_log_runtime.h"
#include "sensor_calibration_store.h"
#include "pin_map.h"

#define HX711_BITS 24U
/* Un pulso extra tras los 24 bits: canal A, ganancia 128, la de una célula. */
#define HX711_GAIN_PULSES 1U
/* Medio periodo de reloj. El integrado admite hasta 0,2 us, pero un pulso tan
 * corto se pierde con la pata cargada por el cable de la báscula. */
#define HX711_CLOCK_HALF_US 1U

#define SCALE_TASK_STACK_BYTES 3072U
/* Instrumentación de ensayo: por debajo del control y del supervisor. */
#define SCALE_TASK_PRIORITY (tskIDLE_PRIORITY + 3U)
/* Cuentas de recorrido que se admiten para declarar asentada la lectura. Es un
 * valor de partida; se ajustará con la célula montada. */
#define SCALE_STABLE_TOLERANCE_COUNTS 500U

static const char *TAG = "scale";

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static scale_status_t s_status;
static scale_calibration_t s_calibration;
/* `scale_calibration_t` guarda un puntero al identificador; este es el texto al
 * que apunta, que tiene que vivir tanto como la tarea. */
static char s_calibration_id[SENSOR_CALIBRATION_ID_LENGTH];
static scale_stability_t s_window;
static uint64_t s_last_conversion_ms;
/*
 * Última conversión leída, que se conserva entre sondeos.
 *
 * El integrado entrega 10 muestras por segundo y se sondea cada 50 ms, de modo
 * que **la mitad de los sondeos no encuentran conversión nueva**. Publicar
 * entonces un cero recién inicializado sería inventar una lectura de báscula
 * vacía; lo correcto es conservar la última y dejar que su antigüedad crezca
 * hasta que el propio plazo la declare desactualizada, que es como se comporta
 * cualquier otro canal del sistema.
 */
static int32_t s_last_counts;
/* Si la última ráfaga terminó con `DOUT` liberado; viaja con `s_last_counts`. */
static bool s_last_released;

static uint64_t monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / UINT64_C(1000);
}

/*
 * Emite la ráfaga completa y devuelve los veinticuatro bits crudos.
 *
 * Toda la ráfaga va dentro de una sección crítica porque `SCK` en alto más de
 * 60 us apaga el integrado: una expropiación a mitad de un pulso corrompería la
 * lectura y dormiría el chip, y desde fuera parecería una báscula averiada.
 *
 * `esp_rom_delay_us` se usa a propósito en vez de `vTaskDelay`: dentro de una
 * sección crítica no se puede ceder el procesador, y los retardos son de
 * microsegundos.
 */
static uint32_t shift_in_burst(bool *released)
{
    uint32_t raw = 0U;

    portENTER_CRITICAL(&s_lock);
    for (uint32_t bit = 0U; bit < HX711_BITS; bit++) {
        (void)gpio_set_level(PIN_SCALE_SCK, 1);
        esp_rom_delay_us(HX711_CLOCK_HALF_US);
        (void)gpio_set_level(PIN_SCALE_SCK, 0);
        esp_rom_delay_us(HX711_CLOCK_HALF_US);
        /* El dato es válido tras el flanco de bajada, y llega con el bit más
         * significativo primero. */
        raw = (raw << 1U) | (uint32_t)(gpio_get_level(PIN_SCALE_DOUT) != 0 ? 1U : 0U);
    }
    for (uint32_t pulse = 0U; pulse < HX711_GAIN_PULSES; pulse++) {
        (void)gpio_set_level(PIN_SCALE_SCK, 1);
        esp_rom_delay_us(HX711_CLOCK_HALF_US);
        (void)gpio_set_level(PIN_SCALE_SCK, 0);
        esp_rom_delay_us(HX711_CLOCK_HALF_US);
    }
    /* El pulso 25 devuelve `DOUT` a alto hasta la próxima conversión. Si sigue
     * bajo, la ráfaga no la leyó ningún integrado: ver `scale_reading_evaluate`. */
    *released = gpio_get_level(PIN_SCALE_DOUT) != 0;
    portEXIT_CRITICAL(&s_lock);

    return raw;
}

/* `DOUT` en bajo anuncia conversión lista. No se espera: se consulta. */
static bool conversion_ready(void)
{
    return gpio_get_level(PIN_SCALE_DOUT) == 0;
}

static void publish(const scale_reading_t *reading, bool stable, bool converted)
{
    portENTER_CRITICAL(&s_lock);
    s_status.reading = *reading;
    s_status.stable = stable;
    s_status.spread_counts = scale_stability_spread(&s_window);
    s_status.calibration_id = s_calibration.calibration_id;
    if (converted) {
        s_status.present = true;
        s_status.conversions++;
    }
    /* Un sondeo sin conversión no es un fallo: a 10 muestras por segundo y 50 ms
     * de sondeo, la mitad no encuentran ninguna. Solo se cuenta como pérdida
     * cuando la lectura llega a quedarse vieja. */
    if (reading->quality == SIGNAL_STALE) {
        s_status.timeouts++;
    }
    portEXIT_CRITICAL(&s_lock);
}

static void scale_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    ESP_LOGI(TAG,
             "Báscula de ensayo iniciada; sondeo=%ums; calibración=%s",
             (unsigned)SCALE_POLL_PERIOD_MS,
             s_calibration.calibration_id != NULL
                 ? s_calibration.calibration_id
                 : "ninguna, se publican cuentas y no gramos");

    while (true) {
        bool converted = false;
        if (conversion_ready()) {
            s_last_counts = scale_reading_sign_extend(shift_in_burst(&s_last_released));
            s_last_conversion_ms = monotonic_ms();
            converted = true;
        }

        /* Mientras no haya habido ninguna conversión no hay nada que interpretar,
         * y se declara «no adquirida» en vez de suponer una báscula vacía. */
        const bool ever_converted = s_last_conversion_ms > 0U;
        const uint64_t now = monotonic_ms();
        const uint64_t age =
            (ever_converted && now > s_last_conversion_ms) ? now - s_last_conversion_ms : 0U;

        scale_calibration_t calibration;
        portENTER_CRITICAL(&s_lock);
        calibration = s_calibration;
        portEXIT_CRITICAL(&s_lock);

        const scale_reading_t reading = scale_reading_evaluate(
            s_last_counts, &calibration, age > UINT32_MAX ? UINT32_MAX : (uint32_t)age,
            SCALE_MAXIMUM_AGE_MS, ever_converted, s_last_released);

        /* La ventana de estabilidad solo avanza con conversiones nuevas:
         * alimentarla con la misma lectura repetida declararía quieta una
         * báscula cuyo integrado dejó de responder. */
        bool stable = false;
        if (converted && reading.quality == SIGNAL_VALID) {
            portENTER_CRITICAL(&s_lock);
            stable = scale_stability_push(&s_window, s_last_counts,
                                          SCALE_STABLE_TOLERANCE_COUNTS);
            portEXIT_CRITICAL(&s_lock);
        } else {
            portENTER_CRITICAL(&s_lock);
            stable = s_status.stable && reading.quality == SIGNAL_VALID;
            portEXIT_CRITICAL(&s_lock);
        }

        publish(&reading, stable, converted);

        /*
         * Solo las conversiones entran en el registro.
         *
         * El sondeo corre más rápido que el convertidor y la mitad de las vueltas
         * no encuentra dato nuevo; encolarlas todas repetiría la misma lectura y
         * una derivada calculada sobre esa serie daría caudal cero a intervalos,
         * que es peor que no tener la muestra. Y se encola **fuera** de la
         * sección crítica de `publish`: el registro toma su propio cerrojo, y
         * anidarlos bloquearía el planificador más de lo necesario por un dato
         * que no es del lazo.
         */
        if (converted) {
            /* El recorrido se lee del estado que `publish` acaba de dejar bajo el
             * cerrojo, y no de la ventana suelta: la ventana la escribe esta
             * misma tarea pero la comparten las órdenes de tara y calibración. */
            portENTER_CRITICAL(&s_lock);
            const uint32_t spread = s_status.spread_counts;
            portEXIT_CRITICAL(&s_lock);

            control_log_scale_t entry = {
                .counts = reading.counts,
                .grams = reading.grams,
                .mass_available = reading.mass_available,
                .settled = stable,
                .spread_counts = spread,
                .quality = reading.quality,
            };
            if (calibration.calibration_id != NULL) {
                (void)snprintf(entry.calibration_id, sizeof(entry.calibration_id), "%s",
                               calibration.calibration_id);
            }
            control_log_runtime_scale(&entry);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SCALE_POLL_PERIOD_MS));
    }
}

esp_err_t hx711_runtime_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const gpio_config_t clock_config = {
        .pin_bit_mask = 1ULL << PIN_SCALE_SCK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&clock_config);
    if (result != ESP_OK) {
        return result;
    }
    /* En bajo: `SCK` en alto de forma sostenida apaga el integrado. */
    (void)gpio_set_level(PIN_SCALE_SCK, 0);

    const gpio_config_t data_config = {
        .pin_bit_mask = 1ULL << PIN_SCALE_DOUT,
        .mode = GPIO_MODE_INPUT,
        /* La salida del HX711 es totémica; un *pull-up* solo la cargaría. Sin
         * báscula conectada la pata queda flotante y la tarea publicará lecturas
         * no adquiridas, que es lo que corresponde. */
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    result = gpio_config(&data_config);
    if (result != ESP_OK) {
        return result;
    }

    scale_stability_reset(&s_window);
    memset(&s_calibration, 0, sizeof(s_calibration));
    /*
     * El factor de la báscula viaja en la calibración de las cadenas de medida
     * (ADR-020 § 4.1) y, como las demás, rige desde el arranque. Sin factor
     * guardado queda sin calibrar: se publican cuentas y no gramos.
     */
    sensor_calibration_t stored = {0};
    (void)sensor_calibration_load(&stored, NULL);
    if (stored.scale_counts_per_gram > 0.0F) {
        snprintf(s_calibration_id, sizeof(s_calibration_id), "%s", stored.id);
        s_calibration.counts_per_gram = stored.scale_counts_per_gram;
        s_calibration.calibration_id = s_calibration_id;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.reading.quality = SIGNAL_NOT_ACQUIRED;
    s_last_conversion_ms = 0U;
    s_last_counts = 0;

    const BaseType_t created = xTaskCreate(scale_task, "scale", SCALE_TASK_STACK_BYTES,
                                           NULL, SCALE_TASK_PRIORITY, &s_task);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool hx711_runtime_get_status(scale_status_t *status)
{
    if (status == NULL || s_task == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

esp_err_t hx711_runtime_tare(void)
{
    if (s_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lock);
    const bool stable = s_status.stable;
    const int32_t average = scale_stability_average(&s_window);
    if (stable) {
        s_calibration.tare_counts = average;
    }
    portEXIT_CRITICAL(&s_lock);

    if (!stable) {
        ESP_LOGW(TAG, "Tara rechazada: la lectura no está asentada");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Tara tomada en %" PRId32 " cuentas", average);
    return ESP_OK;
}

esp_err_t hx711_runtime_set_calibration(float counts_per_gram, const char *identifier)
{
    if (s_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&s_lock);
    s_calibration.counts_per_gram = counts_per_gram;
    s_calibration.calibration_id = identifier;
    portEXIT_CRITICAL(&s_lock);

    if (counts_per_gram > 0.0F) {
        ESP_LOGI(TAG, "Calibración %s: %.3f cuentas por gramo",
                 identifier != NULL ? identifier : "sin identificar",
                 (double)counts_per_gram);
    } else {
        ESP_LOGW(TAG, "Báscula sin calibrar: se publicarán cuentas, no gramos");
    }
    return ESP_OK;
}
