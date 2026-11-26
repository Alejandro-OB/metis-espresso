#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "operational_fsm.h"
#include "process_supervisor.h"
#include "simulated_plant.h"

#define STEP_MS 100U
#define MAX_STEPS 2000U

typedef struct {
    simulation_model_t model;
    process_supervisor_t supervisor;
    process_limits_t limits;
    operational_fsm_t fsm;
} bench_t;

static operational_transition_t dispatch(operational_fsm_t *fsm,
                                         operational_event_t event)
{
    const operational_transition_t transition =
        operational_fsm_dispatch(fsm, event);
    assert(transition.accepted);
    return transition;
}

/*
 * Traduce el estado de la planta a una observación. Es la misma forma que
 * entrega la adquisición real: el supervisor no distingue una de otra, y esa es
 * exactamente la propiedad que estas pruebas comprueban.
 */
static process_observation_t observe(const simulation_model_t *model)
{
    const simulation_snapshot_t *snapshot = simulation_model_snapshot(model);
    assert(snapshot != NULL);
    return (process_observation_t){
        .temperature_c = snapshot->temperature_c,
        .pressure_bar = snapshot->pressure_bar,
        .temperature_usable = snapshot->temperature_valid,
        .pressure_usable = snapshot->pressure_valid,
        .zero_cross_observed = true,
        .zero_cross_available = snapshot->zero_cross_available,
    };
}

static uint32_t step(bench_t *bench)
{
    simulation_model_step(&bench->model, bench->fsm.state, STEP_MS);
    const process_observation_t observation = observe(&bench->model);
    return process_supervisor_evaluate(&bench->supervisor, &bench->limits,
                                       &observation, bench->fsm.state, STEP_MS);
}

static operational_transition_t advance_until_event(
    bench_t *bench, operational_event_t expected_event)
{
    for (unsigned int index = 0; index < MAX_STEPS; ++index) {
        const uint32_t events = step(bench);
        if (events != 0U) {
            const operational_transition_t transition =
                operational_fsm_dispatch_highest_priority(&bench->fsm, events);
            assert(transition.accepted);
            assert(transition.event == expected_event);
            return transition;
        }
    }
    assert(!"tiempo agotado esperando el evento supervisado");
    return (operational_transition_t){0};
}

static void initialize(bench_t *bench)
{
    const simulation_config_t config = simulation_default_config();
    assert(simulation_model_init(&bench->model, &config));
    bench->limits = process_limits_default();
    assert(process_limits_are_valid(&bench->limits));
    process_supervisor_init(&bench->supervisor);
    operational_fsm_init(&bench->fsm);
}

static void reach_ready(bench_t *bench)
{
    dispatch(&bench->fsm, OP_EVENT_LOGICAL_POWER_TOGGLE);
    assert(bench->fsm.state == OP_STATE_COFFEE_HEATING);
    advance_until_event(bench, OP_EVENT_COFFEE_TEMPERATURE_READY);
    assert(bench->fsm.state == OP_STATE_READY);
}

static void test_safe_inactive_outputs(void)
{
    bench_t bench;
    initialize(&bench);
    /* En seguro no hay proceso que supervisar, por larga que sea la espera. */
    simulation_model_step(&bench.model, bench.fsm.state, 1000U);
    const process_observation_t observation = observe(&bench.model);
    assert(process_supervisor_evaluate(&bench.supervisor, &bench.limits,
                                       &observation, bench.fsm.state, 1000U) == 0U);
    const simulation_snapshot_t *snapshot = simulation_model_snapshot(&bench.model);
    assert(snapshot->temperature_valid && snapshot->pressure_valid);
    assert(snapshot->zero_cross_available);
    assert(!snapshot->simulated_heater_on);
    assert(!snapshot->simulated_pump_on);
}

static void test_heating_extraction_and_time_limit(void)
{
    bench_t bench;
    initialize(&bench);
    reach_ready(&bench);
    assert(simulation_model_snapshot(&bench.model)->temperature_c > 90.0F);

    dispatch(&bench.fsm, OP_EVENT_BREW_BUTTON_PRESSED);
    assert(bench.fsm.state == OP_STATE_EXTRACTION);
    for (unsigned int index = 0; index < 20U; ++index) {
        assert(step(&bench) == 0U);
    }
    const simulation_snapshot_t *snapshot = simulation_model_snapshot(&bench.model);
    assert(snapshot->simulated_pump_on);
    assert(snapshot->simulated_pump_duty > 0.0F);
    assert(snapshot->pressure_bar > 5.0F);

    const operational_transition_t finish =
        advance_until_event(&bench, OP_EVENT_EXTRACTION_TIME_LIMIT);
    assert(finish.extraction_finished);
    assert(finish.extraction_termination ==
           OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED);
    assert(bench.fsm.state == OP_STATE_READY);
    assert(step(&bench) == 0U);
    assert(!simulation_model_snapshot(&bench.model)->simulated_pump_on);
}

static void test_purge_limit(void)
{
    bench_t bench;
    initialize(&bench);
    reach_ready(&bench);

    dispatch(&bench.fsm, OP_EVENT_PURGE_BUTTON_PRESSED);
    assert(bench.fsm.state == OP_STATE_MANUAL_PURGE);
    advance_until_event(&bench, OP_EVENT_PURGE_TIME_LIMIT);
    assert(bench.fsm.state == OP_STATE_READY);
}

static void expect_fault_trip(uint32_t fault_mask, bool extraction_required)
{
    bench_t bench;
    initialize(&bench);
    reach_ready(&bench);
    if (extraction_required) {
        dispatch(&bench.fsm, OP_EVENT_BREW_BUTTON_PRESSED);
    }
    simulation_model_set_faults(&bench.model, fault_mask);
    const uint32_t events = step(&bench);
    assert((events & OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP)) != 0U);

    const operational_transition_t transition =
        operational_fsm_dispatch_highest_priority(&bench.fsm, events);
    assert(transition.event == OP_EVENT_SAFETY_TRIP);
    assert(bench.fsm.state == OP_STATE_ALARM);

    /*
     * Las demandas caen en el ciclo siguiente, cuando el modelo ya conoce el
     * estado de alarma. La planta no se apaga sola: la lleva a reposo el estado
     * autorizado, que es también como funcionará la máquina real.
     */
    assert(step(&bench) == 0U);
    const simulation_snapshot_t *snapshot = simulation_model_snapshot(&bench.model);
    assert(!snapshot->simulated_heater_on);
    assert(!snapshot->simulated_pump_on);
}

static void test_fault_injection(void)
{
    expect_fault_trip(SIM_FAULT_TEMPERATURE_SENSOR_DISCONNECTED, false);
    expect_fault_trip(SIM_FAULT_FORCED_OVERTEMPERATURE, false);
    expect_fault_trip(SIM_FAULT_PRESSURE_SENSOR_DISCONNECTED, true);
    expect_fault_trip(SIM_FAULT_FORCED_OVERPRESSURE, true);
    expect_fault_trip(SIM_FAULT_ZERO_CROSS_LOST, true);
}

/*
 * Regresión de H-05: la supervisión debe funcionar sin planta simulada.
 *
 * Antes, el tiempo máximo lo producía el modelo, de modo que al conectar los
 * sensores la máquina habría dejado de recibirlo y la parada por tiempo
 * máximo habría dejado de cumplirse sin que nada lo señalara. Esta prueba no instancia ningún modelo:
 * entrega observaciones como las que produce la adquisición real.
 */
static void test_supervision_without_plant(void)
{
    process_supervisor_t supervisor;
    process_limits_t limits = process_limits_default();
    operational_fsm_t fsm;
    process_supervisor_init(&supervisor);
    operational_fsm_init(&fsm);

    process_observation_t measured = {
        .temperature_c = 22.0F,
        .pressure_bar = 0.0F,
        .temperature_usable = true,
        .pressure_usable = true,
        .zero_cross_observed = false,  /* todavía nadie lo lee */
        .zero_cross_available = false,
    };

    dispatch(&fsm, OP_EVENT_LOGICAL_POWER_TOGGLE);
    assert(fsm.state == OP_STATE_COFFEE_HEATING);
    assert(process_supervisor_evaluate(&supervisor, &limits, &measured,
                                       fsm.state, STEP_MS) == 0U);

    measured.temperature_c = 93.0F;
    uint32_t events = process_supervisor_evaluate(&supervisor, &limits, &measured,
                                                  fsm.state, STEP_MS);
    assert(events == OP_EVENT_MASK(OP_EVENT_COFFEE_TEMPERATURE_READY));
    operational_fsm_dispatch_highest_priority(&fsm, events);
    assert(fsm.state == OP_STATE_READY);

    dispatch(&fsm, OP_EVENT_BREW_BUTTON_PRESSED);
    assert(fsm.state == OP_STATE_EXTRACTION);
    measured.pressure_bar = 9.0F;

    /* Un cruce por cero no observado no puede condenar la preparación. */
    unsigned int elapsed_ms = 0U;
    while (elapsed_ms + STEP_MS < limits.maximum_extraction_ms) {
        assert(process_supervisor_evaluate(&supervisor, &limits, &measured,
                                           fsm.state, STEP_MS) == 0U);
        elapsed_ms += STEP_MS;
    }
    events = process_supervisor_evaluate(&supervisor, &limits, &measured,
                                         fsm.state, STEP_MS);
    assert(events == OP_EVENT_MASK(OP_EVENT_EXTRACTION_TIME_LIMIT));
    const operational_transition_t finish =
        operational_fsm_dispatch_highest_priority(&fsm, events);
    assert(finish.extraction_finished);
    assert(finish.extraction_termination ==
           OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED);

    /* Y una temperatura no apta detiene la máquina aunque nada más falle. */
    measured.temperature_usable = false;
    events = process_supervisor_evaluate(&supervisor, &limits, &measured,
                                         fsm.state, STEP_MS);
    assert(events == OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP));
}

/* Un perfil no puede ampliar un límite de la máquina. */
static void test_profile_cannot_widen_limits(void)
{
    process_limits_t limits = process_limits_default();
    const process_limits_t original = limits;

    assert(process_limits_apply_profile(&limits, 92.0F, 25000U));
    assert(limits.coffee_setpoint_c == 92.0F);
    assert(limits.maximum_extraction_ms == 25000U);
    assert(limits.hard_overtemperature_c == original.hard_overtemperature_c);

    /*
     * Una referencia que la caldera alcanzaría disparando el límite duro antes
     * de declararse preparada no puede adoptarse. El rango de café de un
     * perfil, 80–100 °C, lo impone `extraction_profile`; aquí solo se prueba
     * que el supervisor no admite lo que contradice su propio límite, que es
     * lo que antes garantizaba de rebote la referencia de vapor.
     */
    const process_limits_t before = limits;
    const float unreachable_c =
        limits.hard_overtemperature_c - limits.ready_tolerance_c;
    assert(!process_limits_apply_profile(&limits, unreachable_c, 25000U));
    assert(memcmp(&limits, &before, sizeof(limits)) == 0);
    assert(!process_limits_apply_profile(&limits, 92.0F, 0U));
    assert(memcmp(&limits, &before, sizeof(limits)) == 0);
}

int main(void)
{
    test_safe_inactive_outputs();
    test_heating_extraction_and_time_limit();
    test_purge_limit();
    test_fault_injection();
    test_supervision_without_plant();
    test_profile_cannot_widen_limits();
    puts("PASS: planta, supervisión independiente del origen, tiempos y cinco fallos");
    return 0;
}
