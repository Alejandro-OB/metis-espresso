#include "control_runtime.h"

#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "acquisition_runtime.h"
#include "control_filter.h"
#include "control_log_runtime.h"
#include "control_plant.h"
#include "control_tuning.h"
#include "driver/gpio.h"
#include "heater_modulator.h"
#include "hydraulic_test.h"
#include "identification_mode.h"
#include "identification_tuning.h"
#include "operational_runtime.h"
#include "pi_controller.h"
#include "pin_map.h"
#include "pressure_preinfusion.h"
#include "process_variables.h"
#include "profile_runtime.h"
#include "commissioned_plant.h"
#include "heater_fast_start.h"
#include "first_use_offset_runtime.h"
#include "heater_feedforward.h"
#include "heater_recovery_hold.h"
#include "heater_recovery_return.h"
#include "psm_scheduler.h"
#include "stimulus_sequence.h"
#include "supervisor_runtime.h"
#include "zero_cross_runtime.h"

#define CONTROL_PERIOD_MS 20U       /* ADR-007 § 3.3 */
#define THERMAL_DECIMATION 10U      /* 200 ms, ADR-009 § 3.6 */
#define HEATER_WINDOW_MS 1000U      /* ADR-009 § 3.5 */
#define CONTROL_TASK_STACK_BYTES 5120U
/* Por debajo del supervisor, que es quien puede retirarle el permiso, y por
 * encima de la simulación y del registro. */
#define CONTROL_TASK_PRIORITY (tskIDLE_PRIORITY + 6U)
#define CONTROL_TASK_CORE 1         /* junto a la adquisición y al supervisor */

static const char *TAG = "control";

#if CONFIG_CAFE_CONTROL_BENCH_LOOP
#define BENCH_LOOP 1
#else
#define BENCH_LOOP 0
#endif

/*
 * Sintonía en caliente (ADR-020 § 4.2): solo en el binario de identificación
 * con una secuencia de sintonía. El estado lo cambia la tarea del puerto de
 * servicio y lo aplica la del lazo, siempre bajo `s_lock`.
 *
 * Térmica o de presión (enmienda a ADR-007 § 5 del 2026-10-12), nunca las dos:
 * la secuencia se elige en compilación y es una sola, así que el estado se
 * comparte y cada binario lo usa para su lazo.
 */
#if CONFIG_CAFE_IDENTIFICATION_THERMAL_TUNING
#define THERMAL_TUNING 1
#else
#define THERMAL_TUNING 0
#endif
#if CONFIG_CAFE_IDENTIFICATION_PRESSURE_TUNING
#define PRESSURE_TUNING 1
#else
#define PRESSURE_TUNING 0
#endif
#if THERMAL_TUNING && PRESSURE_TUNING
#error "Una sola sintonía en caliente por binario"
#endif
#define LIVE_TUNING (THERMAL_TUNING || PRESSURE_TUNING)

/*
 * Ensayo hidráulico E2 (enmienda a ADR-007 § 5 del 2026-10-16): la bomba a
 * una orden fija durante la extracción, con las dos actas aprobadas. Sin ellas
 * el guardián no autorizaría la bomba fuera del modo restringido, y el ensayo
 * no puede pasar por ese modo, que no actúa sobre un lazo con acta. Se impide
 * al compilar. La orden la elige la tarea del puerto y la aplica la del lazo,
 * bajo `s_lock`.
 */
#if CONFIG_CAFE_HYDRAULIC_TEST
#define HYDRAULIC_TEST 1
#if !(HAVE_COMMISSIONED_PRESSURE && HAVE_COMMISSIONED_TEMPERATURE)
#error "El ensayo hidráulico exige las actas de presión y de temperatura compiladas"
#endif
static hydraulic_test_t s_hydraulic;
#else
#define HYDRAULIC_TEST 0
#endif
#if LIVE_TUNING
static identification_tuning_t s_tuning;
static bool s_tuning_dirty;
/* Si la sesión de este arranque está confirmada en el mando: sin eso no se
 * ajusta nada (ADR-020 § 4.2). */
static bool s_tuning_confirmed;
static actuation_verdict_t s_tuning_verdict;
static char s_tuning_identifier[CONTROL_TUNING_ID_BYTES];
#endif
#if !BENCH_LOOP
/* La preinfusión a presión de extracción (ADR-007, enmienda del 2026-10-12,
 * tarde): en la sintonía con los números de Kconfig, en operación con los del
 * acta de presión. Sin ellos no actúa y el PI regula desde el principio, como
 * antes. Solo la usa la tarea del lazo, no necesita `s_lock`. */
static bool s_preinfusion_enabled;
static pressure_preinfusion_t s_preinfusion;
/* Filtro propio para decidir el paso, más lento que el del lazo: el del lazo
 * se reinicia en cada ciclo mientras el PI no regula, y con sus 20 ms un golpe
 * de la bomba sobre la meseta bastó para hacer entrar al PI antes de que la
 * pastilla cerrara (2026-10-12). */
static control_filter_t s_preinfusion_filter;
#endif

typedef struct {
    control_tuning_t tuning;
    control_filter_t filter;
    pi_controller_t controller;
    bool closing;            /* el lazo estaba cerrado el ciclo anterior */
    uint32_t resets;
#if BENCH_LOOP
    control_plant_t plant;
#endif
} loop_t;

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static control_runtime_status_t s_status;

static loop_t s_pressure;
static loop_t s_temperature;
static heater_modulator_t s_heater;
static uint8_t s_heater_demand;
static uint32_t s_heater_conducting_ticks;
static uint64_t s_heater_applied_ms;
/* La densidad que el lazo de presión ordenó en este ciclo. Los dos lazos corren
 * en esta misma tarea y la presión va antes, de modo que la temperatura la lee
 * ya actualizada y sin cerrojo. */
static heater_feedforward_t s_heater_feedforward;
/* La ganancia del acta: la que vuelve a regir cuando el supervisor retira el
 * refuerzo del primer uso (ADR-009, enmienda del 2026-10-27). */
static float s_heater_feedforward_base_gain;
/* Retención de la integral tras el café (ADR-009, enmienda del 2026-10-16).
 * Como la prealimentación, la activa el acta; en el binario de identificación,
 * una opción de ensayo. Sin ninguna, el PI se comporta como antes. */
#if defined(CONFIG_CAFE_HEATER_RECOVERY_HOLD) && CONFIG_CAFE_HEATER_RECOVERY_HOLD
#define HEATER_RECOVERY_HOLD_ENABLED 1
#define HEATER_RECOVERY_HOLD_SOURCE "provisional (ensayo supervisado)"
#elif HAVE_COMMISSIONED_TEMPERATURE && defined(COMMISSIONED_TEMPERATURE_RECOVERY_HOLD) && \
    COMMISSIONED_TEMPERATURE_RECOVERY_HOLD
#define HEATER_RECOVERY_HOLD_ENABLED 1
#define HEATER_RECOVERY_HOLD_SOURCE COMMISSIONED_TEMPERATURE_IDENTIFIER
#else
#define HEATER_RECOVERY_HOLD_ENABLED 0
#define HEATER_RECOVERY_HOLD_SOURCE "sin retención en el acta"
#endif
static heater_recovery_hold_t s_heater_recovery_hold;
/* Devolución de la integral tras el café (ADR-009, segunda enmienda del
 * 2026-10-16): la misma regla de origen que la retención. Las dos a la vez no
 * tienen sentido: una congela la integral y la otra la deja crecer. */
#if defined(CONFIG_CAFE_HEATER_RECOVERY_RETURN_TAU_S) && CONFIG_CAFE_HEATER_RECOVERY_RETURN_TAU_S > 0
#define HEATER_RECOVERY_RETURN_TAU_S ((float)CONFIG_CAFE_HEATER_RECOVERY_RETURN_TAU_S)
#define HEATER_RECOVERY_RETURN_SOURCE "provisional (ensayo supervisado)"
#elif HAVE_COMMISSIONED_TEMPERATURE && defined(COMMISSIONED_TEMPERATURE_RECOVERY_RETURN_TAU_S)
#define HEATER_RECOVERY_RETURN_TAU_S COMMISSIONED_TEMPERATURE_RECOVERY_RETURN_TAU_S
#define HEATER_RECOVERY_RETURN_SOURCE COMMISSIONED_TEMPERATURE_IDENTIFIER
#else
#define HEATER_RECOVERY_RETURN_TAU_S 0.0F
#define HEATER_RECOVERY_RETURN_SOURCE "sin devolución en el acta"
#endif
#if HEATER_RECOVERY_HOLD_ENABLED
#if defined(CONFIG_CAFE_HEATER_RECOVERY_RETURN_TAU_S) && CONFIG_CAFE_HEATER_RECOVERY_RETURN_TAU_S > 0
#error "La retención y la devolución de la integral térmica no se combinan"
#endif
#endif
static heater_recovery_return_t s_heater_recovery_return;
static uint8_t s_pump_density;
static bool s_pump_allowed;
/* Arranque rápido (ADR-009, enmienda del 2026-10-11): una fase anterior al PI.
 * Sin parámetros del acta —o, en el binario de identificación, provisionales—
 * no actúa y el calentador se comporta exactamente como con el acta v2. */
static heater_fast_start_t s_heater_fast_start;

/*
 * Los parámetros del arranque rápido salen del acta de temperatura. Solo el
 * binario de identificación admite unos provisionales, para el ensayo
 * supervisado que los mide; y si se dan, mandan sobre el acta y se avisa.
 * Sin ninguno de los dos, no actúa: no se arranca al 100 % con un número
 * supuesto.
 */
static void configure_fast_start(void)
{
#if defined(CONFIG_CAFE_HEATER_FAST_START_PROVISIONAL)
    const heater_fast_start_parameters_t parameters = {
        .margin_c = (float)CONFIG_CAFE_HEATER_FAST_START_MARGIN_DECI_C / 10.0F,
        .activation_band_c = (float)CONFIG_CAFE_HEATER_FAST_START_BAND_DECI_C / 10.0F,
        .max_boost_s = (float)CONFIG_CAFE_HEATER_FAST_START_MAX_BOOST_S,
        .max_setpoint_c = (float)CONFIG_CAFE_HEATER_FAST_START_MAX_SETPOINT_DECI_C / 10.0F,
        .release_drop_c = (float)CONFIG_CAFE_HEATER_FAST_START_RELEASE_DECI_C / 10.0F,
    };
    const char *source = "provisional (ensayo supervisado)";
#elif HAVE_COMMISSIONED_TEMPERATURE && defined(COMMISSIONED_TEMPERATURE_FAST_START_MARGIN_C)
    const heater_fast_start_parameters_t parameters = {
        .margin_c = COMMISSIONED_TEMPERATURE_FAST_START_MARGIN_C,
        .activation_band_c = COMMISSIONED_TEMPERATURE_FAST_START_BAND_C,
        .max_boost_s = COMMISSIONED_TEMPERATURE_FAST_START_MAX_BOOST_S,
        .max_setpoint_c = COMMISSIONED_TEMPERATURE_FAST_START_MAX_SETPOINT_C,
        .release_drop_c = COMMISSIONED_TEMPERATURE_FAST_START_RELEASE_C,
    };
    const char *source = COMMISSIONED_TEMPERATURE_IDENTIFIER;
#else
    const heater_fast_start_parameters_t parameters = {0};
    const char *source = NULL;
#endif
    if (source == NULL) {
        (void)heater_fast_start_configure(&s_heater_fast_start, NULL);
        ESP_LOGI(TAG, "Arranque rápido: sin parámetros en el acta; no actúa");
        return;
    }
    if (!heater_fast_start_configure(&s_heater_fast_start, &parameters)) {
        ESP_LOGE(TAG, "Arranque rápido: parámetros de %s no válidos; no actúa", source);
        return;
    }
    ESP_LOGW(TAG,
             "Arranque rápido ACTIVO (%s): corte en referencia - %.1f C, banda %.1f C, "
             "máximo %.0f s al 100 %%, referencia hasta %.1f C, espera hasta caer %.1f C",
             source, (double)parameters.margin_c, (double)parameters.activation_band_c,
             (double)parameters.max_boost_s, (double)parameters.max_setpoint_c,
             (double)parameters.release_drop_c);
}

control_tuning_t control_runtime_pressure_tuning(void)
{
    return control_tuning_pressure();
}

control_tuning_t control_runtime_temperature_tuning(void)
{
    return control_tuning_temperature();
}

/*
 * Referencia de temperatura según el estado.
 *
 * El lazo no elige: pregunta al estado operativo si le corresponde la
 * referencia de café, la única desde que se retiró el vapor. Fuera de los estados que regulan, devuelve el
 * ambiente, que junto a la inhibición del guardián deja el calentador apagado
 * por dos caminos independientes.
 */
static float temperature_setpoint(const supervisor_status_t *supervisor)
{
    switch (supervisor->state) {
    case OP_STATE_COFFEE_HEATING:
    case OP_STATE_READY:
    case OP_STATE_EXTRACTION:
    case OP_STATE_MANUAL_PURGE:
        return supervisor->limits.coffee_setpoint_c;
    case OP_STATE_SAFE_INACTIVE:
    case OP_STATE_ALARM:
    default:
        return 0.0F;
    }
}

/*
 * Referencia de presión: la del perfil efectivo, y solo mientras hay demanda de
 * bomba. En cualquier otro estado la referencia es cero, que para una planta
 * unilateral significa apagar.
 */
static float pressure_setpoint(const supervisor_status_t *supervisor)
{
    if (supervisor->state != OP_STATE_EXTRACTION &&
        supervisor->state != OP_STATE_MANUAL_PURGE) {
        return 0.0F;
    }
    extraction_profile_t profile = {0};
    if (!profile_runtime_get_effective(&profile)) {
        return 0.0F;
    }
    return profile.pressure_setpoint_bar;
}

/* Rellena la parte común del registro con lo que comparten los dos lazos. */
static void fill_common(control_log_sample_t *sample,
                        control_log_loop_t which,
                        const supervisor_status_t *supervisor,
                        actuation_verdict_t verdict,
                        bool output_enabled,
                        const pi_result_t *result,
                        float authorized_setpoint)
{
    sample->loop = which;
    sample->state = supervisor->state;
    sample->verdict = verdict;
    sample->commissioned = false;  /* lo sobrescribe el llamante con su sintonía */
    sample->output_enabled = output_enabled;
    sample->control.authorized_setpoint = authorized_setpoint;
    sample->control.internal_setpoint = result->setpoint;
    sample->control.error = result->error;
    sample->control.proportional = result->proportional;
    sample->control.integral = result->integral;
    sample->control.feedforward = result->feedforward;
    sample->control.unlimited = result->unlimited;
    sample->control.output = result->output;
    sample->control.saturated_high = result->saturated_high;
    sample->control.saturated_low = result->saturated_low;
    sample->control.integral_held = result->integral_held;
}

/*
 * Decide si el lazo debe cerrarse en este ciclo y lo reinicia en los cambios.
 *
 * Reiniciar al inhibir es lo que impide que, al recuperar el permiso, el lazo
 * arranque con una integral cargada durante el tiempo en que su salida estuvo
 * cortada y con una referencia interna que se alejó de la medida.
 */
static bool update_gate(loop_t *loop, control_log_loop_t which, bool closing,
                        float measurement, actuation_verdict_t verdict)
{
    if (closing == loop->closing) {
        return closing;
    }
    loop->closing = closing;
    loop->resets++;
    control_filter_reset(&loop->filter);
    pi_controller_reset(&loop->controller, measurement);
    control_log_runtime_event(closing ? CONTROL_LOG_EVENT_LOOP_ENABLED
                                      : CONTROL_LOG_EVENT_LOOP_INHIBITED,
                              which, (uint32_t)verdict);
    return closing;
}

static void run_pressure(const supervisor_status_t *supervisor)
{
    const actuation_verdict_t verdict = supervisor->actuation.pump;
    const bool allowed = actuation_verdict_allows(verdict);
    const bool identifying = verdict == ACTUATION_ALLOWED_IDENTIFICATION;
#if HYDRAULIC_TEST
    portENTER_CRITICAL(&s_lock);
    const uint8_t hydraulic_order = hydraulic_test_step(&s_hydraulic, supervisor->state);
    portEXIT_CRITICAL(&s_lock);
    /* Solo en la extracción: el purgado sigue siendo el de operación. La orden
     * sustituye al PI, que queda cebado en la medida como al identificar. */
    const bool hydraulic = allowed && supervisor->state == OP_STATE_EXTRACTION;
#else
    const bool hydraulic = false;
    const uint8_t hydraulic_order = 0U;
#endif

    control_log_sample_t sample = {0};
    float converted = 0.0F;

#if BENCH_LOOP
    converted = control_plant_value(&s_pressure.plant);
    sample.source = PROCESS_SOURCE_SIMULATED;
    sample.quality = SIGNAL_VALID;
    /* El modelo de banco no pasa por convertidor: declarar un código y una
     * tensión sería inventar dos medidas que nadie tomó. */
    const bool closing = true;
    const bool tuning_active = false;
    const bool pump_tuning_ready = false;
#else
    measurement_t measurement = {0};
    const bool have_measurement = acquisition_runtime_get_pressure(&measurement);
    converted = have_measurement ? measurement.raw_value : 0.0F;
    sample.source = have_measurement ? PROCESS_SOURCE_MEASURED : PROCESS_SOURCE_NONE;
    sample.quality = have_measurement ? measurement.quality : SIGNAL_NOT_ACQUIRED;
    sample.signal.counts = measurement.counts;
    sample.signal.volts = measurement.volts;
#if PRESSURE_TUNING
    /* Con la sintonía, identificar es regular: el PI decide y la secuencia solo
     * pone el techo (enmienda a ADR-007 § 5 del 2026-10-12). Sin planta
     * configurada no regula ni aplica el techo en lazo abierto: la bomba queda
     * en cero. */
    pi_parameters_t tuning_parameters;
    bool tuning_ready;
    bool tuning_dirty;
    char tuning_identifier[CONTROL_TUNING_ID_BYTES];
    portENTER_CRITICAL(&s_lock);
    /* Una extracción por prueba: entre una y otra la sesión sigue confirmada y
     * el siguiente ajuste se pide con la bomba quieta, que es cuando se mira
     * el registro de la anterior. Abortada, no: el aborto cierra la sesión. */
    s_tuning_confirmed = supervisor->identification.phase == IDENTIFICATION_ARMED ||
                         supervisor->identification.phase == IDENTIFICATION_RUNNING ||
                         supervisor->identification.phase == IDENTIFICATION_FINISHED;
    s_tuning_verdict = verdict;
    tuning_ready = s_tuning.configured;
    tuning_dirty = s_tuning_dirty;
    s_tuning_dirty = false;
    tuning_parameters = identification_tuning_parameters(&s_tuning);
    memcpy(tuning_identifier, s_tuning_identifier, sizeof(tuning_identifier));
    portEXIT_CRITICAL(&s_lock);
    if (tuning_dirty && tuning_ready) {
        (void)pi_controller_configure(&s_pressure.controller, &tuning_parameters);
        pi_controller_reset(&s_pressure.controller, converted);
    }
    /*
     * Solo con techo y con la pastilla ya cerrada. En el segundo de cero que
     * abre y cierra la secuencia el PI no puede actuar; mientras la pastilla se
     * moja, la presión se queda en la meseta de la canastilla haga lo que haga
     * la bomba. En los dos casos el PI cargaría la integral contra un error que
     * la bomba no corrige, y con la molienda fina eso llevó la presión a 12 bar.
     * Fuera, la bomba va al techo y la compuerta mantiene el PI cebado en la
     * medida; al cerrar la pastilla arranca desde la integral de la
     * preinfusión, no de cero ni del techo.
     */
    const float tuning_ceiling =
        identifying && tuning_ready ? supervisor->identification.demand_percent : 0.0F;
#else
    const float tuning_ceiling = 0.0F;
#endif
    /*
     * En operación la preinfusión hace lo mismo con el techo del acta: bomba
     * al techo mientras la pastilla se moja, PI desde el cierre. Sin ella en el
     * acta el PI regula desde el principio, que es lo que había antes.
     */
    const float preinfusion_ceiling =
        identifying ? tuning_ceiling
                    : (allowed && s_preinfusion_enabled ? s_pressure.tuning.pi.output_maximum
                                                        : 0.0F);
    const pressure_preinfusion_inputs_t preinfusion_inputs = {
        .ceiling_percent = preinfusion_ceiling,
        .setpoint_bar = pressure_setpoint(supervisor),
        .pressure_bar = sample.quality == SIGNAL_VALID
                            ? control_filter_push(&s_preinfusion_filter, converted)
                            : NAN,
    };
    if (sample.quality != SIGNAL_VALID) {
        control_filter_reset(&s_preinfusion_filter);
    }
    const pressure_preinfusion_decision_t preinfusion =
        pressure_preinfusion_step(&s_preinfusion, &preinfusion_inputs);
#if PRESSURE_TUNING
    const bool tuning_active = identifying && tuning_ready && preinfusion.regulate;
    const bool pump_tuning_ready = tuning_ready;
#else
    const bool tuning_active = false;
    const bool pump_tuning_ready = false;
#endif
    const bool wetting = !identifying && !hydraulic && allowed && s_preinfusion_enabled &&
                         !preinfusion.regulate;
    /* Identificar no es cerrar el lazo: el PI se queda cebado en la medida y la
     * salida la fija la secuencia (ADR-007 § 5, enmienda del 2026-09-21). La
     * sintonía es la excepción. En operación, mientras la pastilla se moja
     * tampoco: la bomba va al techo del acta. */
    const bool closing = allowed && !hydraulic && (identifying ? tuning_active : !wetting);
#endif

    const bool gated = update_gate(&s_pressure, CONTROL_LOG_LOOP_PRESSURE, closing,
                                   converted, verdict);
#if !BENCH_LOOP
    /* La compuerta acaba de reiniciar el PI con la integral en cero, y el
     * evento `loop-enabled` del registro marca el cierre de la pastilla. */
    if (gated && preinfusion.hand_over) {
        pi_controller_reset_with_integral(&s_pressure.controller, converted,
                                          preinfusion.integral_start_percent);
    }
#endif
    /* Durante la identificación ninguna referencia gobierna nada, y el registro
     * no debe sugerir lo contrario. En la sintonía sí: la del perfil, que el
     * estado de extracción ya trae. */
    const float authorized = tuning_active                ? pressure_setpoint(supervisor)
                             : identifying || hydraulic ? 0.0F
                                                        : pressure_setpoint(supervisor);
    float filtered = converted;
    pi_result_t result = {0};
    if (gated) {
        filtered = control_filter_push(&s_pressure.filter, converted);
        result = pi_controller_update(&s_pressure.controller, authorized, filtered, 0.0F);
    } else {
        /* Sin permiso, el lazo se mantiene cebado en la medida en vez de
         * calcular contra un error que no podrá corregir. */
        control_filter_reset(&s_pressure.filter);
        pi_controller_reset(&s_pressure.controller, converted);
        result.setpoint = converted;
    }
#if !BENCH_LOOP
    if (wetting) {
        result.output = preinfusion_ceiling;
        result.unlimited = result.output;
    }
#endif
    if (identifying) {
        const float ceiling = supervisor->identification.demand_percent;
        if (tuning_active) {
            /* El guardián juzgó el techo, no la salida del PI. */
            result.output = result.output < ceiling ? result.output : ceiling;
        } else if (PRESSURE_TUNING) {
            /* Mojando la pastilla, la bomba va al techo que el guardián admitió;
             * sin planta configurada, a cero. */
            result.output = pump_tuning_ready ? ceiling : 0.0F;
            result.unlimited = result.output;
        } else {
            /* La demanda que el guardián admitió, sin pasar por el PI. */
            result.output = ceiling;
            result.unlimited = result.output;
        }
    }

    if (hydraulic) {
        /* La orden elegida, sin pasar por el PI. Cero si no se eligió ninguna. */
        result.output = (float)hydraulic_order;
        result.unlimited = result.output;
    }

    const uint8_t density = allowed ? psm_scheduler_quantize(result.output) : 0U;
    zero_cross_runtime_set_pump_demand(density, allowed);
    s_pump_density = density;
    s_pump_allowed = allowed;

#if BENCH_LOOP
    /*
     * El modelo recibe la densidad cuantizada, no la salida continua del PI:
     * así se ejercita la resolución del 1 % que decide ADR-007 § 3.3. El rizado
     * de la emisión discreta no se reproduce aquí porque la interrupción no
     * ofrece oportunidades mientras el guardián inhibe; eso se verifica en el
     * anfitrión, en `test_control_loop`, donde los 120 Hz se simulan exactos.
     */
    (void)control_plant_step(&s_pressure.plant, (float)density);
#endif

    fill_common(&sample, CONTROL_LOG_LOOP_PRESSURE, supervisor, verdict, allowed,
                &result, authorized);
    sample.commissioned = s_pressure.tuning.commissioned;
#if PRESSURE_TUNING
    snprintf(sample.tuning, sizeof(sample.tuning), "%s", tuning_identifier);
#elif HYDRAULIC_TEST
    /* Cada línea declara que la bomba no la gobierna el acta. */
    snprintf(sample.tuning, sizeof(sample.tuning), "%s", "hydraulic-test-v1");
#else
    snprintf(sample.tuning, sizeof(sample.tuning), "%s", s_pressure.tuning.identifier);
#endif
    sample.signal.converted = converted;
    sample.signal.filtered = filtered;
    sample.specific.pressure.density = density;
    (void)zero_cross_runtime_get_pump_statistics(&sample.specific.pressure.opportunities,
                                                 &sample.specific.pressure.emitted,
                                                 &sample.specific.pressure.residue, NULL);
    sample.specific.pressure.enabled_ms = supervisor->pump_service.enabled_ms;
    sample.specific.pressure.rest_ms = supervisor->pump_service.rest_ms;
    control_log_runtime_sample(&sample);

    portENTER_CRITICAL(&s_lock);
    s_status.pressure_measurement = converted;
    s_status.pressure_output = result.output;
    s_status.pressure_density = density;
    s_status.pressure_verdict = verdict;
    s_status.pressure_resets = s_pressure.resets;
    portEXIT_CRITICAL(&s_lock);
}

static void run_temperature(const supervisor_status_t *supervisor, float *previous_filtered)
{
    const actuation_verdict_t verdict = supervisor->actuation.heater;
    const bool allowed = actuation_verdict_allows(verdict);
    const bool identifying = verdict == ACTUATION_ALLOWED_IDENTIFICATION;

    control_log_sample_t sample = {0};
    float converted = 0.0F;

#if BENCH_LOOP
    converted = control_plant_value(&s_temperature.plant);
    sample.source = PROCESS_SOURCE_SIMULATED;
    sample.quality = SIGNAL_VALID;
    const bool closing = true;
    /* El lazo de banco no existe en el binario de identificación. */
    const bool tuning_active = false;
#else
    measurement_t measurement = {0};
    const bool have_measurement = acquisition_runtime_get_temperature(&measurement);
    converted = have_measurement ? measurement.raw_value : 0.0F;
    sample.source = have_measurement ? PROCESS_SOURCE_MEASURED : PROCESS_SOURCE_NONE;
    sample.quality = have_measurement ? measurement.quality : SIGNAL_NOT_ACQUIRED;
    sample.signal.counts = measurement.counts;
    sample.signal.volts = measurement.volts;
#if THERMAL_TUNING
    /* Con la sintonía, identificar es regular: el PI decide y la secuencia
     * solo pone el techo. Sin planta configurada no regula ni aplica el techo
     * en lazo abierto: el calentador queda en cero. */
    pi_parameters_t tuning_parameters;
    bool tuning_ready;
    bool tuning_dirty;
    char tuning_identifier[CONTROL_TUNING_ID_BYTES];
    portENTER_CRITICAL(&s_lock);
    s_tuning_confirmed = supervisor->identification.phase == IDENTIFICATION_RUNNING;
    s_tuning_verdict = verdict;
    tuning_ready = s_tuning.configured;
    tuning_dirty = s_tuning_dirty;
    s_tuning_dirty = false;
    tuning_parameters = identification_tuning_parameters(&s_tuning);
    memcpy(tuning_identifier, s_tuning_identifier, sizeof(tuning_identifier));
    portEXIT_CRITICAL(&s_lock);
    if (tuning_dirty && tuning_ready) {
        /* Cada cambio arranca el PI desde la medida: la referencia interna sube
         * otra vez con su limitador y la integral parte de cero, de modo que
         * cada prueba empieza desde un estado conocido y se lee sola. */
        (void)pi_controller_configure(&s_temperature.controller, &tuning_parameters);
        pi_controller_reset(&s_temperature.controller, converted);
    }
    const bool tuning_active = identifying && tuning_ready;
#else
    const bool tuning_active = false;
#endif
    const bool closing = allowed && (!identifying || tuning_active);
#endif

    const bool gated = update_gate(&s_temperature, CONTROL_LOG_LOOP_TEMPERATURE, closing,
                                   converted, verdict);
    /* En la sintonía la referencia es la de café aunque el estado operativo
     * siga en reposo: el modo restringido no pasa por «calentando». */
    const float authorized = tuning_active ? supervisor->limits.coffee_setpoint_c
                             : identifying ? 0.0F
                                           : temperature_setpoint(supervisor);
    float filtered = converted;
    pi_result_t result = {0};
    if (gated) {
        filtered = control_filter_push(&s_temperature.filter, converted);
        /* `ff-v1` (ADR-009 § 3.4) por la puerta 2 (enmienda del 2026-10-12):
         * el PI se calcula sin ella y satura solo su parte en su tope; la
         * potencia que la bomba se va a llevar se suma después, desde el ciclo
         * en que arranca y hasta el 100 %. Sin ganancia en el acta, cero. */
        /* El refuerzo del primer uso, si el supervisor lo tiene armado para
         * esta preparación; si no, la del acta. */
        s_heater_feedforward.gain = heater_feedforward_gain_for(
            s_heater_feedforward_base_gain, supervisor->first_use_feedforward_gain);
        const bool had_water = !heater_feedforward_no_water(&s_heater_feedforward);
        const float feedforward = heater_feedforward_step(
            &s_heater_feedforward, s_pump_density, s_pump_allowed, filtered,
            supervisor->limits.ready_tolerance_c);
        if (had_water && heater_feedforward_no_water(&s_heater_feedforward)) {
            ESP_LOGW(TAG,
                     "Prealimentación cortada: con la bomba el metal subió de %.2f a %.2f °C; "
                     "no entra agua",
                     (double)s_heater_feedforward.start_temperature_c, (double)filtered);
        }
        const bool hold_integral =
            HEATER_RECOVERY_HOLD_ENABLED &&
            heater_recovery_hold_step(&s_heater_recovery_hold, s_pump_allowed, authorized, filtered);
        result = pi_controller_update_holding(&s_temperature.controller, authorized, filtered,
                                              0.0F, hold_integral);
        const heater_recovery_return_decision_t returning = heater_recovery_return_step(
            &s_heater_recovery_return, s_pump_allowed, result.integral,
            (float)(CONTROL_PERIOD_MS * THERMAL_DECIMATION) / 1000.0F);
        if (returning.apply) {
            pi_controller_relax_integral(&s_temperature.controller, returning.target,
                                         returning.fraction);
        }
        result.feedforward = feedforward;
        result.unlimited += feedforward;
        result.output = heater_feedforward_apply(result.output, feedforward);
    } else {
        control_filter_reset(&s_temperature.filter);
        pi_controller_reset(&s_temperature.controller, converted);
        heater_feedforward_reset(&s_heater_feedforward);
        heater_recovery_hold_reset(&s_heater_recovery_hold);
        heater_recovery_return_reset(&s_heater_recovery_return);
        result.setpoint = converted;
        /* El modulador solo se olvida cuando la salida se corta. Identificando
         * sigue modulando la demanda de la secuencia, y reiniciarlo cada 200 ms
         * le haría perder el residuo que da la resolución fina. */
        if (!allowed) {
            heater_modulator_reset(&s_heater);
        }
    }
    /*
     * El arranque rápido decide después del PI y antes de cuantizar. Mientras
     * gobierna, el PI se reinicia cada ciclo desde la medida: no acumula
     * integral con una salida que no es la suya, y al recibir el mando parte
     * de cero, como pide la enmienda. Nunca en identificación del calentador:
     * ahí la salida es la de la secuencia o la de la sintonía.
     */
    const float thermal_period_s = (float)(CONTROL_PERIOD_MS * THERMAL_DECIMATION) / 1000.0F;
    const heater_fast_start_inputs_t fast_start_inputs = {
        .heating = !identifying && (supervisor->state == OP_STATE_COFFEE_HEATING ||
                                    supervisor->state == OP_STATE_READY),
        .measurement_valid = gated && sample.quality == SIGNAL_VALID,
        .pump_allowed = s_pump_allowed,
        .setpoint_c = authorized,
        .temperature_c = filtered,
        .elapsed_s = thermal_period_s,
    };
    const heater_fast_start_decision_t fast_start =
        heater_fast_start_step(&s_heater_fast_start, &fast_start_inputs);
    if (fast_start.overrides) {
        result.output = fast_start.output;
        result.unlimited = fast_start.output;
        pi_controller_reset(&s_temperature.controller, filtered);
        heater_recovery_return_reset(&s_heater_recovery_return);
    } else if (fast_start.hand_over) {
        pi_controller_reset(&s_temperature.controller, filtered);
    }

    if (identifying) {
        const float ceiling = supervisor->identification.demand_percent;
        if (tuning_active) {
            /* El guardián juzgó el techo, no la salida del PI: la salida nunca
             * pasa de lo que él admitió en este ciclo. */
            result.output = result.output < ceiling ? result.output : ceiling;
        } else if (THERMAL_TUNING) {
            result.output = 0.0F;
            result.unlimited = 0.0F;
        } else {
            result.output = ceiling;
            result.unlimited = result.output;
        }
    }

    const uint8_t demand = allowed ? heater_modulator_quantize(result.output) : 0U;

#if BENCH_LOOP
    /*
     * El modelo recibe la conducción que el modulador emitió de verdad durante
     * los diez pasos anteriores, no la demanda. Es lo que hace que la modulación
     * por ciclo integral quede ejercitada con su granularidad real.
     */
    const float applied_percent =
        100.0F * (float)s_heater_conducting_ticks / (float)THERMAL_DECIMATION;
    (void)control_plant_step(&s_temperature.plant, applied_percent);
#endif
    s_heater_conducting_ticks = 0U;

    /* Velocidad de ascenso observada, que ADR-009 § 3.9 obliga a registrar y de
     * la que saldrá el umbral de la protección por caldera seca. */
    const float rise_rate = (filtered - *previous_filtered) / thermal_period_s;
    *previous_filtered = filtered;

    fill_common(&sample, CONTROL_LOG_LOOP_TEMPERATURE, supervisor, verdict, allowed,
                &result, authorized);
    sample.commissioned = s_temperature.tuning.commissioned;
#if THERMAL_TUNING
    snprintf(sample.tuning, sizeof(sample.tuning), "%s", tuning_identifier);
#else
    snprintf(sample.tuning, sizeof(sample.tuning), "%s", s_temperature.tuning.identifier);
#endif
    sample.signal.converted = converted;
    sample.signal.filtered = filtered;
    sample.specific.temperature.window_ms = HEATER_WINDOW_MS;
    sample.specific.temperature.requested_ms = heater_modulator_target_ms(&s_heater);
    sample.specific.temperature.applied_ms = heater_modulator_applied_ms(&s_heater);
    sample.specific.temperature.residue_ms = heater_modulator_residue_ms(&s_heater);
    sample.specific.temperature.rise_rate_c_s = rise_rate;
    sample.specific.temperature.fast_start =
        heater_fast_start_phase_name(heater_fast_start_phase(&s_heater_fast_start));
    control_log_runtime_sample(&sample);

    portENTER_CRITICAL(&s_lock);
    s_status.temperature_measurement = converted;
    s_status.temperature_output = result.output;
    s_status.temperature_demand = demand;
    s_status.temperature_verdict = verdict;
    s_status.temperature_resets = s_temperature.resets;
    s_heater_demand = demand;
    portEXIT_CRITICAL(&s_lock);
}

/*
 * Conmuta el calentador dentro de la ventana. Corre cada 20 ms, no cada 200:
 * la resolución de la modulación es la del paso con que se conmuta, y con el
 * periodo del lazo térmico sería del 20 %.
 */
static void step_heater_output(bool allowed)
{
    const uint8_t demand = allowed ? s_heater_demand : 0U;
    const bool conduct = heater_modulator_step(&s_heater, demand);
    if (conduct) {
        s_heater_conducting_ticks++;
    }
    /* Solo cuenta lo que llega a la salida: con la actuación no autorizada el
     * modulador sigue su cuenta, pero el calentador no conduce. */
    if (conduct && allowed) {
        s_heater_applied_ms += CONTROL_PERIOD_MS;
    }
    /* El guardián lleva la salida a reposo por su propia ruta cada 50 ms; esta
     * es la del lazo, y ninguna de las dos depende de la otra. */
    (void)gpio_set_level(PIN_HEATER_CMD, (conduct && allowed) ? 1 : 0);
}

static void control_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t tick = 0U;
    float previous_thermal_filtered = 0.0F;

    ESP_LOGI(TAG, "Lazos de control iniciados; núcleo=%d; periodo=%ums; lazo de banco=%s",
             (int)xPortGetCoreID(), (unsigned)CONTROL_PERIOD_MS, BENCH_LOOP ? "sí" : "no");
    (void)esp_task_wdt_add(NULL);

    while (true) {
        const int64_t started_us = esp_timer_get_time();

        supervisor_status_t supervisor = {0};
        if (supervisor_runtime_get_status(&supervisor)) {
            run_pressure(&supervisor);
            if ((tick % THERMAL_DECIMATION) == 0U) {
                run_temperature(&supervisor, &previous_thermal_filtered);
            }
            step_heater_output(actuation_verdict_allows(supervisor.actuation.heater));
        } else {
            /* Sin supervisor no hay veredicto, y sin veredicto no se actúa. */
            zero_cross_runtime_set_pump_demand(0U, false);
            (void)gpio_set_level(PIN_HEATER_CMD, 0);
        }

        const int64_t elapsed_us = esp_timer_get_time() - started_us;
        portENTER_CRITICAL(&s_lock);
        s_status.cycles++;
        s_status.heater_applied_ms = s_heater_applied_ms;
        if ((tick % THERMAL_DECIMATION) == 0U) {
            s_status.thermal_cycles++;
        }
        if (elapsed_us > (int64_t)CONTROL_PERIOD_MS * 1000) {
            s_status.deadline_misses++;
        }
        portEXIT_CRITICAL(&s_lock);

        tick++;
        (void)esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
    }
}

static bool configure_loop(loop_t *loop, const control_tuning_t *tuning)
{
    loop->tuning = *tuning;
    if (!control_filter_configure(&loop->filter, tuning->filter_time_constant_s,
                                  tuning->sample_period_s)) {
        return false;
    }
    return pi_controller_configure(&loop->controller, &tuning->pi);
}

#if !BENCH_LOOP && (PRESSURE_TUNING || (HAVE_COMMISSIONED_PRESSURE && \
                                       defined(COMMISSIONED_PRESSURE_PREINFUSION_MARGIN_BAR)))
static void configure_preinfusion(float margin_bar, float filter_time_constant_s,
                                  float integral_start_percent,
                                  pressure_preinfusion_start_t start, float sample_period_s,
                                  const char *source)
{
    const pressure_preinfusion_parameters_t parameters = {
        .handover_margin_bar = margin_bar,
        .integral_start_percent = integral_start_percent,
        .start = start,
    };
    s_preinfusion_enabled =
        pressure_preinfusion_configure(&s_preinfusion, &parameters) &&
        control_filter_configure(&s_preinfusion_filter, filter_time_constant_s, sample_period_s);
    if (s_preinfusion_enabled && start == PRESSURE_PREINFUSION_START_CEILING) {
        ESP_LOGW(TAG, "preinfusión (%s): bomba al techo hasta referencia - %.2f bar (filtro "
                      "de %.0f ms); el PI arranca desde el techo, sin salto",
                 source, (double)margin_bar, (double)(filter_time_constant_s * 1000.0F));
    } else if (s_preinfusion_enabled) {
        ESP_LOGW(TAG, "preinfusión (%s): bomba al techo hasta referencia - %.2f bar (filtro "
                      "de %.0f ms); el PI arranca con la integral en %.0f %%",
                 source, (double)margin_bar, (double)(filter_time_constant_s * 1000.0F),
                 (double)integral_start_percent);
    } else {
        ESP_LOGE(TAG, "preinfusión de %s no válida: el PI regula desde el principio", source);
    }
}
#endif

esp_err_t control_runtime_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

#if PRESSURE_TUNING
    control_tuning_t pressure = control_tuning_pressure();
    {
        /* De la 6.5 y la 6.6 con café: la subida rápida y el retardo del
         * escalón, y la ganancia entre el 50 y el 75 % (enmienda a ADR-007 § 5
         * del 2026-10-12). Provisional; se fija en el acta de presión. */
        const control_identified_plant_t plant = {
            .identifier = "identification-provisional",
            .gain = (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_TUNING_GAIN_MILLI / 1000.0F,
            .time_constant_s =
                (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_TUNING_TIME_CONSTANT_MS / 1000.0F,
            .dead_time_s = (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_TUNING_DEAD_TIME_MS / 1000.0F,
            .lambda_s = (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_TUNING_LAMBDA_MS / 1000.0F,
            .filter_time_constant_s =
                (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_TUNING_FILTER_MS / 1000.0F,
            .setpoint_rate =
                (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_TUNING_SETPOINT_RATE_MILLI / 1000.0F,
        };
        const float ceiling = (float)stimulus_sequence_peak_percent(
            stimulus_sequence_find(STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1));
        if (identification_tuning_configure(&s_tuning, &plant, pressure.sample_period_s,
                                            ceiling)) {
            pressure.filter_time_constant_s = plant.filter_time_constant_s;
            pressure.pi = identification_tuning_parameters(&s_tuning);
        } else {
            ESP_LOGE(TAG, "sintonía de presión sin configurar: planta provisional no "
                          "utilizable; la bomba no regulará");
        }
        configure_preinfusion(
            (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_PREINFUSION_MARGIN_MBAR / 1000.0F,
            (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_PREINFUSION_FILTER_MS / 1000.0F,
            (float)CONFIG_CAFE_IDENTIFICATION_PRESSURE_PREINFUSION_INTEGRAL_PERCENT,
            PRESSURE_PREINFUSION_START_FIXED, pressure.sample_period_s, "ensayo supervisado");
        (void)identification_tuning_identifier(&s_tuning, s_tuning_identifier,
                                               sizeof(s_tuning_identifier));
    }
#else
    const control_tuning_t pressure = control_tuning_pressure();
#if !BENCH_LOOP && HAVE_COMMISSIONED_PRESSURE && \
    defined(COMMISSIONED_PRESSURE_PREINFUSION_MARGIN_BAR)
    /* El acta elige cómo arranca el PI; una planta que no lo declara arranca
     * desde la integral fija, como todas las firmadas antes del 2026-11-06. */
#if defined(COMMISSIONED_PRESSURE_PREINFUSION_START_FROM_CEILING)
    const pressure_preinfusion_start_t preinfusion_start = PRESSURE_PREINFUSION_START_CEILING;
#else
    const pressure_preinfusion_start_t preinfusion_start = PRESSURE_PREINFUSION_START_FIXED;
#endif
    configure_preinfusion(COMMISSIONED_PRESSURE_PREINFUSION_MARGIN_BAR,
                          COMMISSIONED_PRESSURE_PREINFUSION_FILTER_TIME_CONSTANT_S,
                          COMMISSIONED_PRESSURE_PREINFUSION_INTEGRAL_PERCENT, preinfusion_start,
                          pressure.sample_period_s, COMMISSIONED_PRESSURE_IDENTIFIER);
#endif
#endif
#if THERMAL_TUNING
    control_tuning_t temperature = control_tuning_temperature();
    {
        const control_identified_plant_t plant = {
            .identifier = "identification-provisional",
            .gain = (float)CONFIG_CAFE_IDENTIFICATION_TUNING_GAIN_MILLI / 1000.0F,
            .time_constant_s = (float)CONFIG_CAFE_IDENTIFICATION_TUNING_TIME_CONSTANT_S,
            .dead_time_s = (float)CONFIG_CAFE_IDENTIFICATION_TUNING_DEAD_TIME_MS / 1000.0F,
            .lambda_s = (float)CONFIG_CAFE_IDENTIFICATION_TUNING_LAMBDA_S,
            .filter_time_constant_s = (float)CONFIG_CAFE_IDENTIFICATION_TUNING_FILTER_MS / 1000.0F,
            .setpoint_rate = (float)CONFIG_CAFE_IDENTIFICATION_TUNING_SETPOINT_RATE_MILLI / 1000.0F,
        };
        const float ceiling = (float)stimulus_sequence_peak_percent(
            stimulus_sequence_find(STIMULUS_SEQUENCE_HEATER_TUNING_10PCT_V1));
        if (identification_tuning_configure(&s_tuning, &plant, temperature.sample_period_s,
                                            ceiling)) {
            temperature.filter_time_constant_s = plant.filter_time_constant_s;
            temperature.pi = identification_tuning_parameters(&s_tuning);
        } else {
            ESP_LOGE(TAG, "sintonía en caliente sin configurar: planta provisional no "
                          "utilizable; el calentador no regulará");
        }
        (void)identification_tuning_identifier(&s_tuning, s_tuning_identifier,
                                               sizeof(s_tuning_identifier));
    }
#else
    const control_tuning_t temperature = control_tuning_temperature();
#endif
    if (!configure_loop(&s_pressure, &pressure) ||
        !configure_loop(&s_temperature, &temperature)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!heater_modulator_configure(&s_heater, HEATER_WINDOW_MS, CONTROL_PERIOD_MS)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *feedforward_source = NULL;
    const float feedforward_gain = heater_feedforward_configured_gain(&feedforward_source);
    if (!heater_feedforward_configure(&s_heater_feedforward, feedforward_gain)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_heater_feedforward_base_gain = s_heater_feedforward.gain;
    ESP_LOGI(TAG, "Prealimentación del calentador ff-v1: %.3f %% por cada %% de bomba (%s)%s",
             (double)s_heater_feedforward.gain, feedforward_source,
             s_heater_feedforward.gain > 0.0F ? "" : "; puerta 2 cerrada");
    heater_recovery_hold_reset(&s_heater_recovery_hold);
    ESP_LOGI(TAG, "Retención de la integral térmica tras el café: %s (%s)",
             HEATER_RECOVERY_HOLD_ENABLED ? "sí" : "no", HEATER_RECOVERY_HOLD_SOURCE);
    if (!heater_recovery_return_configure(&s_heater_recovery_return,
                                          HEATER_RECOVERY_RETURN_TAU_S)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Devolución de la integral térmica tras el café: τ_eq=%.0f s (%s)",
             (double)s_heater_recovery_return.tau_s, HEATER_RECOVERY_RETURN_SOURCE);
    configure_fast_start();
#if HYDRAULIC_TEST
    {
        const uint8_t orders[HYDRAULIC_TEST_ORDERS] = {
            (uint8_t)CONFIG_CAFE_HYDRAULIC_TEST_ORDER_1,
            (uint8_t)CONFIG_CAFE_HYDRAULIC_TEST_ORDER_2,
            (uint8_t)CONFIG_CAFE_HYDRAULIC_TEST_ORDER_3,
        };
        /* El techo del acta acota las órdenes: no se lleva la bomba a una zona
         * que el acta no vio. Si no caben, el ensayo queda sin configurar y la
         * bomba en cero, lo que también se ve en el registro. */
        if (hydraulic_test_configure(&s_hydraulic, orders, pressure.pi.output_maximum)) {
            ESP_LOGW(TAG, "ENSAYO HIDRÁULICO: bomba a orden fija en la extracción (%u, %u o "
                          "%u %%, con H,order,<n>); sin orden, en cero. Presión: %s; "
                          "temperatura: %s",
                     (unsigned)orders[0], (unsigned)orders[1], (unsigned)orders[2],
                     pressure.identifier, temperature.identifier);
        } else {
            ESP_LOGE(TAG, "ensayo hidráulico sin configurar: órdenes %u, %u y %u frente a un "
                          "techo de %.0f %%; la bomba no se moverá en la extracción",
                     (unsigned)orders[0], (unsigned)orders[1], (unsigned)orders[2],
                     (double)pressure.pi.output_maximum);
        }
    }
#endif

#if BENCH_LOOP
    const control_plant_parameters_t pressure_plant = control_plant_pressure_bench();
    control_plant_parameters_t temperature_plant = control_plant_temperature_bench();
    /* La planta térmica se avanza con el periodo del lazo térmico, que es el
     * que la decimación le da de verdad. */
    temperature_plant.sample_period_s =
        (float)(CONTROL_PERIOD_MS * THERMAL_DECIMATION) / 1000.0F;
    if (!control_plant_configure(&s_pressure.plant, &pressure_plant) ||
        !control_plant_configure(&s_temperature.plant, &temperature_plant)) {
        return ESP_ERR_INVALID_ARG;
    }
    control_plant_reset(&s_pressure.plant, pressure_plant.rest_value);
    control_plant_reset(&s_temperature.plant, temperature_plant.rest_value);
#endif

    s_status.bench_loop = BENCH_LOOP ? true : false;
    s_status.pressure_tuning = pressure.identifier;
    s_status.temperature_tuning = temperature.identifier;
    s_status.pressure_commissioned = pressure.commissioned;
    s_status.temperature_commissioned = temperature.commissioned;
    s_status.pressure_verdict = ACTUATION_UNDECIDED;
    s_status.temperature_verdict = ACTUATION_UNDECIDED;

    control_log_runtime_event(CONTROL_LOG_EVENT_SESSION_OPENED,
                              CONTROL_LOG_LOOP_PRESSURE, 0U);

    const BaseType_t created =
        xTaskCreatePinnedToCore(control_task, "control", CONTROL_TASK_STACK_BYTES, NULL,
                                CONTROL_TASK_PRIORITY, &s_task, CONTROL_TASK_CORE);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_status.running = true;
    return ESP_OK;
}

bool control_runtime_get_status(control_runtime_status_t *status)
{
    if (status == NULL || s_task == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

identification_tuning_result_t control_runtime_tuning_command(
    const identification_tuning_command_t *command, control_runtime_tuning_report_t *report)
{
    if (command == NULL || report == NULL) {
        return IDENTIFICATION_TUNING_REJECTED_COMMAND;
    }
    *report = (control_runtime_tuning_report_t){0};
#if LIVE_TUNING
    identification_tuning_result_t result;
    portENTER_CRITICAL(&s_lock);
    if (command->kind != IDENTIFICATION_TUNING_COMMAND_SHOW && !s_tuning_confirmed) {
        result = IDENTIFICATION_TUNING_REJECTED_NOT_CONFIRMED;
    } else {
        result = identification_tuning_apply(&s_tuning, command);
        if (result == IDENTIFICATION_TUNING_ACCEPTED &&
            command->kind != IDENTIFICATION_TUNING_COMMAND_SHOW) {
            s_tuning_dirty = true;
            (void)identification_tuning_identifier(&s_tuning, s_tuning_identifier,
                                                   sizeof(s_tuning_identifier));
        }
    }
    const pi_parameters_t parameters = identification_tuning_parameters(&s_tuning);
    memcpy(report->identifier, s_tuning_identifier, sizeof(report->identifier));
    report->lambda_s = s_tuning.lambda_s;
    report->integral_enabled = s_tuning.integral_enabled;
    report->proportional_gain = parameters.proportional_gain;
    report->integral_gain = parameters.integral_gain;
    report->ceiling_percent = s_tuning.ceiling_percent;
    report->verdict = s_tuning_verdict;
    portEXIT_CRITICAL(&s_lock);
    return result;
#else
    (void)command;
    snprintf(report->identifier, sizeof(report->identifier), "%s", "unavailable");
    return IDENTIFICATION_TUNING_REJECTED_NOT_CONFIGURED;
#endif
}

hydraulic_test_result_t control_runtime_hydraulic_command(
    const hydraulic_test_command_t *command, control_runtime_hydraulic_report_t *report)
{
    if (command == NULL || report == NULL) {
        return HYDRAULIC_TEST_REJECTED_COMMAND;
    }
    *report = (control_runtime_hydraulic_report_t){0};
#if HYDRAULIC_TEST
    supervisor_status_t supervisor = {0};
    const bool have_state = supervisor_runtime_get_status(&supervisor);
    hydraulic_test_result_t result = HYDRAULIC_TEST_ACCEPTED;
    portENTER_CRITICAL(&s_lock);
    if (command->kind == HYDRAULIC_TEST_COMMAND_ORDER) {
        /* Sin estado conocido no se sabe si la bomba está quieta. */
        result = have_state ? hydraulic_test_select(&s_hydraulic, command->percent,
                                                    supervisor.state)
                            : HYDRAULIC_TEST_REJECTED_PUMP_RUNNING;
    } else if (command->kind != HYDRAULIC_TEST_COMMAND_SHOW) {
        result = HYDRAULIC_TEST_REJECTED_COMMAND;
    } else if (!s_hydraulic.configured) {
        result = HYDRAULIC_TEST_REJECTED_NOT_CONFIGURED;
    }
    report->selected = s_hydraulic.selected;
    report->applied = s_hydraulic.applied;
    memcpy(report->orders, s_hydraulic.orders, sizeof(report->orders));
    portEXIT_CRITICAL(&s_lock);
    return result;
#else
    (void)command;
    return HYDRAULIC_TEST_REJECTED_NOT_CONFIGURED;
#endif
}
