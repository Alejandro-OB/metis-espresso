#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "actuation_guard.h"
#include "identification_mode.h"
#include "process_supervisor.h"
#include "stimulus_sequence.h"

#define PERIOD_MS 50U  /* el del supervisor, que es quien avanza el modo */

static const stimulus_sequence_t *pump_1pct(void)
{
    return stimulus_sequence_find(STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1);
}

static const stimulus_sequence_t *heater_1pct(void)
{
    return stimulus_sequence_find(STIMULUS_SEQUENCE_HEATER_STEP_1PCT_V1);
}

/* Avanza la confirmación `ms` milisegundos con el mismo estado del mando. */
static bool hold(identification_mode_t *mode, operational_state_t state, bool brew,
                 bool tare, uint32_t ms)
{
    bool confirmed = false;
    for (uint32_t elapsed = 0U; elapsed < ms; elapsed += PERIOD_MS) {
        confirmed = identification_mode_observe_panel(mode, state, brew, tare, PERIOD_MS) ||
                    confirmed;
    }
    return confirmed;
}

static void arm(identification_mode_t *mode, const stimulus_sequence_t *sequence)
{
    assert(identification_mode_configure(mode, sequence, 1.0F, false) ==
           IDENTIFICATION_SETUP_ACCEPTED);
    (void)hold(mode, OP_STATE_SAFE_INACTIVE, false, false, PERIOD_MS);
    assert(hold(mode, OP_STATE_SAFE_INACTIVE, true, true,
                IDENTIFICATION_CONFIRMATION_HOLD_MS));
    assert(mode->phase == IDENTIFICATION_ARMED);
}

/* Cada secuencia declarada es válida, se encuentra y tiene un nombre propio. */
static void test_declared_sequences(void)
{
    const char *names[STIMULUS_SEQUENCE_COUNT] = {0};
    for (int id = 1; id < (int)STIMULUS_SEQUENCE_COUNT; id++) {
        const stimulus_sequence_t *sequence =
            stimulus_sequence_find((stimulus_sequence_id_t)id);
        assert(sequence != NULL);
        assert(sequence->id == (stimulus_sequence_id_t)id);
        assert(stimulus_sequence_is_valid(sequence));
        names[id] = stimulus_sequence_name((stimulus_sequence_id_t)id);
        assert(strcmp(names[id], "none") != 0);
        /* La versión va en el nombre: un registro antiguo sigue significando lo
         * mismo aunque se publique otra secuencia. */
        assert(strstr(names[id], "-v") != NULL);
        for (int other = 1; other < id; other++) {
            assert(strcmp(names[id], names[other]) != 0);
        }
        /* Ninguna empieza actuando: el reposo inicial es la línea de base contra
         * la que se juzga si la respuesta es legible. */
        assert(sequence->steps[0].demand_percent == 0U);
        /* Una secuencia agotada no deja nada encendido. */
        assert(stimulus_sequence_demand_at(sequence,
                                           stimulus_sequence_duration_ms(sequence)) == 0U);
    }
    assert(stimulus_sequence_find(STIMULUS_SEQUENCE_NONE) == NULL);
    assert(stimulus_sequence_find(STIMULUS_SEQUENCE_COUNT) == NULL);
}

/*
 * Las secuencias de la bomba caben en una extracción con el tiempo máximo por
 * omisión. El estado de extracción no se relaja en este modo, de modo que una
 * secuencia más larga se cortaría siempre antes de terminar.
 */
static void test_pump_sequences_fit_in_an_extraction(void)
{
    const process_limits_t limits = process_limits_default();
    for (int id = 1; id < (int)STIMULUS_SEQUENCE_COUNT; id++) {
        const stimulus_sequence_t *sequence =
            stimulus_sequence_find((stimulus_sequence_id_t)id);
        if (sequence->target == IDENTIFICATION_TARGET_PUMP) {
            assert(stimulus_sequence_duration_ms(sequence) < limits.maximum_extraction_ms);
        }
    }
}

/*
 * El escalón largo del calentador llega al régimen de la caldera real: cubre al
 * menos cuatro constantes de tiempo de las medidas el 2026-10-08 (τ ≈ 2300 s,
 * puesta en servicio 4.1), y nunca pide más que los escalones cortos que ya se
 * ensayaron.
 */
#define MEASURED_BOILER_TAU_MS UINT32_C(2300000)

static void test_the_long_heater_step_reaches_steady_state(void)
{
    const stimulus_sequence_t *sequence =
        stimulus_sequence_find(STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V2);
    assert(sequence != NULL);
    assert(sequence->target == IDENTIFICATION_TARGET_HEATER);
    const uint32_t hold_ms = stimulus_sequence_duration_ms(sequence) - sequence->steps[0].duration_ms;
    assert(hold_ms >= 4U * MEASURED_BOILER_TAU_MS);
    assert(stimulus_sequence_peak_percent(sequence) ==
           stimulus_sequence_peak_percent(
               stimulus_sequence_find(STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V1)));
}

/*
 * El calentamiento previo al enfriamiento libre no pide un nivel nuevo: es el
 * 5 % ya ensayado, más largo, y termina solo.
 */
static void test_the_cooling_preheat_reuses_a_tried_level(void)
{
    const stimulus_sequence_t *preheat =
        stimulus_sequence_find(STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V2);
    const stimulus_sequence_t *tried =
        stimulus_sequence_find(STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V1);
    assert(preheat != NULL && preheat->target == IDENTIFICATION_TARGET_HEATER);
    assert(stimulus_sequence_peak_percent(preheat) == stimulus_sequence_peak_percent(tried));
    assert(stimulus_sequence_duration_ms(preheat) > stimulus_sequence_duration_ms(tried));
    assert(stimulus_sequence_demand_at(preheat, stimulus_sequence_duration_ms(preheat)) == 0U);
}

/* La demanda sigue la tabla, escalón a escalón. */
static void test_demand_follows_the_table(void)
{
    const stimulus_sequence_t *sequence = pump_1pct();
    assert(stimulus_sequence_demand_at(sequence, 0U) == 0U);
    assert(stimulus_sequence_demand_at(sequence, 1999U) == 0U);
    assert(stimulus_sequence_demand_at(sequence, 2000U) == 1U);
    assert(stimulus_sequence_demand_at(sequence, 7999U) == 1U);
    assert(stimulus_sequence_demand_at(sequence, 8000U) == 0U);
    assert(stimulus_sequence_demand_at(sequence, UINT32_MAX) == 0U);
    assert(stimulus_sequence_peak_percent(sequence) == 1U);
    assert(stimulus_sequence_demand_at(NULL, 2000U) == 0U);
}

/* El cero no pide nada, y el binario de operación tampoco. */
static void test_zero_and_operation_binary_request_nothing(void)
{
    const identification_mode_t empty = {0};
    assert(empty.phase == IDENTIFICATION_DISABLED);
    assert(identification_mode_request(&empty).target == IDENTIFICATION_TARGET_NONE);

    identification_mode_t mode;
    assert(identification_mode_configure(&mode, NULL, 1.0F, false) ==
           IDENTIFICATION_SETUP_NO_SEQUENCE);
    assert(mode.phase == IDENTIFICATION_DISABLED);
    /* Ni pulsando la combinación se arma. */
    (void)hold(&mode, OP_STATE_SAFE_INACTIVE, false, false, PERIOD_MS);
    assert(!hold(&mode, OP_STATE_SAFE_INACTIVE, true, true, 10000U));
    assert(identification_mode_request(&mode).target == IDENTIFICATION_TARGET_NONE);
    assert(identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS) ==
           IDENTIFICATION_EVENT_NONE);
}

static void test_configuration_refusals(void)
{
    identification_mode_t mode;
    /* La secuencia del 5 % no se arma bajo un tope del 1 %. */
    assert(identification_mode_configure(
               &mode, stimulus_sequence_find(STIMULUS_SEQUENCE_PUMP_STEP_5PCT_V1), 1.0F,
               false) == IDENTIFICATION_SETUP_EXCEEDS_LIMIT);
    assert(mode.phase == IDENTIFICATION_DISABLED);
    assert(identification_mode_configure(&mode, pump_1pct(), 0.0F, false) ==
           IDENTIFICATION_SETUP_INVALID_LIMIT);
    assert(identification_mode_configure(&mode, pump_1pct(), 101.0F, false) ==
           IDENTIFICATION_SETUP_INVALID_LIMIT);
    assert(identification_mode_configure(&mode, pump_1pct(), 1.0F, true) ==
           IDENTIFICATION_SETUP_ALREADY_COMMISSIONED);
    assert(mode.phase == IDENTIFICATION_DISABLED);

    const stimulus_sequence_t broken = {
        .id = STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1,
        .target = IDENTIFICATION_TARGET_PUMP,
        .step_count = 1U,
        .steps = {{1U, 0U}},
    };
    assert(identification_mode_configure(&mode, &broken, 1.0F, false) ==
           IDENTIFICATION_SETUP_INVALID_SEQUENCE);
}

/* La confirmación exige soltar primero, mantener los dos y hacerlo a tiempo. */
static void test_confirmation(void)
{
    identification_mode_t mode;

    /* Apretados desde antes del arranque: no confirma nunca. */
    assert(identification_mode_configure(&mode, pump_1pct(), 1.0F, false) ==
           IDENTIFICATION_SETUP_ACCEPTED);
    assert(!hold(&mode, OP_STATE_SAFE_INACTIVE, true, true, 20000U));
    assert(mode.phase == IDENTIFICATION_AWAITING_CONFIRMATION);

    /* Un solo botón no cuenta, y soltar a medias reinicia la cuenta. */
    assert(identification_mode_configure(&mode, pump_1pct(), 1.0F, false) ==
           IDENTIFICATION_SETUP_ACCEPTED);
    (void)hold(&mode, OP_STATE_SAFE_INACTIVE, false, false, PERIOD_MS);
    assert(!hold(&mode, OP_STATE_SAFE_INACTIVE, true, false, 6000U));
    assert(!hold(&mode, OP_STATE_SAFE_INACTIVE, true, true, 4000U));
    assert(!hold(&mode, OP_STATE_SAFE_INACTIVE, false, true, PERIOD_MS));
    assert(!hold(&mode, OP_STATE_SAFE_INACTIVE, true, true, 4000U));
    assert(mode.phase == IDENTIFICATION_AWAITING_CONFIRMATION);
    assert(hold(&mode, OP_STATE_SAFE_INACTIVE, true, true, 1000U));
    assert(mode.phase == IDENTIFICATION_ARMED);

    /* La ventana se cierra sola. */
    assert(identification_mode_configure(&mode, pump_1pct(), 1.0F, false) ==
           IDENTIFICATION_SETUP_ACCEPTED);
    (void)hold(&mode, OP_STATE_SAFE_INACTIVE, false, false,
               IDENTIFICATION_CONFIRMATION_WINDOW_MS);
    assert(mode.phase == IDENTIFICATION_DECLINED);
    assert(!hold(&mode, OP_STATE_SAFE_INACTIVE, true, true, 10000U));
    assert(identification_mode_request(&mode).target == IDENTIFICATION_TARGET_NONE);

    /* Encender antes de confirmar es elegir operar. */
    assert(identification_mode_configure(&mode, pump_1pct(), 1.0F, false) ==
           IDENTIFICATION_SETUP_ACCEPTED);
    (void)hold(&mode, OP_STATE_SAFE_INACTIVE, false, false, PERIOD_MS);
    (void)hold(&mode, OP_STATE_SAFE_INACTIVE, true, true, 2000U);
    (void)hold(&mode, OP_STATE_COFFEE_HEATING, true, true, PERIOD_MS);
    assert(mode.phase == IDENTIFICATION_DECLINED);
}

/* Armado, espera al estado operativo; empieza con el veredicto propio. */
static void test_run_to_completion(void)
{
    identification_mode_t mode;
    arm(&mode, pump_1pct());

    identification_request_t request = identification_mode_request(&mode);
    assert(request.target == IDENTIFICATION_TARGET_PUMP);
    assert(request.demand_percent == 0.0F);
    assert(request.maximum_percent == 1.0F);

    /* Mientras el estado no autoriza, sigue armado. */
    for (int cycle = 0; cycle < 100; cycle++) {
        assert(identification_mode_update(&mode, ACTUATION_INHIBITED_NOT_AUTHORIZED,
                                          PERIOD_MS) == IDENTIFICATION_EVENT_NONE);
    }
    assert(mode.phase == IDENTIFICATION_ARMED);

    assert(identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS) ==
           IDENTIFICATION_EVENT_STARTED);
    assert(mode.phase == IDENTIFICATION_RUNNING);

    const uint32_t duration = stimulus_sequence_duration_ms(pump_1pct());
    identification_event_t event = IDENTIFICATION_EVENT_NONE;
    while (mode.phase == IDENTIFICATION_RUNNING) {
        request = identification_mode_request(&mode);
        assert(request.demand_percent ==
               (float)stimulus_sequence_demand_at(pump_1pct(), mode.running_ms));
        event = identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS);
    }
    assert(event == IDENTIFICATION_EVENT_FINISHED);
    assert(mode.phase == IDENTIFICATION_FINISHED);
    assert(mode.running_ms >= duration && mode.running_ms < duration + PERIOD_MS);

    /* Terminada, no pide nada ni vuelve a empezar. */
    assert(identification_mode_request(&mode).target == IDENTIFICATION_TARGET_NONE);
    assert(identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS) ==
           IDENTIFICATION_EVENT_NONE);
    assert(mode.phase == IDENTIFICATION_FINISHED);
}

/* Cualquier otra respuesta del guardián interrumpe, y no se reanuda. */
static void test_abort_is_final(void)
{
    identification_mode_t mode;
    arm(&mode, heater_1pct());
    assert(identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS) ==
           IDENTIFICATION_EVENT_STARTED);
    assert(identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS) ==
           IDENTIFICATION_EVENT_NONE);
    assert(identification_mode_update(&mode, ACTUATION_INHIBITED_HARD_LIMIT, PERIOD_MS) ==
           IDENTIFICATION_EVENT_ABORTED);
    assert(mode.phase == IDENTIFICATION_ABORTED);
    assert(mode.stop_verdict == ACTUATION_INHIBITED_HARD_LIMIT);
    assert(identification_mode_request(&mode).target == IDENTIFICATION_TARGET_NONE);
    assert(identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS) ==
           IDENTIFICATION_EVENT_NONE);
    assert(mode.phase == IDENTIFICATION_ABORTED);

    /* Incluso el veredicto ordinario interrumpe: la secuencia solo corre bajo
     * el suyo. */
    arm(&mode, heater_1pct());
    (void)identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, PERIOD_MS);
    assert(identification_mode_update(&mode, ACTUATION_ALLOWED, PERIOD_MS) ==
           IDENTIFICATION_EVENT_ABORTED);

    /* Un tope que rechaza el primer escalón no deja el ensayo armado. */
    arm(&mode, heater_1pct());
    assert(identification_mode_update(&mode, ACTUATION_INHIBITED_STIMULUS_LIMIT, PERIOD_MS) ==
           IDENTIFICATION_EVENT_ABORTED);
}

/*
 * La cadena entera: modo y guardián juntos, como en el supervisor.
 *
 * Es la única prueba que responde a si lo que se aplica a la salida es siempre
 * lo que el guardián juzgó, y si una protección que salta a mitad de secuencia
 * deja la salida cortada. La cantidad publicada se toma, como en la envoltura,
 * de la petición juzgada y antes de avanzar.
 */
static void test_with_the_guard(void)
{
    actuation_guard_t guard;
    actuation_guard_init(&guard);
    identification_mode_t mode;
    arm(&mode, pump_1pct());

    actuation_request_t request = {
        .pressure_commissioned = false,
        .temperature_commissioned = false,
        .authorization = {.extraction_pump_allowed = true, .heater_regulation_allowed = true},
        .temperature_c = 93.0F,
        .pressure_bar = 0.0F,
        .temperature_usable = true,
        .pressure_usable = true,
        .hard_overtemperature_c = 150.0F,
        .hard_overpressure_bar = 12.0F,
        .maximum_acquisition_age_ms = 100U,
    };

    uint32_t admitted_cycles = 0U;
    uint32_t cycle = 0U;
    while (mode.phase == IDENTIFICATION_ARMED || mode.phase == IDENTIFICATION_RUNNING) {
        /* A mitad del escalón la presión alcanza el límite duro. */
        request.pressure_bar = cycle == 100U ? 12.0F : 1.0F;
        request.identification = identification_mode_request(&mode);
        const actuation_decision_t decision = actuation_guard_evaluate(&guard, &request);
        assert(decision.heater == ACTUATION_INHIBITED_NOT_COMMISSIONED);
        if (decision.pump == ACTUATION_ALLOWED_IDENTIFICATION) {
            assert(request.identification.demand_percent <=
                   request.identification.maximum_percent);
            admitted_cycles++;
        }
        actuation_guard_update_service(&guard, actuation_verdict_allows(decision.pump),
                                       PERIOD_MS);
        (void)identification_mode_update(&mode, decision.pump, PERIOD_MS);
        cycle++;
    }
    assert(mode.phase == IDENTIFICATION_ABORTED);
    assert(mode.stop_verdict == ACTUATION_INHIBITED_HARD_LIMIT);
    assert(admitted_cycles == 100U);

    /* Terminado el ensayo, la bomba vuelve a la inhibición ordinaria. */
    request.pressure_bar = 1.0F;
    request.identification = identification_mode_request(&mode);
    assert(actuation_guard_evaluate(&guard, &request).pump ==
           ACTUATION_INHIBITED_NOT_COMMISSIONED);
}

int main(void)
{
    test_declared_sequences();
    test_pump_sequences_fit_in_an_extraction();
    test_the_long_heater_step_reaches_steady_state();
    test_the_cooling_preheat_reuses_a_tried_level();
    test_demand_follows_the_table();
    test_zero_and_operation_binary_request_nothing();
    test_configuration_refusals();
    test_confirmation();
    test_run_to_completion();
    test_abort_is_final();
    test_with_the_guard();
    puts("PASS: modo restringido, confirmación y secuencias de estímulo");
    return 0;
}
