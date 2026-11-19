#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "extraction_profile.h"
#include "extraction_session.h"
#include "operational_fsm.h"
#include "process_supervisor.h"
#include "simulated_plant.h"

#define STEP_MS 100U

static operational_transition_t accept(operational_fsm_t *fsm,
                                       operational_event_t event)
{
    const operational_transition_t transition =
        operational_fsm_dispatch(fsm, event);
    assert(transition.accepted);
    return transition;
}

/* La observación viaja igual que desde la adquisición real: el supervisor no
 * sabe de dónde salen los números. */
static process_observation_t observe(const simulation_model_t *plant)
{
    const simulation_snapshot_t *snapshot = simulation_model_snapshot(plant);
    return (process_observation_t){
        .temperature_c = snapshot->temperature_c,
        .pressure_bar = snapshot->pressure_bar,
        .temperature_usable = snapshot->temperature_valid,
        .pressure_usable = snapshot->pressure_valid,
        .zero_cross_observed = true,
        .zero_cross_available = snapshot->zero_cross_available,
    };
}

static uint32_t supervise(simulation_model_t *plant,
                          process_supervisor_t *supervisor,
                          const process_limits_t *limits,
                          operational_state_t state)
{
    simulation_model_step(plant, state, STEP_MS);
    const process_observation_t observation = observe(plant);
    return process_supervisor_evaluate(supervisor, limits, &observation, state,
                                       STEP_MS);
}

static void reach_ready(simulation_model_t *plant,
                        process_supervisor_t *supervisor,
                        const process_limits_t *limits,
                        operational_fsm_t *fsm)
{
    accept(fsm, OP_EVENT_LOGICAL_POWER_TOGGLE);
    for (unsigned int index = 0; index < 2000U; ++index) {
        const uint32_t events = supervise(plant, supervisor, limits, fsm->state);
        if (events != 0U) {
            const operational_transition_t transition =
                operational_fsm_dispatch_highest_priority(fsm, events);
            assert(transition.event == OP_EVENT_COFFEE_TEMPERATURE_READY);
            assert(transition.accepted);
            return;
        }
    }
    assert(!"ready timeout");
}

static void test_profile_validation(void)
{
    const extraction_profile_limits_t limits = extraction_simulation_limits();
    extraction_profile_t profile = extraction_simulation_default_profile();
    assert(extraction_profile_validate(&profile, &limits) == PROFILE_VALID);

    profile.maximum_time_ms = 0U;
    assert(extraction_profile_validate(&profile, &limits) ==
           PROFILE_TIME_OUT_OF_RANGE);
    profile = extraction_simulation_default_profile();
    profile.temperature_tolerance_c = 0.0F;
    assert(extraction_profile_validate(&profile, &limits) ==
           PROFILE_TEMPERATURE_TOLERANCE_OUT_OF_RANGE);
    profile = extraction_simulation_default_profile();
    profile.pressure_setpoint_bar = 15.0F;
    assert(extraction_profile_validate(&profile, &limits) ==
           PROFILE_PRESSURE_OUT_OF_RANGE);
    profile = extraction_simulation_default_profile();
    profile.id[0] = '\0';
    assert(extraction_profile_validate(&profile, &limits) ==
           PROFILE_INVALID_ID);
}

static extraction_summary_t execute(bool manual_stop,
                                    bool expected_mass_defined)
{
    extraction_profile_t profile = extraction_simulation_default_profile();
    profile.maximum_time_ms = 5000U;
    profile.expected_beverage_mass_defined = expected_mass_defined;
    profile.expected_beverage_mass_g = expected_mass_defined ? 36.0F : 0.0F;

    simulation_model_t plant;
    const simulation_config_t config = simulation_default_config();
    assert(simulation_model_init(&plant, &config));
    assert(simulation_model_apply_extraction_profile(
        &plant, profile.temperature_setpoint_c, profile.pressure_setpoint_bar));

    /* El tiempo máximo es una decisión del supervisor, no del modelo. */
    process_limits_t limits = process_limits_default();
    assert(process_limits_apply_profile(&limits, profile.temperature_setpoint_c,
                                        profile.maximum_time_ms));
    process_supervisor_t supervisor;
    process_supervisor_init(&supervisor);

    operational_fsm_t fsm;
    operational_fsm_init(&fsm);
    reach_ready(&plant, &supervisor, &limits, &fsm);

    const operational_transition_t start =
        accept(&fsm, OP_EVENT_BREW_BUTTON_PRESSED);
    assert(start.extraction_started);
    extraction_session_t session;
    assert(extraction_session_start(&session, "test-session", &profile, 1000U));

    operational_transition_t finish = {0};
    for (uint32_t elapsed = STEP_MS; elapsed <= 6000U; elapsed += STEP_MS) {
        const uint32_t events = supervise(&plant, &supervisor, &limits, fsm.state);
        const simulation_snapshot_t *snapshot = simulation_model_snapshot(&plant);
        const extraction_sample_t sample = {
            .elapsed_ms = elapsed,
            .observed_temperature_c = snapshot->temperature_c,
            .observed_pressure_bar = snapshot->pressure_bar,
            .target_temperature_c = profile.temperature_setpoint_c,
            .target_pressure_bar = profile.pressure_setpoint_bar,
            .simulated_pump_duty = snapshot->simulated_pump_duty,
            .temperature_valid = snapshot->temperature_valid,
            .pressure_valid = snapshot->pressure_valid,
            .simulated_heater_on = snapshot->simulated_heater_on,
        };
        assert(extraction_session_record(&session, &sample));

        if (manual_stop && elapsed == 2000U) {
            finish = accept(&fsm, OP_EVENT_LOCAL_NORMAL_STOP);
            break;
        }
        if (events != 0U) {
            finish = operational_fsm_dispatch_highest_priority(&fsm, events);
            assert(finish.accepted);
            break;
        }
    }

    assert(finish.extraction_finished);
    assert(extraction_session_finish(
        &session, finish.extraction_termination,
        1000U + session.samples[session.summary.sample_count - 1U].elapsed_ms));
    assert(session.summary.complete);
    assert(session.summary.sample_count > 0U);
    assert(session.summary.mean_absolute_temperature_error_c >= 0.0F);
    assert(session.summary.mean_absolute_pressure_error_bar >= 0.0F);
    assert(session.summary.temperature_within_tolerance_percent >= 0.0F);
    assert(session.summary.temperature_within_tolerance_percent <= 100.0F);
    assert(session.summary.pressure_within_tolerance_percent >= 0.0F);
    assert(session.summary.pressure_within_tolerance_percent <= 100.0F);
    return session.summary;
}

static void test_time_and_manual_termination(void)
{
    const extraction_summary_t by_time = execute(false, true);
    assert(by_time.termination ==
           OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED);
    assert(by_time.duration_ms == 5000U);
    assert(by_time.effective_profile.expected_beverage_mass_defined);
    assert(by_time.effective_profile.expected_beverage_mass_g == 36.0F);

    const extraction_summary_t manual = execute(true, false);
    assert(manual.termination == OP_TERMINATION_MANUAL_NORMAL_STOP);
    assert(manual.duration_ms == 2000U);
}

/*
 * El límite de prueba de la 5.10 solo baja, mantiene válidos los límites y
 * dispara la protección a su valor; por debajo, la caldera sigue calentando.
 */
static void test_the_trip_test_limit_only_lowers(void)
{
    process_limits_t limits = process_limits_default();
    const float bench_limit = limits.hard_overtemperature_c;

    process_limits_t raised = limits;
    assert(process_limits_lower_for_trip_test(&raised, bench_limit + 50.0F));
    assert(raised.hard_overtemperature_c == bench_limit);

    assert(process_limits_lower_for_trip_test(&limits, 60.0F));
    assert(limits.hard_overtemperature_c == 60.0F);
    assert(limits.coffee_setpoint_c == 60.0F - PROCESS_TRIP_TEST_SETPOINT_MARGIN_C);
    assert(process_limits_are_valid(&limits));

    /* Un perfil de café a 93 °C ya no cabe: no puede subir lo que la prueba bajó. */
    assert(!process_limits_apply_profile(&limits, 93.0F, 30000U));
    assert(limits.hard_overtemperature_c == 60.0F);

    process_supervisor_t supervisor;
    process_supervisor_init(&supervisor);
    process_observation_t below = {.temperature_c = 59.0F, .temperature_usable = true};
    process_observation_t above = {.temperature_c = 60.5F, .temperature_usable = true};
    assert((process_supervisor_evaluate(&supervisor, &limits, &below, OP_STATE_READY, STEP_MS) &
            OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP)) == 0U);
    assert(process_supervisor_evaluate(&supervisor, &limits, &above, OP_STATE_READY, STEP_MS) ==
           OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP));

    /* Un límite absurdo se rechaza sin tocar nada. */
    process_limits_t untouched = process_limits_default();
    assert(!process_limits_lower_for_trip_test(&untouched, 0.0F));
    assert(untouched.hard_overtemperature_c == bench_limit);
}

/*
 * El diagnóstico de bomba sin calentar solo baja la referencia de café: la
 * máquina se declara preparada a cualquier temperatura de la caldera, y el
 * límite duro, la presión y los tiempos quedan como estaban.
 */
static void test_the_pump_diagnostic_only_lowers_the_coffee_setpoint(void)
{
    const process_limits_t before = process_limits_default();
    process_limits_t limits = before;
    assert(process_limits_for_pump_diagnostic(&limits));
    assert(limits.coffee_setpoint_c == PROCESS_PUMP_DIAGNOSTIC_SETPOINT_C);
    assert(limits.hard_overtemperature_c == before.hard_overtemperature_c);
    assert(limits.hard_overpressure_bar == before.hard_overpressure_bar);
    assert(limits.maximum_extraction_ms == before.maximum_extraction_ms);
    assert(process_limits_are_valid(&limits));
    assert(!process_limits_for_pump_diagnostic(NULL));
}

/*
 * El corte del purgado se fija dentro de su rango y el supervisor lo aplica a
 * ese valor; fuera del rango no se toca nada, y el resto de límites no cambia.
 */
static void test_the_purge_cut_is_set_within_its_range(void)
{
    const process_limits_t before = process_limits_default();
    process_limits_t limits = before;
    assert(process_limits_set_purge(&limits, 4000U));
    assert(limits.maximum_purge_ms == 4000U);
    assert(limits.maximum_extraction_ms == before.maximum_extraction_ms);
    assert(limits.hard_overtemperature_c == before.hard_overtemperature_c);

    process_supervisor_t supervisor;
    process_supervisor_init(&supervisor);
    const process_observation_t steady = {
        .temperature_c = before.coffee_setpoint_c,
        .temperature_usable = true,
        .pressure_usable = true,
        .zero_cross_observed = true,
        .zero_cross_available = true,
    };
    uint32_t elapsed = 0U;
    uint32_t events = 0U;
    while ((events & OP_EVENT_MASK(OP_EVENT_PURGE_TIME_LIMIT)) == 0U && elapsed < 20000U) {
        events = process_supervisor_evaluate(&supervisor, &limits, &steady,
                                             OP_STATE_MANUAL_PURGE, STEP_MS);
        elapsed += STEP_MS;
    }
    assert(elapsed == 4000U);

    process_limits_t untouched = before;
    assert(!process_limits_set_purge(&untouched, PROCESS_PURGE_MINIMUM_MS - 1U));
    assert(!process_limits_set_purge(&untouched, PROCESS_PURGE_MAXIMUM_MS + 1U));
    assert(untouched.maximum_purge_ms == before.maximum_purge_ms);
    assert(!process_limits_set_purge(NULL, 4000U));
}


/* Una alarma solo se despeja si la causa ya no está: sin medida de temperatura
 * (el caso del 2026-10-17, arranque sin red), sobre el límite duro o sobre la
 * presión máxima, no; con todo en orden, sí. Una presión no apta no lo impide
 * en reposo, donde la bomba no se usa. */
static void test_an_alarm_clears_only_without_its_cause(void)
{
    const process_limits_t limits = process_limits_default();
    process_observation_t ok = {.temperature_c = 27.0F, .temperature_usable = true,
                                .pressure_bar = 0.0F, .pressure_usable = true};
    assert(process_supervisor_clear_allowed(&limits, &ok));

    process_observation_t no_pressure = ok;
    no_pressure.pressure_usable = false;
    assert(process_supervisor_clear_allowed(&limits, &no_pressure));

    const process_observation_t nothing = {0};
    assert(!process_supervisor_clear_allowed(&limits, &nothing));

    process_observation_t hot = ok;
    hot.temperature_c = limits.hard_overtemperature_c;
    assert(!process_supervisor_clear_allowed(&limits, &hot));

    process_observation_t pressed = ok;
    pressed.pressure_bar = limits.hard_overpressure_bar;
    assert(!process_supervisor_clear_allowed(&limits, &pressed));

    assert(!process_supervisor_clear_allowed(&limits, NULL));
    const process_limits_t invalid = {0};
    assert(!process_supervisor_clear_allowed(&invalid, &ok));
}


/* La red que falta un segundo seguido devuelve la máquina a reposo, sin
 * alarma; un instante sin red no. Sin red, la presión no apta no dispara en
 * el café (la bomba no puede moverse); con red, sí. */
static void test_mains_loss_rests_without_alarm(void)
{
    process_supervisor_t supervisor;
    process_supervisor_init(&supervisor);
    const process_limits_t limits = process_limits_default();
    process_observation_t dark = {.temperature_c = 92.0F, .temperature_usable = true,
                                  .pressure_usable = false, .mains_absent = true};
    uint32_t events = 0U;
    uint32_t elapsed = 0U;
    while (events == 0U && elapsed < 5000U) {
        events = process_supervisor_evaluate(&supervisor, &limits, &dark,
                                             OP_STATE_EXTRACTION, 50U);
        elapsed += 50U;
    }
    assert(events == OP_EVENT_MASK(OP_EVENT_MAINS_LOST));
    assert(elapsed == PROCESS_MAINS_LOSS_MS);

    process_supervisor_init(&supervisor);
    for (int i = 0; i < 10; ++i) {
        assert(process_supervisor_evaluate(&supervisor, &limits, &dark, OP_STATE_READY, 50U) == 0U);
    }
    process_observation_t lit = dark;
    lit.mains_absent = false;
    lit.pressure_usable = true;
    assert(process_supervisor_evaluate(&supervisor, &limits, &lit, OP_STATE_READY, 50U) == 0U);
    for (int i = 0; i < 10; ++i) {
        assert(process_supervisor_evaluate(&supervisor, &limits, &dark, OP_STATE_READY, 50U) == 0U);
    }

    process_observation_t blind = lit;
    blind.pressure_usable = false;
    assert(process_supervisor_evaluate(&supervisor, &limits, &blind, OP_STATE_EXTRACTION, 50U) ==
           OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP));
}

int main(void)
{
    test_mains_loss_rests_without_alarm();
    test_an_alarm_clears_only_without_its_cause();
    test_profile_validation();
    test_time_and_manual_termination();
    test_the_trip_test_limit_only_lowers();
    test_the_pump_diagnostic_only_lowers_the_coffee_setpoint();
    test_the_purge_cut_is_set_within_its_range();
    puts("PASS: perfiles, muestras y terminaciones de extracción");
    return 0;
}
