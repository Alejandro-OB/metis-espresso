#include "first_use_offset.h"

#include <math.h>
#include <stddef.h>

void first_use_offset_init(first_use_offset_t *state)
{
    if (state != NULL) {
        *state = (first_use_offset_t){0};
    }
}

first_use_offset_check_t first_use_offset_check(float requested_c,
                                                const first_use_offset_bounds_t *bounds,
                                                float boiler_setpoint_c)
{
    if (bounds == NULL) {
        return FIRST_USE_OFFSET_UNCHECKED;
    }
    if (!isfinite(requested_c)) {
        return FIRST_USE_OFFSET_NOT_FINITE;
    }
    if (requested_c < 0.0F) {
        return FIRST_USE_OFFSET_NEGATIVE;
    }
    if (requested_c == 0.0F) {
        return FIRST_USE_OFFSET_NONE;
    }
    /* Un máximo ilegible no puede autorizar nada: cuenta como el cero de un
     * acta sin la sección. */
    const float maximum_c =
        isfinite(bounds->maximum_c) && bounds->maximum_c > 0.0F ? bounds->maximum_c : 0.0F;
    if (requested_c > maximum_c) {
        return FIRST_USE_OFFSET_ABOVE_MAXIMUM;
    }
    /* La misma desigualdad estricta que `process_limits_are_valid`, para que una
     * petición admitida aquí no la rechace después el supervisor. Con límites
     * ilegibles la comparación falla y se rechaza. */
    const float raised_c = boiler_setpoint_c + requested_c;
    if (!isfinite(raised_c) || !isfinite(bounds->hard_limit_c) ||
        !isfinite(bounds->margin_c) ||
        !(bounds->hard_limit_c > raised_c + bounds->margin_c)) {
        return FIRST_USE_OFFSET_ABOVE_HARD_LIMIT;
    }
    return FIRST_USE_OFFSET_ACCEPTED;
}

bool first_use_offset_check_allows(first_use_offset_check_t check)
{
    return check == FIRST_USE_OFFSET_NONE || check == FIRST_USE_OFFSET_ACCEPTED;
}

first_use_feedforward_check_t first_use_feedforward_check(float requested_gain,
                                                          const first_use_offset_bounds_t *bounds)
{
    if (bounds == NULL) {
        return FIRST_USE_FEEDFORWARD_UNCHECKED;
    }
    if (!isfinite(requested_gain)) {
        return FIRST_USE_FEEDFORWARD_NOT_FINITE;
    }
    if (requested_gain == 0.0F) {
        return FIRST_USE_FEEDFORWARD_NONE;
    }
    /* Una ganancia del acta ilegible cuenta como cero: entonces cualquier
     * petición positiva se juzga solo contra el máximo. */
    const float base = isfinite(bounds->feedforward_base_gain) &&
                               bounds->feedforward_base_gain > 0.0F
                           ? bounds->feedforward_base_gain
                           : 0.0F;
    if (requested_gain < base) {
        return FIRST_USE_FEEDFORWARD_BELOW_BASE;
    }
    if (requested_gain == base) {
        return FIRST_USE_FEEDFORWARD_NONE;
    }
    /* Un máximo ilegible, o que no pasa de la del acta, no autoriza nada:
     * es el caso sin el campo en el acta. */
    const float maximum = isfinite(bounds->feedforward_maximum_gain) &&
                                  bounds->feedforward_maximum_gain > base
                              ? bounds->feedforward_maximum_gain
                              : base;
    if (requested_gain > maximum) {
        return FIRST_USE_FEEDFORWARD_ABOVE_MAXIMUM;
    }
    return FIRST_USE_FEEDFORWARD_ACCEPTED;
}

bool first_use_feedforward_check_allows(first_use_feedforward_check_t check)
{
    return check == FIRST_USE_FEEDFORWARD_NONE || check == FIRST_USE_FEEDFORWARD_ACCEPTED;
}

first_use_offset_check_t first_use_offset_adopt_request(
    first_use_offset_t *state, float requested_c, float requested_gain,
    const first_use_offset_bounds_t *bounds, float boiler_setpoint_c,
    first_use_feedforward_check_t *feedforward_check)
{
    const first_use_feedforward_check_t gain_check =
        first_use_feedforward_check(requested_gain, bounds);
    if (feedforward_check != NULL) {
        *feedforward_check = gain_check;
    }
    if (state == NULL) {
        return FIRST_USE_OFFSET_UNCHECKED;
    }
    const first_use_offset_check_t check =
        first_use_offset_check(requested_c, bounds, boiler_setpoint_c);
    first_use_offset_init(state);
    /* Todo o nada: un refuerzo rechazado no deja armada la subida, ni al
     * revés, porque el perfil que los pidió se rechaza entero. */
    if (!first_use_offset_check_allows(check) ||
        !first_use_feedforward_check_allows(gain_check)) {
        return check;
    }
    const bool offset = check == FIRST_USE_OFFSET_ACCEPTED;
    const bool reinforced = gain_check == FIRST_USE_FEEDFORWARD_ACCEPTED;
    if (offset || reinforced) {
        state->phase = FIRST_USE_OFFSET_ARMED;
        state->offset_c = offset ? requested_c : 0.0F;
        state->feedforward_gain = reinforced ? requested_gain : 0.0F;
    }
    return check;
}

first_use_offset_check_t first_use_offset_adopt(first_use_offset_t *state,
                                                float requested_c,
                                                const first_use_offset_bounds_t *bounds,
                                                float boiler_setpoint_c)
{
    return first_use_offset_adopt_request(state, requested_c, 0.0F, bounds, boiler_setpoint_c,
                                          NULL);
}

bool first_use_offset_observe(first_use_offset_t *state, operational_state_t operational)
{
    if (state == NULL) {
        return false;
    }
    const bool extracting = operational == OP_STATE_EXTRACTION;
    switch (state->phase) {
    case FIRST_USE_OFFSET_ARMED:
        if (!extracting) {
            state->outside_seen = true;
        } else if (state->outside_seen) {
            state->phase = FIRST_USE_OFFSET_BREWING;
        }
        return false;
    case FIRST_USE_OFFSET_BREWING:
        if (extracting) {
            return false;
        }
        /* Termine como termine —parada normal, tiempo máximo o protección—,
         * la preparación ya usó el agua que se calentó para ella. */
        first_use_offset_init(state);
        return true;
    case FIRST_USE_OFFSET_IDLE:
    default:
        return false;
    }
}

float first_use_offset_applied_c(const first_use_offset_t *state)
{
    if (state == NULL || (state->phase != FIRST_USE_OFFSET_ARMED &&
                          state->phase != FIRST_USE_OFFSET_BREWING)) {
        return 0.0F;
    }
    return state->offset_c;
}

float first_use_offset_feedforward_gain(const first_use_offset_t *state)
{
    if (state == NULL || (state->phase != FIRST_USE_OFFSET_ARMED &&
                          state->phase != FIRST_USE_OFFSET_BREWING)) {
        return 0.0F;
    }
    return state->feedforward_gain;
}

const char *first_use_feedforward_check_name(first_use_feedforward_check_t check)
{
    switch (check) {
    case FIRST_USE_FEEDFORWARD_UNCHECKED:
        return "unchecked";
    case FIRST_USE_FEEDFORWARD_NONE:
        return "none";
    case FIRST_USE_FEEDFORWARD_ACCEPTED:
        return "accepted";
    case FIRST_USE_FEEDFORWARD_NOT_FINITE:
        return "not-finite";
    case FIRST_USE_FEEDFORWARD_BELOW_BASE:
        return "below-base";
    case FIRST_USE_FEEDFORWARD_ABOVE_MAXIMUM:
        return "above-maximum";
    default:
        return "unknown";
    }
}

const char *first_use_offset_check_name(first_use_offset_check_t check)
{
    switch (check) {
    case FIRST_USE_OFFSET_UNCHECKED:
        return "unchecked";
    case FIRST_USE_OFFSET_NONE:
        return "none";
    case FIRST_USE_OFFSET_ACCEPTED:
        return "accepted";
    case FIRST_USE_OFFSET_NOT_FINITE:
        return "not-finite";
    case FIRST_USE_OFFSET_NEGATIVE:
        return "negative";
    case FIRST_USE_OFFSET_ABOVE_MAXIMUM:
        return "above-maximum";
    case FIRST_USE_OFFSET_ABOVE_HARD_LIMIT:
        return "above-hard-limit";
    default:
        return "unknown";
    }
}

const char *first_use_offset_phase_name(first_use_offset_phase_t phase)
{
    switch (phase) {
    case FIRST_USE_OFFSET_IDLE:
        return "idle";
    case FIRST_USE_OFFSET_ARMED:
        return "armed";
    case FIRST_USE_OFFSET_BREWING:
        return "brewing";
    default:
        return "unknown";
    }
}
