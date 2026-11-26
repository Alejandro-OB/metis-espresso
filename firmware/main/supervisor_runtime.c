#include "supervisor_runtime.h"

#include <inttypes.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "acquisition_runtime.h"
#include "commissioned_plant.h"
#include "control_log_runtime.h"
#include "control_tuning.h"
#include "first_use_offset.h"
#include "first_use_offset_runtime.h"
#include "operational_runtime.h"
#include "zero_cross_runtime.h"
#include "panel_runtime.h"
#include "pin_map.h"
#include "profile_runtime.h"
#include "pump_ladder.h"
#include "water_reference_runtime.h"
#include "simulation_runtime.h"

#define SUPERVISOR_TASK_STACK_BYTES 4096U
#define SUPERVISOR_TASK_PRIORITY (tskIDLE_PRIORITY + 7U)
#define SUPERVISOR_TASK_CORE 1    /* junto a la adquisición que lo alimenta */
#define SUPERVISOR_PERIOD_MS 50U  /* cinco ciclos de adquisición de presión */
/*
 * Plazo de la vigilancia de adquisición.
 *
 * No es el mismo criterio que la antigüedad de cada señal: aquí se mide la más
 * reciente de las dos, de modo que el número detecta que la tarea de
 * adquisición se ha detenido, no que una señal concreta haya envejecido. Diez
 * ciclos de presión y dos del propio supervisor: suficiente para no disparar
 * por jitter y lo bastante corto para cortar en una décima de segundo.
 */
#define SUPERVISOR_MAXIMUM_ACQUISITION_AGE_MS 100U
/*
 * Puesta en servicio de cada lazo, decidida al compilar (ADR-020 § 4.2).
 *
 * Sale de la sintonía, que solo es verdadera si el firmware lleva el archivo de
 * planta de ese lazo generado desde su acta. Se lee una vez al arrancar y no
 * cambia: ninguna orden en ejecución la toca.
 */
static bool s_pressure_commissioned;
static bool s_temperature_commissioned;

/*
 * Un lazo puesto en servicio energiza la máquina de verdad, y no puede hacerlo
 * gobernado por el modelo de banco ni vigilado sobre una planta simulada: la
 * actuación saldría a la caldera real con un lazo que mira otra cosa. Se impide
 * al compilar porque en ejecución ya sería tarde.
 */
#if (HAVE_COMMISSIONED_PRESSURE || HAVE_COMMISSIONED_TEMPERATURE) && \
    (CONFIG_CAFE_CONTROL_BENCH_LOOP || CONFIG_CAFE_SIMULATION)
#error "Un firmware con planta identificada no admite el lazo de banco ni la simulación"
#endif

/*
 * La secuencia del modo restringido se elige al compilar y solo existe en el
 * binario de identificación. En el de operación no hay ninguna, y el modo queda
 * inhabilitado sin que nada en ejecución pueda cambiarlo.
 */
#if CONFIG_CAFE_IDENTIFICATION
#if CONFIG_CAFE_IDENTIFICATION_HEATER_STEP_1PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_STEP_1PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_STEP_2PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_STEP_3PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_STEP_3PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_STEP_5PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_STEP_10PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_STEP_10PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_STEP_2PCT_V2
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V2
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_STEP_5PCT_V2
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V2
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_1PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_2PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_3PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_3PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_5PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_5PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_10PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_10PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_20PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_20PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_30PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_30PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_50PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_50PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_STEP_75PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_75PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_TUNING_10PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_TUNING_10PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_DIAGNOSTIC_50PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_50PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_DIAGNOSTIC_100PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_100PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_HEATER_PULSE_100PCT_V1
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_HEATER_PULSE_100PCT_V1
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_LADDER_V1
/* Arranca en el primer escalón; los demás se eligen por el puerto. */
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1
#define PUMP_LADDER 1
#define PUMP_LADDER_VARIANT PUMP_LADDER_HYDRAULIC
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_THERMAL_LADDER_V1
/* La de la 7.9: escalones de 25 s a 10, 30 y 50 %. */
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_THERMAL_10PCT_V1
#define PUMP_LADDER 1
#define PUMP_LADDER_VARIANT PUMP_LADDER_THERMAL
#elif CONFIG_CAFE_IDENTIFICATION_PUMP_TUNING_75PCT_V1
/* La sintonía de presión: una extracción por prueba, re-armada por el puerto. */
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1
#define PUMP_LADDER 1
#define PUMP_LADDER_VARIANT PUMP_LADDER_PRESSURE_TUNING
#else
#error "Binario de identificación sin secuencia de estímulo"
#endif
#define IDENTIFICATION_MAXIMUM_PERCENT ((float)CONFIG_CAFE_IDENTIFICATION_MAXIMUM_PERCENT)
#else
#define IDENTIFICATION_SEQUENCE STIMULUS_SEQUENCE_NONE
#define IDENTIFICATION_MAXIMUM_PERCENT 0.0F
#endif

static const char *TAG = "supervisor";

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static supervisor_status_t s_status;
static process_supervisor_t s_supervisor;
static process_limits_t s_limits;
static bool s_clear_requested;
/* Actualización de firmware en marcha (ADR-021): el guardián corta las dos
 * salidas mientras dure. */
static bool s_updating;
static actuation_guard_t s_guard;
static identification_mode_t s_identification;
static identification_setup_t s_identification_setup;
/*
 * Compensación del primer uso (ADR-009, enmienda del 2026-10-19). La referencia
 * de caldera del perfil se guarda sin el desplazamiento para poder volver a
 * ella al terminar la preparación que lo consume. El máximo se lee una vez al
 * arrancar: no cambia sin recompilar.
 */
static first_use_offset_t s_first_use;
static float s_profile_boiler_c;
static float s_first_use_maximum_c;
static first_use_offset_source_t s_first_use_source;
#ifndef PUMP_LADDER
#define PUMP_LADDER 0
#endif
#if PUMP_LADDER
/* Escalera de la bomba (enmienda a ADR-007 § 5 del 2026-10-10). La tarea del
 * puerto decide y deja el escalón pendiente; esta tarea lo arma, porque es la
 * única que escribe `s_identification`. Ambas bajo `s_lock`. */
static pump_ladder_t s_ladder;
static stimulus_sequence_id_t s_ladder_pending;
#endif

static uint64_t monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / UINT64_C(1000);
}

/*
 * Ruta de corte propia, anterior a cualquier controlador.
 *
 * Se ejecuta en todos los ciclos, incluso cuando el veredicto permite: así el
 * reposo no depende de que alguien recuerde aplicarlo, y mientras no exista
 * ninguna ley de regulación la propiedad se sostiene por construcción.
 */
static void drive_safe_outputs(const actuation_decision_t *decision)
{
    if (!actuation_verdict_allows(decision->pump)) {
        (void)gpio_set_level(PIN_PUMP_CMD, 0);
    }
    if (!actuation_verdict_allows(decision->heater)) {
        (void)gpio_set_level(PIN_HEATER_CMD, 0);
    }
}

/*
 * Antigüedad de la medida más reciente. Si la adquisición se detuviera, este
 * número crece sin límite y el guardián inhibe: es la vigilancia de plazo que
 * exige ADR-007, independiente del watchdog del sistema.
 */
static uint32_t acquisition_age_ms(void)
{
    measurement_t temperature = {0};
    measurement_t pressure = {0};
    if (!acquisition_runtime_get_temperature(&temperature) ||
        !acquisition_runtime_get_pressure(&pressure)) {
        return UINT32_MAX;
    }
    const uint64_t newest = temperature.acquired_ms > pressure.acquired_ms
                                ? temperature.acquired_ms
                                : pressure.acquired_ms;
    if (newest == 0U) {
        return 0U;  /* la tarea aún no ha cerrado su primer ciclo */
    }
    const uint64_t now = monotonic_ms();
    const uint64_t age = now > newest ? now - newest : 0U;
    return age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
}

/*
 * Traduce lo observado a la forma que entiende el supervisor.
 *
 * Cuando no hay ninguna fuente de proceso, la observación se declara no apta en
 * lugar de omitirse: quedarse callado dejaría a la máquina operando sin
 * vigilancia, que es peor que detenerla.
 */
static process_observation_t observe(process_source_t *source)
{
    process_snapshot_t process = {0};
    process_observation_t observation = {0};
    if (!process_variables_get(&process)) {
        *source = PROCESS_SOURCE_NONE;
        return observation;
    }
    *source = process.source;
    observation.temperature_c = process.temperature_c;
    observation.pressure_bar = process.pressure_bar;
    observation.temperature_usable = signal_quality_is_usable(process.temperature_quality);
    observation.pressure_usable = signal_quality_is_usable(process.pressure_quality);
    /* Solo con la planta real: la simulada declara su propio cruce por cero, y
     * en el banco sin red el detector nunca lo ve. */
    observation.mains_absent =
        process.source == PROCESS_SOURCE_MEASURED && !zero_cross_runtime_usable();

#if CONFIG_CAFE_SIMULATION
    /* El cruce por cero solo se conoce mientras la planta sea simulada; con
     * sensores reales nadie lo lee todavía y no puede afirmarse ni negarse. */
    simulation_snapshot_t plant = {0};
    if (process.source == PROCESS_SOURCE_SIMULATED &&
        simulation_runtime_get_snapshot(&plant)) {
        observation.zero_cross_observed = true;
        observation.zero_cross_available = plant.zero_cross_available;
    }
#endif
    return observation;
}

/* Lo que ya se aplicó del perfil efectivo: con lo mismo no se vuelve a aplicar. */
typedef struct {
    uint32_t version;
    char id[EXTRACTION_PROFILE_ID_LENGTH];
    float boiler_offset_c;
    float first_use_feedforward_gain;
} applied_profile_t;

static bool profile_already_seen(const applied_profile_t *applied,
                                 const extraction_profile_t *profile)
{
    return profile->version == applied->version && strcmp(profile->id, applied->id) == 0 &&
           profile->boiler_offset_c == applied->boiler_offset_c &&
           profile->first_use_feedforward_gain == applied->first_use_feedforward_gain;
}

static void mark_profile_seen(applied_profile_t *applied, const extraction_profile_t *profile)
{
    applied->version = profile->version;
    snprintf(applied->id, sizeof(applied->id), "%s", profile->id);
    applied->boiler_offset_c = profile->boiler_offset_c;
    applied->first_use_feedforward_gain = profile->first_use_feedforward_gain;
}

/* El perfil aporta la referencia, el tiempo máximo y, si lo declara, el corte
 * del purgado y el desplazamiento del primer uso; el resto de los límites
 * pertenece a la máquina y ningún perfil puede ampliarlos.
 *
 * El desplazamiento forma parte de lo que identifica al perfil aplicado: la
 * misma versión con otra petición es una adopción nueva, y la misma petición
 * repetida no rearma la que ya se consumió (ver `sync_runtime`).
 *
 * Devuelve true si la referencia acaba de subir por un desplazamiento: quien
 * llama decide entonces si «lista» se retira. */
static bool refresh_limits(applied_profile_t *applied)
{
#if CONFIG_CAFE_IDENTIFICATION_PUMP_NO_HEAT
    /* En el diagnóstico sin calentar, un perfil no puede devolver la
     * referencia de café a la de trabajo. */
    (void)applied;
    return false;
#endif
    extraction_profile_t profile = {0};
    if (!profile_runtime_get_effective(&profile)) {
        return false;
    }
    if (profile_already_seen(applied, &profile)) {
        return false;
    }
    /* El perfil pide el agua sobre el café; el lazo regula la caldera. La
     * conversión sale del acta (ADR-009, enmienda del 2026-10-18), y una
     * petición fuera de lo medido no se aplica: se conservan los límites
     * vigentes, igual que con cualquier otro valor del perfil fuera de rango. */
    const water_reference_t water = water_reference_commissioned();
    float boiler_setpoint_c = 0.0F;
    if (!water_reference_boiler_setpoint(&water, profile.temperature_setpoint_c,
                                         &boiler_setpoint_c)) {
        ESP_LOGE(TAG,
                 "Perfil %s v%" PRIu32 ": agua %.1f C fuera de lo medido "
                 "(caldera %.1f-%.1f C, agua %.1f-%.1f C); se conservan los vigentes",
                 profile.id, profile.version, (double)profile.temperature_setpoint_c,
                 (double)water.boiler_minimum_c, (double)water.boiler_maximum_c,
                 (double)water_reference_water_for(&water, water.boiler_minimum_c),
                 (double)water_reference_water_for(&water, water.boiler_maximum_c));
        /* Se anota como visto para no repetir el error en cada ciclo; los
         * límites no cambian hasta que llegue otro perfil. */
        mark_profile_seen(applied, &profile);
        return false;
    }
    /* Todo o nada sobre una copia: un perfil con la referencia válida y el
     * purgado o el desplazamiento fuera de rango no puede dejar aplicada solo
     * la mitad. Sin purgado declarado vuelve el compilado, para que retirar el
     * campo de un perfil no deje vigente el corte del anterior. */
    process_limits_t candidate = s_limits;
    const uint32_t purge_ms = profile.purge_maximum_defined
                                  ? profile.purge_maximum_ms
                                  : (uint32_t)CONFIG_CAFE_PURGE_MAXIMUM_MS;
    /* El desplazamiento se suma después de convertir el agua a caldera, y se
     * juzga con el límite duro vigente: el ensayo de corte puede haberlo bajado
     * desde que `profile_runtime` lo admitió. */
    first_use_offset_t first_use = s_first_use;
    const first_use_offset_bounds_t bounds = {
        .maximum_c = s_first_use_maximum_c,
        .hard_limit_c = candidate.hard_overtemperature_c,
        .margin_c = candidate.ready_tolerance_c,
        .feedforward_base_gain = heater_feedforward_configured_gain(NULL),
        .feedforward_maximum_gain = first_use_feedforward_maximum_gain(),
    };
    first_use_feedforward_check_t gain_check = FIRST_USE_FEEDFORWARD_UNCHECKED;
    const first_use_offset_check_t check = first_use_offset_adopt_request(
        &first_use, profile.boiler_offset_c, profile.first_use_feedforward_gain, &bounds,
        boiler_setpoint_c, &gain_check);
    if (!first_use_feedforward_check_allows(gain_check)) {
        ESP_LOGE(TAG,
                 "Perfil %s v%" PRIu32 ": refuerzo de la prealimentación %.3f rechazado (%s; "
                 "acta %.3f, máximo %.3f); se conservan los vigentes",
                 profile.id, profile.version, (double)profile.first_use_feedforward_gain,
                 first_use_feedforward_check_name(gain_check),
                 (double)bounds.feedforward_base_gain, (double)bounds.feedforward_maximum_gain);
        mark_profile_seen(applied, &profile);
        return false;
    }
    if (!first_use_offset_check_allows(check)) {
        ESP_LOGE(TAG,
                 "Perfil %s v%" PRIu32 ": desplazamiento de caldera %.2f C rechazado (%s; "
                 "máximo %.2f C, %s); se conservan los vigentes",
                 profile.id, profile.version, (double)profile.boiler_offset_c,
                 first_use_offset_check_name(check), (double)s_first_use_maximum_c,
                 first_use_offset_source_name(s_first_use_source));
        mark_profile_seen(applied, &profile);
        return false;
    }
    const float offset_c = first_use_offset_applied_c(&first_use);
    const float previous_setpoint_c = s_limits.coffee_setpoint_c;
    if (process_limits_apply_profile(&candidate, boiler_setpoint_c + offset_c,
                                     profile.maximum_time_ms) &&
        process_limits_set_purge(&candidate, purge_ms)) {
        s_limits = candidate;
        s_first_use = first_use;
        s_profile_boiler_c = boiler_setpoint_c;
        mark_profile_seen(applied, &profile);
        ESP_LOGI(TAG, "Límites de proceso tomados del perfil=%s v%" PRIu32
                 "; %s %.1f C -> caldera %.2f C; primer uso +%.2f C (%s) -> referencia "
                 "%.2f C; refuerzo %.3f (%s); purgado manual: corte a %" PRIu32 " ms (%s)",
                 profile.id, profile.version, water.defined ? "agua" : "caldera",
                 (double)profile.temperature_setpoint_c, (double)boiler_setpoint_c,
                 (double)offset_c,
                 offset_c > 0.0F ? "vuelve a 0 al terminar la próxima preparación"
                                 : "sin compensación",
                 (double)s_limits.coffee_setpoint_c,
                 (double)first_use_offset_feedforward_gain(&first_use),
                 first_use_offset_feedforward_gain(&first_use) > 0.0F
                     ? "solo la próxima preparación"
                     : "la del acta",
                 purge_ms,
                 profile.purge_maximum_defined ? "perfil" : "compilado");
        return offset_c > 0.0F && s_limits.coffee_setpoint_c > previous_setpoint_c;
    }
    ESP_LOGE(TAG,
             "Perfil %s v%" PRIu32 " fuera de los límites de la máquina; "
             "se conservan los vigentes",
             profile.id, profile.version);
    return false;
}

/*
 * La preparación que consumió el desplazamiento terminó: la referencia vuelve a
 * la del perfil. Bajarla no puede dejar los límites inválidos, pero se
 * comprueba igual sobre una copia; si fallara, se dice y se conservan.
 */
static void release_first_use_offset(void)
{
    process_limits_t candidate = s_limits;
    if (process_limits_apply_profile(&candidate, s_profile_boiler_c,
                                     candidate.maximum_extraction_ms)) {
        const float previous_c = s_limits.coffee_setpoint_c;
        s_limits = candidate;
        ESP_LOGI(TAG, "Primer uso: la preparación terminó; desplazamiento a 0, referencia "
                 "de caldera %.2f C -> %.2f C (la del perfil)",
                 (double)previous_c, (double)s_limits.coffee_setpoint_c);
    } else {
        ESP_LOGE(TAG, "Primer uso: no se pudo volver a la referencia del perfil (%.2f C); "
                 "se conserva %.2f C", (double)s_profile_boiler_c,
                 (double)s_limits.coffee_setpoint_c);
    }
}

/*
 * Pulsadores del mando para la confirmación del modo restringido.
 *
 * Antes de que el mando arranque no hay lectura, y sin lectura no hay
 * confirmación: se toman como sueltos.
 */
static void observe_confirmation(operational_state_t state)
{
    panel_status_t panel = {0};
    const bool have_panel = panel_runtime_get_status(&panel);
    if (identification_mode_observe_panel(&s_identification, state,
                                          have_panel && panel.pressed.brew,
                                          have_panel && panel.pressed.tare,
                                          SUPERVISOR_PERIOD_MS)) {
        ESP_LOGW(TAG, "Modo restringido CONFIRMADO en el mando; secuencia=%s; tope=%.0f%%",
                 stimulus_sequence_name(s_identification.sequence->id),
                 (double)s_identification.maximum_percent);
    }
}

static control_log_loop_t identification_loop(void)
{
    return s_identification.sequence != NULL &&
                   s_identification.sequence->target == IDENTIFICATION_TARGET_HEATER
               ? CONTROL_LOG_LOOP_TEMPERATURE
               : CONTROL_LOG_LOOP_PRESSURE;
}

/*
 * Avanza la secuencia con el veredicto de su salida y deja constancia.
 *
 * Los sucesos van al registro del lazo, que es la evidencia del ensayo, y a la
 * consola, que es lo que mira el operador.
 */
static void advance_identification(const actuation_decision_t *decision)
{
    if (s_identification.sequence == NULL) {
        return;
    }
    const actuation_verdict_t verdict =
        s_identification.sequence->target == IDENTIFICATION_TARGET_HEATER ? decision->heater
                                                                          : decision->pump;
    const identification_event_t event =
        identification_mode_update(&s_identification, verdict, SUPERVISOR_PERIOD_MS);
    const stimulus_sequence_id_t id = s_identification.sequence->id;
    switch (event) {
    case IDENTIFICATION_EVENT_STARTED:
        control_log_runtime_event(CONTROL_LOG_EVENT_STIMULUS_STARTED, identification_loop(),
                                  (uint32_t)id);
        ESP_LOGW(TAG, "Secuencia de estímulo iniciada: %s (%" PRIu32 "ms)",
                 stimulus_sequence_name(id),
                 stimulus_sequence_duration_ms(s_identification.sequence));
        break;
    case IDENTIFICATION_EVENT_FINISHED:
        control_log_runtime_event(CONTROL_LOG_EVENT_STIMULUS_FINISHED, identification_loop(),
                                  (uint32_t)id);
        ESP_LOGW(TAG, "Secuencia de estímulo terminada: %s; la salida vuelve a "
                      "inhibirse por falta de puesta en servicio",
                 stimulus_sequence_name(id));
        break;
    case IDENTIFICATION_EVENT_ABORTED:
        control_log_runtime_event(CONTROL_LOG_EVENT_STIMULUS_ABORTED, identification_loop(),
                                  (uint32_t)s_identification.stop_verdict);
        ESP_LOGE(TAG, "Secuencia de estímulo INTERRUMPIDA a los %" PRIu32 "ms: %s; "
                      "no se reanuda en esta sesión",
                 s_identification.running_ms,
                 actuation_verdict_name(s_identification.stop_verdict));
        break;
    case IDENTIFICATION_EVENT_NONE:
    default:
        break;
    }
}

#if PUMP_LADDER
static void apply_pending_step(operational_state_t state)
{
    /* El escalón espera, sin perderse, a que la extracción o la purga en curso
     * se cierren: armarlo con ellas abiertas lo arrancaría sin pulsar café. */
    if (!pump_ladder_may_arm(state)) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    const stimulus_sequence_id_t pending = s_ladder_pending;
    s_ladder_pending = STIMULUS_SEQUENCE_NONE;
    portEXIT_CRITICAL(&s_lock);
    if (pending == STIMULUS_SEQUENCE_NONE) {
        return;
    }
    const stimulus_sequence_t *sequence = stimulus_sequence_find(pending);
    if (identification_mode_rearm(&s_identification, sequence)) {
        ESP_LOGW(TAG, "Escalera de la bomba: armado %s; pulsar café para recorrerlo",
                 stimulus_sequence_name(pending));
        portENTER_CRITICAL(&s_lock);
        s_status.identification.sequence = pending;
        portEXIT_CRITICAL(&s_lock);
    } else {
        ESP_LOGE(TAG, "Escalera de la bomba: %s no se pudo armar; la sesión sigue en %s",
                 stimulus_sequence_name(pending),
                 identification_phase_name(s_identification.phase));
    }
}
#endif

static void supervisor_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    applied_profile_t applied = {0};

    ESP_LOGI(TAG, "Supervisión del proceso iniciada; núcleo=%d",
             (int)xPortGetCoreID());
    (void)esp_task_wdt_add(NULL);
    while (true) {
        const bool setpoint_raised = refresh_limits(&applied);

        process_source_t source = PROCESS_SOURCE_NONE;
        const process_observation_t observation = observe(&source);
        const operational_state_t state = operational_runtime_state();
        /* Antes de evaluar: la preparación que lo consumía acaba de terminar
         * y «lista» se juzga ya con la referencia del perfil. */
        if (first_use_offset_observe(&s_first_use, state)) {
            release_first_use_offset();
        }
        uint32_t events = process_supervisor_evaluate(
            &s_supervisor, &s_limits, &observation, state, SUPERVISOR_PERIOD_MS);
        /* «Lista» se declara contra la referencia subida: si la caldera aún no
         * llega, la máquina vuelve a calentar y el barista no puede pulsar
         * antes. Solo si el supervisor no tenía ya un evento, que sería de
         * mayor prioridad (una protección, por ejemplo). */
        if (events == 0U && setpoint_raised &&
            process_supervisor_ready_withdrawn(&s_limits, &observation, state)) {
            events = OP_EVENT_MASK(OP_EVENT_COFFEE_TEMPERATURE_LOST);
            ESP_LOGI(TAG, "Primer uso: referencia subida a %.2f C con la caldera en %.2f C; "
                     "deja de estar lista hasta llegar",
                     (double)s_limits.coffee_setpoint_c, (double)observation.temperature_c);
        }

        bool dropped = false;
        if (events != 0U) {
            dropped = !operational_runtime_post_events(events, 0U);
            if (dropped) {
                ESP_LOGE(TAG, "La cola no admitió el evento=0x%08" PRIx32, events);
            }
        }

        portENTER_CRITICAL(&s_lock);
        const bool clear_requested = s_clear_requested;
        s_clear_requested = false;
        const bool updating = s_updating;
        portEXIT_CRITICAL(&s_lock);
        if (clear_requested && state == OP_STATE_ALARM) {
            if (process_supervisor_clear_allowed(&s_limits, &observation)) {
                if (operational_runtime_post_events(OP_EVENT_MASK(OP_EVENT_SAFETY_CLEARED), 0U)) {
                    ESP_LOGW(TAG, "Alarma despejada desde el mando: la causa ya no está; "
                                  "la máquina vuelve a reposo");
                } else {
                    ESP_LOGE(TAG, "La cola no admitió el despeje de la alarma");
                }
            } else {
                ESP_LOGW(TAG,
                         "Alarma no despejada: la causa sigue (temperatura %s, %.2f °C; "
                         "presión %s, %.2f bar)",
                         observation.temperature_usable ? "apta" : "no apta",
                         (double)observation.temperature_c,
                         observation.pressure_usable ? "apta" : "no apta",
                         (double)observation.pressure_bar);
            }
        }

        observe_confirmation(state);
#if PUMP_LADDER
        apply_pending_step(operational_runtime_state());
#endif

        /* El guardián decide con la autorización del estado ya alcanzado, no
         * con el anterior: un evento recién publicado puede haber retirado el
         * permiso, y aplicarlo un ciclo tarde sería aplicarlo tarde. */
        const actuation_request_t request = {
            /* La tarea no existe durante el arranque, de modo que esa parte se
             * cumple por construcción y no por evaluación. La actualización sí
             * ocurre con el firmware ya corriendo, y es la que llega aquí. */
            .starting_up = updating,
            .pressure_commissioned = s_pressure_commissioned,
            .temperature_commissioned = s_temperature_commissioned,
            .authorization = operational_runtime_authorization(),
            .temperature_c = observation.temperature_c,
            .pressure_bar = observation.pressure_bar,
            .temperature_usable = observation.temperature_usable,
            .pressure_usable = observation.pressure_usable,
            .hard_overtemperature_c = s_limits.hard_overtemperature_c,
            .hard_overpressure_bar = s_limits.hard_overpressure_bar,
            .zero_cross_observed = observation.zero_cross_observed,
            .zero_cross_available = observation.zero_cross_available,
            .acquisition_age_ms = acquisition_age_ms(),
            .maximum_acquisition_age_ms = SUPERVISOR_MAXIMUM_ACQUISITION_AGE_MS,
            .identification = identification_mode_request(&s_identification),
        };
        const actuation_decision_t decision =
            actuation_guard_evaluate(&s_guard, &request);
        drive_safe_outputs(&decision);
        /* La demanda que se publica es la que el guardián acaba de juzgar, y se
         * toma antes de avanzar la secuencia: si el escalón cambia ahora, el
         * nuevo se juzga en el ciclo siguiente antes de aplicarse. */
        const bool stimulus_admitted =
            decision.pump == ACTUATION_ALLOWED_IDENTIFICATION ||
            decision.heater == ACTUATION_ALLOWED_IDENTIFICATION;
        const float admitted_demand =
            stimulus_admitted ? request.identification.demand_percent : 0.0F;
        advance_identification(&decision);
        actuation_guard_update_service(
            &s_guard, actuation_verdict_allows(decision.pump), SUPERVISOR_PERIOD_MS);

        portENTER_CRITICAL(&s_lock);
        s_status.source = source;
        s_status.observing = observation.temperature_usable;
        s_status.state = state;
        s_status.limits = s_limits;
        s_status.first_use_offset_c = first_use_offset_applied_c(&s_first_use);
        s_status.first_use_feedforward_gain = first_use_offset_feedforward_gain(&s_first_use);
        s_status.actuation = decision;
        s_status.pump_service = s_guard.pump_service;
        s_status.identification.phase = s_identification.phase;
        s_status.identification.demand_percent = admitted_demand;
        s_status.identification.running_ms = s_identification.running_ms;
        s_status.identification.stop_verdict = s_identification.stop_verdict;
        if (!actuation_verdict_allows(decision.pump) ||
            !actuation_verdict_allows(decision.heater)) {
            s_status.inhibition_cycles++;
        }
        if (events != 0U) {
            if (dropped) {
                s_status.dropped_events++;
            } else {
                s_status.published_events++;
            }
        }
        portEXIT_CRITICAL(&s_lock);

        /* La vigilancia solo se alimenta si el ciclo llegó hasta aquí: si la
         * tarea se detuviera a la mitad, el watchdog lo notaría. */
        (void)esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SUPERVISOR_PERIOD_MS));
    }
}

esp_err_t supervisor_runtime_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_limits = process_limits_default();
    if (!process_limits_are_valid(&s_limits)) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Un valor fuera de rango no deja un purgado sin corte: el supervisor no
     * arranca, igual que con cualquier otro límite inválido. */
    if (!process_limits_set_purge(&s_limits, (uint32_t)CONFIG_CAFE_PURGE_MAXIMUM_MS)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Purgado manual: corte a %lu ms",
             (unsigned long)s_limits.maximum_purge_ms);
#if CONFIG_CAFE_IDENTIFICATION_TRIP_TEST
    /* Ensayo de corte (5.10): el límite solo baja; si no quedara válido, no se
     * arranca, igual que con cualquier otro límite inválido. */
    if (!process_limits_lower_for_trip_test(
            &s_limits, (float)CONFIG_CAFE_IDENTIFICATION_TRIP_TEST_LIMIT_C)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "Ensayo de corte: límite duro bajado a %.1fC y referencia de café a %.1fC",
             (double)s_limits.hard_overtemperature_c, (double)s_limits.coffee_setpoint_c);
#endif
#if CONFIG_CAFE_IDENTIFICATION_PUMP_NO_HEAT
    if (!process_limits_for_pump_diagnostic(&s_limits)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGW(TAG, "Diagnóstico de bomba SIN CALENTAR: referencia de café en %.1fC; "
                  "límite duro %.1fC y demás protecciones sin cambios",
             (double)s_limits.coffee_setpoint_c, (double)s_limits.hard_overtemperature_c);
#endif
    first_use_offset_init(&s_first_use);
    s_profile_boiler_c = s_limits.coffee_setpoint_c;
    s_first_use_maximum_c = first_use_offset_maximum_c(&s_first_use_source);
    if (s_first_use_source == FIRST_USE_OFFSET_SOURCE_PROVISIONAL) {
        ESP_LOGW(TAG, "Primer uso: máximo PROVISIONAL de %.1f C (ensayo supervisado); "
                 "manda sobre el acta", (double)s_first_use_maximum_c);
    } else if (s_first_use_source == FIRST_USE_OFFSET_SOURCE_ACTA) {
        ESP_LOGI(TAG, "Primer uso: máximo de %.2f C (%s)", (double)s_first_use_maximum_c,
                 first_use_offset_source_name(s_first_use_source));
    } else {
        ESP_LOGI(TAG, "Primer uso: sin máximo en el acta; se rechaza cualquier desplazamiento");
    }
    process_supervisor_init(&s_supervisor);
    /* El estado queda publicado antes de crear la tarea: quien lo consulte en el
     * arranque debe ver los límites vigentes y no ceros que parecerían un fallo. */
    actuation_guard_init(&s_guard);
    s_pressure_commissioned = control_tuning_pressure().commissioned;
    s_temperature_commissioned = control_tuning_temperature().commissioned;

    /* El modo restringido se prepara con la marca del lazo que gobernaría: un
     * lazo ya puesto en servicio no se ensaya a lazo abierto. */
    const stimulus_sequence_t *sequence = stimulus_sequence_find(IDENTIFICATION_SEQUENCE);
    const bool target_commissioned =
        sequence != NULL && sequence->target == IDENTIFICATION_TARGET_HEATER
            ? s_temperature_commissioned
            : s_pressure_commissioned;
    s_identification_setup = identification_mode_configure(
        &s_identification, sequence, IDENTIFICATION_MAXIMUM_PERCENT, target_commissioned);
#if PUMP_LADDER
    pump_ladder_init_variant(&s_ladder, PUMP_LADDER_VARIANT);
    s_ladder_pending = STIMULUS_SEQUENCE_NONE;
#endif
    s_status.identification = (identification_status_t){
        .setup = s_identification_setup,
        .phase = s_identification.phase,
        .sequence = sequence != NULL ? sequence->id : STIMULUS_SEQUENCE_NONE,
        .target = sequence != NULL ? sequence->target : IDENTIFICATION_TARGET_NONE,
        .maximum_percent = IDENTIFICATION_MAXIMUM_PERCENT,
        .stop_verdict = ACTUATION_UNDECIDED,
    };

    s_status.limits = s_limits;
    s_status.state = OP_STATE_SAFE_INACTIVE;
    /* Antes de que la tarea corra, ninguna salida está autorizada. */
    s_status.actuation = (actuation_decision_t){
        .pump = ACTUATION_UNDECIDED,
        .heater = ACTUATION_UNDECIDED,
    };

    const BaseType_t created = xTaskCreatePinnedToCore(
        supervisor_task, "process_sup", SUPERVISOR_TASK_STACK_BYTES, NULL,
        SUPERVISOR_TASK_PRIORITY, &s_task, SUPERVISOR_TASK_CORE);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool supervisor_runtime_pump_allowed(void)
{
    portENTER_CRITICAL(&s_lock);
    const actuation_verdict_t verdict = s_status.actuation.pump;
    portEXIT_CRITICAL(&s_lock);
    return actuation_verdict_allows(verdict);
}

bool supervisor_runtime_heater_allowed(void)
{
    portENTER_CRITICAL(&s_lock);
    const actuation_verdict_t verdict = s_status.actuation.heater;
    portEXIT_CRITICAL(&s_lock);
    return actuation_verdict_allows(verdict);
}

bool supervisor_runtime_get_status(supervisor_status_t *status)
{
    if (status == NULL || s_task == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

pump_ladder_result_t supervisor_runtime_ladder_command(const pump_ladder_command_t *command,
                                                       uint8_t *current_percent)
{
    if (current_percent != NULL) {
        *current_percent = 0U;
    }
#if PUMP_LADDER
    if (command == NULL) {
        return PUMP_LADDER_REJECTED_NOT_CONFIGURED;
    }
    pump_ladder_result_t result = PUMP_LADDER_ACCEPTED;
    portENTER_CRITICAL(&s_lock);
    if (command->kind == PUMP_LADDER_COMMAND_STEP) {
        stimulus_sequence_id_t id = STIMULUS_SEQUENCE_NONE;
        /* Con un escalón todavía sin armar, la fase publicada aún dice
         * «terminada»: no se admite otro encima. */
        result = s_ladder_pending != STIMULUS_SEQUENCE_NONE
                     ? PUMP_LADDER_REJECTED_NOT_FINISHED
                     : pump_ladder_select(&s_ladder, command->percent,
                                          s_status.identification.phase, &id);
        if (result == PUMP_LADDER_ACCEPTED) {
            s_ladder_pending = id;
        }
    } else if (command->kind != PUMP_LADDER_COMMAND_SHOW) {
        result = PUMP_LADDER_REJECTED_NOT_IN_LADDER;
    }
    if (current_percent != NULL) {
        *current_percent = pump_ladder_current_percent(&s_ladder);
    }
    portEXIT_CRITICAL(&s_lock);
    return result;
#else
    (void)command;
    return PUMP_LADDER_REJECTED_NOT_CONFIGURED;
#endif
}

void supervisor_runtime_set_updating(bool updating)
{
    portENTER_CRITICAL(&s_lock);
    s_updating = updating;
    portEXIT_CRITICAL(&s_lock);
}

void supervisor_runtime_request_alarm_clear(void)
{
    portENTER_CRITICAL(&s_lock);
    s_clear_requested = true;
    portEXIT_CRITICAL(&s_lock);
}
