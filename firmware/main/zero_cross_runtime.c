#include "zero_cross_runtime.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "pin_map.h"
#include "psm_scheduler.h"

/*
 * Red de 60 Hz con un pulso por semiciclo, es decir 120 Hz.
 *
 * Confirmado el 2026-09-15 con la documentación del módulo adquirido: su salida
 * `SYNC` reposa en alto y emite un pulso bajo de unos 100 us en cada cruce, con
 * 8,23 ms de nivel alto entre pulsos. El periodo resultante, 8,33 ms, es el de
 * 120 Hz. Deja de ser una hipótesis.
 */
#define ZERO_CROSS_NOMINAL_HZ 120.0F
#define ZERO_CROSS_TOLERANCE 0.10F
/* Cinco periodos buenos seguidos, unos 42 ms: suficiente para no habilitar con
 * una sola medida y muy por debajo de lo que tarda una extracción en empezar. */
#define ZERO_CROSS_SETTLING_EVENTS 5U

static const char *TAG = "zero_cross";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static zero_cross_monitor_t s_monitor;
static psm_scheduler_t s_scheduler;
static uint8_t s_pump_density;
static bool s_pump_allowed;
static bool s_started;

zero_cross_limits_t zero_cross_runtime_default_limits(void)
{
    return zero_cross_limits_for(ZERO_CROSS_NOMINAL_HZ, ZERO_CROSS_TOLERANCE,
                                 ZERO_CROSS_SETTLING_EVENTS);
}

/*
 * Rutina de interrupción del flanco.
 *
 * Hace tres cosas y ninguna más: toma el instante, se lo entrega al monitor y
 * decide con el planificador si esta oportunidad se aprovecha. Las tres son
 * aritmética entera sin bloqueos. `esp_timer_get_time` es segura desde
 * interrupción y reside en IRAM, de modo que la captura no depende de que la
 * caché de flash esté disponible: durante una escritura en flash, una ISR que
 * necesitara flash simplemente no se ejecutaría y los flancos se perderían en
 * silencio.
 */
static void IRAM_ATTR zero_cross_isr(void *argument)
{
    (void)argument;
    const int64_t now = esp_timer_get_time();

    bool emit = false;
    portENTER_CRITICAL_ISR(&s_lock);
    (void)zero_cross_monitor_event(&s_monitor, (uint64_t)now);
    /*
     * Un rebote no es un cruce: no ofrece oportunidad al planificador ni toca
     * la salida. Si la tocara, escribiría a destiempo, unos 50 µs después del
     * cruce real, la decisión que ya se tomó en él. Cualquier otro rechazo
     * —periodo largo, reloj que retrocede— sí pasa por abajo, y como la señal
     * deja de ser apta la salida queda en bajo.
     */
    if (zero_cross_monitor_last_was_bounce(&s_monitor)) {
        portEXIT_CRITICAL_ISR(&s_lock);
        return;
    }
    /*
     * El planificador vive aquí y no en la tarea de control, y no es una
     * elección de comodidad. Las oportunidades llegan a 120 Hz, cada 8,33 ms, y
     * el lazo corre cada 20 ms: una tarea vería dos o tres cruces por ciclo y no
     * podría decidir sobre cada uno por separado, que es exactamente lo que
     * ADR-007 § 3.4 pide. Cada cruce es un semiciclo, y el planificador decide
     * por ciclos completos porque la bomba solo conduce en una polaridad
     * (enmienda del 2026-10-11). `psm_scheduler_half_cycle` es aritmética
     * entera sin bloqueos, de modo que cabe en una interrupción sin alargarla
     * de forma apreciable.
     */
    if (s_pump_allowed && zero_cross_monitor_usable(&s_monitor, (uint64_t)now)) {
        emit = psm_scheduler_half_cycle(&s_scheduler, s_pump_density);
    }
    portEXIT_CRITICAL_ISR(&s_lock);

    /*
     * El nivel se escribe en **todos** los cruces, también cuando no se emite.
     * El módulo dispara mientras la entrada esté alta y el triac se apaga solo
     * en el cruce siguiente, de modo que dejar el nivel sin tocar prolongaría la
     * actuación anterior una semionda más de lo decidido.
     */
    (void)gpio_set_level(PIN_PUMP_CMD, emit ? 1 : 0);
}

void zero_cross_runtime_set_pump_demand(uint8_t density, bool allowed)
{
    portENTER_CRITICAL(&s_lock);
    s_pump_density = density;
    s_pump_allowed = allowed;
    portEXIT_CRITICAL(&s_lock);
}

void zero_cross_runtime_reset_pump(void)
{
    portENTER_CRITICAL(&s_lock);
    psm_scheduler_reset(&s_scheduler);
    s_pump_density = 0U;
    s_pump_allowed = false;
    portEXIT_CRITICAL(&s_lock);
}

bool zero_cross_runtime_get_pump_statistics(uint32_t *opportunities,
                                            uint32_t *emitted,
                                            uint32_t *residue,
                                            uint8_t *density)
{
    if (!s_started) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    const uint32_t offered = psm_scheduler_opportunities(&s_scheduler);
    const uint32_t used = psm_scheduler_emitted(&s_scheduler);
    const uint32_t accumulator = psm_scheduler_residue(&s_scheduler);
    const uint8_t requested = s_pump_density;
    portEXIT_CRITICAL(&s_lock);

    if (opportunities != NULL) {
        *opportunities = offered;
    }
    if (emitted != NULL) {
        *emitted = used;
    }
    if (residue != NULL) {
        *residue = accumulator;
    }
    if (density != NULL) {
        *density = requested;
    }
    return true;
}

esp_err_t zero_cross_runtime_start(const zero_cross_limits_t *limits)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    const zero_cross_limits_t effective =
        limits != NULL ? *limits : zero_cross_runtime_default_limits();

    portENTER_CRITICAL(&s_lock);
    const bool configured = zero_cross_monitor_configure(&s_monitor, &effective);
    psm_scheduler_reset(&s_scheduler);
    s_pump_density = 0U;
    s_pump_allowed = false;
    portEXIT_CRITICAL(&s_lock);
    if (!configured) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Flanco de bajada, no de subida.
     *
     * La salida del módulo reposa en alto y marca cada cruce con un pulso bajo
     * de unos 100 us. El instante del cruce es por tanto el **inicio** de ese
     * pulso, que es su flanco de bajada; el de subida llega 100 us más tarde.
     * Ambos ocurren una vez por semiciclo y darían la misma frecuencia, de modo
     * que medir la cadencia no habría delatado el error: solo el disparo saldría
     * sistemáticamente retrasado.
     */
    esp_err_t result = gpio_set_intr_type(PIN_PUMP_ZC, GPIO_INTR_NEGEDGE);
    if (result != ESP_OK) {
        return result;
    }
    /* El servicio puede haberlo instalado otro componente; eso no es un fallo. */
    result = gpio_install_isr_service(0);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    result = gpio_isr_handler_add(PIN_PUMP_ZC, zero_cross_isr, NULL);
    if (result != ESP_OK) {
        return result;
    }

    s_started = true;
    ESP_LOGI(TAG,
             "Vigilancia de cruce por cero iniciada; periodo esperado %" PRIu32 "–%" PRIu32
             " us; pérdida a %" PRIu32 " us",
             effective.minimum_period_us, effective.maximum_period_us, effective.timeout_us);
    return ESP_OK;
}

bool zero_cross_runtime_usable(void)
{
    if (!s_started) {
        return false;
    }
    const uint64_t now = (uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    const bool usable = zero_cross_monitor_usable(&s_monitor, now);
    portEXIT_CRITICAL(&s_lock);
    return usable;
}

bool zero_cross_runtime_get_snapshot(zero_cross_snapshot_t *snapshot)
{
    if (snapshot == NULL || !s_started) {
        return false;
    }
    const uint64_t now = (uint64_t)esp_timer_get_time();

    /* La copia se hace bajo sección crítica y los cálculos fuera: dentro solo
     * debe ocurrir lo imprescindible, porque la ISR compite por este cerrojo. */
    zero_cross_monitor_t copy;
    portENTER_CRITICAL(&s_lock);
    copy = s_monitor;
    portEXIT_CRITICAL(&s_lock);

    snapshot->usable = zero_cross_monitor_usable(&copy, now);
    snapshot->frequency_hz = zero_cross_monitor_frequency_hz(&copy);
    snapshot->observed_frequency_hz = zero_cross_monitor_observed_frequency_hz(&copy);
    snapshot->last_interval_us = zero_cross_monitor_last_interval_us(&copy);
    snapshot->last_period_us = zero_cross_monitor_last_period_us(&copy);
    snapshot->total_events = zero_cross_monitor_total_events(&copy);
    snapshot->rejected_events = zero_cross_monitor_rejected_events(&copy);
    snapshot->short_rejections = zero_cross_monitor_short_rejections(&copy);
    snapshot->shortest_short_us = zero_cross_monitor_shortest_short_us(&copy);
    snapshot->longest_short_us = zero_cross_monitor_longest_short_us(&copy);
    snapshot->age_ms = 0U;
    if (copy.total_events > 0U && now > copy.last_event_us) {
        const uint64_t age = (now - copy.last_event_us) / UINT64_C(1000);
        snapshot->age_ms = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
    }
    return true;
}
