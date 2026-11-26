#include "process_supervisor.h"

#include "commissioned_plant.h"

#include <float.h>
#include <stddef.h>

static uint32_t saturating_add_u32(uint32_t value, uint32_t increment)
{
    return UINT32_MAX - value < increment ? UINT32_MAX : value + increment;
}

process_limits_t process_limits_default(void)
{
    /*
     * Valores nominales que hacen observable el prototipo en banco. No son
     * referencias puestas en servicio ni límites de seguridad caracterizados:
     * el límite duro definitivo se fija con la cafetera montada.
     */
    process_limits_t limits = {
        .coffee_setpoint_c = 93.0F,
        .ready_tolerance_c = 0.5F,
        .lost_delta_c = 2.0F,
        .hard_overtemperature_c = 150.0F,
        .hard_overpressure_bar = 12.0F,
        .maximum_extraction_ms = 30000U,
        .maximum_purge_ms = 10000U,
    };
    /*
     * El límite duro de un lazo puesto en servicio es el de su acta
     * (ADR-020 § 4.2): se congela con la planta y no puede quedarse en el
     * provisional de banco mientras ese lazo actúa. Si el del acta no dejara
     * límites válidos —por ejemplo, un límite térmico que la referencia de
     * café alcanzaría antes de declararse preparada—, el supervisor se niega
     * a arrancar y nada se energiza.
     */
#if HAVE_COMMISSIONED_PRESSURE
    limits.hard_overpressure_bar = COMMISSIONED_PRESSURE_HARD_LIMIT;
#endif
#if HAVE_COMMISSIONED_TEMPERATURE
    limits.hard_overtemperature_c = COMMISSIONED_TEMPERATURE_HARD_LIMIT;
#endif
    return limits;
}

/*
 * Sin vapor (ADR-006, revisión del 2026-10-06), la referencia de café es la
 * única que se regula y el límite duro solo tiene que quedar por encima de ella
 * con la banda de preparada incluida: si no, la caldera dispararía la
 * protección justo al llegar a la condición que se le pide. Antes la referencia
 * de vapor se interponía entre ambas y daba ese margen por añadidura.
 */
bool process_limits_are_valid(const process_limits_t *limits)
{
    return limits != NULL &&
           limits->coffee_setpoint_c > 0.0F &&
           limits->hard_overtemperature_c >
               limits->coffee_setpoint_c + limits->ready_tolerance_c &&
           limits->ready_tolerance_c > 0.0F &&
           limits->lost_delta_c > limits->ready_tolerance_c &&
           limits->hard_overpressure_bar > 0.0F &&
           limits->maximum_extraction_ms > 0U &&
           limits->maximum_purge_ms > 0U;
}

bool process_limits_apply_profile(process_limits_t *limits,
                                  float temperature_setpoint_c,
                                  uint32_t maximum_time_ms)
{
    if (limits == NULL || maximum_time_ms == 0U || temperature_setpoint_c <= 0.0F) {
        return false;
    }
    process_limits_t candidate = *limits;
    candidate.coffee_setpoint_c = temperature_setpoint_c;
    candidate.maximum_extraction_ms = maximum_time_ms;
    if (!process_limits_are_valid(&candidate)) {
        return false;
    }
    *limits = candidate;
    return true;
}

bool process_limits_lower_for_trip_test(process_limits_t *limits, float limit_c)
{
    if (limits == NULL || !(limit_c > 0.0F)) {
        return false;
    }
    process_limits_t candidate = *limits;
    if (limit_c < candidate.hard_overtemperature_c) {
        candidate.hard_overtemperature_c = limit_c;
    }
    const float setpoint = candidate.hard_overtemperature_c - PROCESS_TRIP_TEST_SETPOINT_MARGIN_C;
    if (setpoint < candidate.coffee_setpoint_c) {
        candidate.coffee_setpoint_c = setpoint;
    }
    if (!process_limits_are_valid(&candidate)) {
        return false;
    }
    *limits = candidate;
    return true;
}

bool process_limits_for_pump_diagnostic(process_limits_t *limits)
{
    if (limits == NULL) {
        return false;
    }
    process_limits_t candidate = *limits;
    candidate.coffee_setpoint_c = PROCESS_PUMP_DIAGNOSTIC_SETPOINT_C;
    if (!process_limits_are_valid(&candidate)) {
        return false;
    }
    *limits = candidate;
    return true;
}

bool process_limits_set_purge(process_limits_t *limits, uint32_t maximum_purge_ms)
{
    if (limits == NULL || maximum_purge_ms < PROCESS_PURGE_MINIMUM_MS ||
        maximum_purge_ms > PROCESS_PURGE_MAXIMUM_MS) {
        return false;
    }
    process_limits_t candidate = *limits;
    candidate.maximum_purge_ms = maximum_purge_ms;
    if (!process_limits_are_valid(&candidate)) {
        return false;
    }
    *limits = candidate;
    return true;
}

void process_supervisor_init(process_supervisor_t *supervisor)
{
    if (supervisor == NULL) {
        return;
    }
    *supervisor = (process_supervisor_t){
        .previous_state = OP_STATE_SAFE_INACTIVE,
        .initialized = true,
    };
}

static bool state_uses_pump(operational_state_t state)
{
    return state == OP_STATE_EXTRACTION || state == OP_STATE_MANUAL_PURGE;
}

static bool state_is_supervised(operational_state_t state)
{
    /*
     * En seguro y en alarma no hay proceso que supervisar: en el primero nada
     * está autorizado y en el segundo la protección ya actuó. Seguir disparando
     * allí solo produciría eventos redundantes.
     */
    return state != OP_STATE_SAFE_INACTIVE && state != OP_STATE_ALARM;
}

static void update_timers(process_supervisor_t *supervisor,
                          operational_state_t state,
                          uint32_t elapsed_ms)
{
    if (state != supervisor->previous_state) {
        supervisor->state_elapsed_ms = 0U;
        supervisor->previous_state = state;
    }
    supervisor->state_elapsed_ms =
        saturating_add_u32(supervisor->state_elapsed_ms, elapsed_ms);
}

/*
 * Una condición insegura se evalúa antes que cualquier umbral de proceso, y la
 * temperatura no apta cuenta como tal: sin ella no puede afirmarse que la
 * caldera esté dentro de su límite, y afirmar que no hay peligro por no poder
 * medirlo sería la lectura equivocada del silencio.
 */
static bool safety_trip_required(const process_limits_t *limits,
                                 const process_observation_t *observation,
                                 operational_state_t state)
{
    if (!observation->temperature_usable) {
        return true;
    }
    if (observation->temperature_c >= limits->hard_overtemperature_c) {
        return true;
    }
    if (observation->pressure_usable &&
        observation->pressure_bar >= limits->hard_overpressure_bar) {
        return true;
    }
    if (!state_uses_pump(state)) {
        return false;
    }
    /* Con la bomba en uso, la presión deja de ser información y pasa a ser
     * condición de seguridad. El cruce por cero solo condena cuando se observa:
     * declarar perdido lo que nadie mira impediría toda preparación. Sin red la
     * presión no apta no condena: el transductor se alimenta de la red y la
     * bomba, que se dispara en cada cruce, no puede moverse; lo que toca es
     * volver a reposo (`OP_EVENT_MAINS_LOST`), no una alarma. */
    return (!observation->pressure_usable && !observation->mains_absent) ||
           (observation->zero_cross_observed && !observation->zero_cross_available);
}

uint32_t process_supervisor_evaluate(process_supervisor_t *supervisor,
                                     const process_limits_t *limits,
                                     const process_observation_t *observation,
                                     operational_state_t state,
                                     uint32_t elapsed_ms)
{
    if (supervisor == NULL || !supervisor->initialized || observation == NULL ||
        !process_limits_are_valid(limits) || elapsed_ms == 0U) {
        return 0U;
    }

    update_timers(supervisor, state, elapsed_ms);

    if (!state_is_supervised(state)) {
        return 0U;
    }
    if (safety_trip_required(limits, observation, state)) {
        return OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP);
    }
    supervisor->mains_absent_ms =
        observation->mains_absent ? saturating_add_u32(supervisor->mains_absent_ms, elapsed_ms)
                                  : 0U;
    if (supervisor->mains_absent_ms >= PROCESS_MAINS_LOSS_MS) {
        return OP_EVENT_MASK(OP_EVENT_MAINS_LOST);
    }

    switch (state) {
    case OP_STATE_COFFEE_HEATING:
        if (observation->temperature_c >=
            limits->coffee_setpoint_c - limits->ready_tolerance_c) {
            return OP_EVENT_MASK(OP_EVENT_COFFEE_TEMPERATURE_READY);
        }
        break;
    case OP_STATE_READY:
        if (observation->temperature_c <
            limits->coffee_setpoint_c - limits->lost_delta_c) {
            return OP_EVENT_MASK(OP_EVENT_COFFEE_TEMPERATURE_LOST);
        }
        break;
    case OP_STATE_EXTRACTION:
        if (supervisor->state_elapsed_ms >= limits->maximum_extraction_ms) {
            return OP_EVENT_MASK(OP_EVENT_EXTRACTION_TIME_LIMIT);
        }
        break;
    case OP_STATE_MANUAL_PURGE:
        if (supervisor->state_elapsed_ms >= limits->maximum_purge_ms) {
            return OP_EVENT_MASK(OP_EVENT_PURGE_TIME_LIMIT);
        }
        break;
    case OP_STATE_SAFE_INACTIVE:
    case OP_STATE_ALARM:
    default:
        break;
    }
    return 0U;
}

bool process_supervisor_clear_allowed(const process_limits_t *limits,
                                      const process_observation_t *observation)
{
    if (observation == NULL || !process_limits_are_valid(limits)) {
        return false;
    }
    return !safety_trip_required(limits, observation, OP_STATE_SAFE_INACTIVE);
}

uint32_t process_supervisor_state_elapsed_ms(const process_supervisor_t *supervisor)
{
    return supervisor != NULL && supervisor->initialized
               ? supervisor->state_elapsed_ms
               : 0U;
}

bool process_supervisor_ready_withdrawn(const process_limits_t *limits,
                                        const process_observation_t *observation,
                                        operational_state_t state)
{
    if (state != OP_STATE_READY || observation == NULL ||
        !observation->temperature_usable || !process_limits_are_valid(limits)) {
        return false;
    }
    return observation->temperature_c <
           limits->coffee_setpoint_c - limits->ready_tolerance_c;
}
