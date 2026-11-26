#include "heater_fast_start.h"

#include <math.h>
#include <stddef.h>

#define FULL_POWER_PERCENT 100.0F

static bool positive(float value)
{
    return isfinite(value) && value > 0.0F;
}

static bool non_negative(float value)
{
    return isfinite(value) && value >= 0.0F;
}

bool heater_fast_start_configure(heater_fast_start_t *fast_start,
                                 const heater_fast_start_parameters_t *parameters)
{
    if (fast_start == NULL) {
        return false;
    }
    *fast_start = (heater_fast_start_t){0};
    if (parameters == NULL || !positive(parameters->margin_c) ||
        !non_negative(parameters->activation_band_c) || !positive(parameters->max_boost_s) ||
        !positive(parameters->max_setpoint_c) || !non_negative(parameters->release_drop_c)) {
        return false;
    }
    fast_start->parameters = *parameters;
    fast_start->configured = true;
    return true;
}

static heater_fast_start_decision_t hand_over(heater_fast_start_t *fast_start)
{
    fast_start->phase = HEATER_FAST_START_HANDED_OVER;
    return (heater_fast_start_decision_t){.hand_over = true};
}

static bool may_arm(const heater_fast_start_parameters_t *p, const heater_fast_start_inputs_t *in)
{
    return in->measurement_valid && !in->pump_allowed && isfinite(in->setpoint_c) &&
           isfinite(in->temperature_c) && in->setpoint_c <= p->max_setpoint_c &&
           in->temperature_c < in->setpoint_c - p->margin_c - p->activation_band_c;
}

heater_fast_start_decision_t heater_fast_start_step(heater_fast_start_t *fast_start,
                                                    const heater_fast_start_inputs_t *inputs)
{
    const heater_fast_start_decision_t pi_rules = {0};
    if (fast_start == NULL || inputs == NULL || !fast_start->configured) {
        return pi_rules;
    }
    if (!inputs->heating) {
        /* Fuera de «calentando» termina el episodio: el próximo vuelve a
         * decidir. Si estaba al 100 %, el estado que lo sacó ya cortó. */
        fast_start->phase = HEATER_FAST_START_IDLE;
        fast_start->used_in_episode = false;
        return pi_rules;
    }

    const heater_fast_start_parameters_t *p = &fast_start->parameters;
    switch (fast_start->phase) {
    case HEATER_FAST_START_IDLE:
        /* Se decide una sola vez, en el primer ciclo del episodio: si no
         * arma, el episodio entero es del PI. */
        if (fast_start->used_in_episode) {
            return pi_rules;
        }
        fast_start->used_in_episode = true;
        if (!may_arm(p, inputs)) {
            return pi_rules;
        }
        fast_start->phase = HEATER_FAST_START_BOOST;
        fast_start->end = HEATER_FAST_START_END_NONE;
        fast_start->boost_s = 0.0F;
        return (heater_fast_start_decision_t){.overrides = true, .output = FULL_POWER_PERCENT};

    case HEATER_FAST_START_BOOST:
        if (!inputs->measurement_valid || inputs->pump_allowed ||
            !isfinite(inputs->temperature_c)) {
            fast_start->end = HEATER_FAST_START_END_ABANDONED;
            return hand_over(fast_start);
        }
        fast_start->boost_s += non_negative(inputs->elapsed_s) ? inputs->elapsed_s : 0.0F;
        if (inputs->temperature_c >= inputs->setpoint_c - p->margin_c) {
            fast_start->end = HEATER_FAST_START_END_CUT;
        } else if (fast_start->boost_s >= p->max_boost_s) {
            /* La energía queda acotada aunque el sensor no llegue nunca. */
            fast_start->end = HEATER_FAST_START_END_TIMEOUT;
        } else {
            return (heater_fast_start_decision_t){.overrides = true,
                                                  .output = FULL_POWER_PERCENT};
        }
        fast_start->phase = HEATER_FAST_START_COAST;
        fast_start->peak_c = inputs->temperature_c;
        fast_start->boost_s = 0.0F;
        return (heater_fast_start_decision_t){.overrides = true, .output = 0.0F};

    case HEATER_FAST_START_COAST:
        /* A 0 % hasta que deje de subir. Pasar al PI en el instante del corte
         * le haría pedir Kp·δ mientras el calor acumulado aún sube (ADR-009). */
        if (!inputs->measurement_valid || inputs->pump_allowed ||
            !isfinite(inputs->temperature_c)) {
            return hand_over(fast_start);
        }
        if (inputs->temperature_c > fast_start->peak_c) {
            fast_start->peak_c = inputs->temperature_c;
        }
        fast_start->boost_s += non_negative(inputs->elapsed_s) ? inputs->elapsed_s : 0.0F;
        /* La espera tampoco es indefinida: una lectura que no baja nunca no
         * debe dejar la caldera sin calentar. */
        if (inputs->temperature_c <= fast_start->peak_c - p->release_drop_c ||
            fast_start->boost_s >= p->max_boost_s) {
            return hand_over(fast_start);
        }
        return (heater_fast_start_decision_t){.overrides = true, .output = 0.0F};

    case HEATER_FAST_START_HANDED_OVER:
    default:
        return pi_rules;
    }
}

heater_fast_start_phase_t heater_fast_start_phase(const heater_fast_start_t *fast_start)
{
    return fast_start == NULL ? HEATER_FAST_START_IDLE : fast_start->phase;
}

heater_fast_start_end_t heater_fast_start_end(const heater_fast_start_t *fast_start)
{
    return fast_start == NULL ? HEATER_FAST_START_END_NONE : fast_start->end;
}

const char *heater_fast_start_phase_name(heater_fast_start_phase_t phase)
{
    switch (phase) {
    case HEATER_FAST_START_BOOST:
        return "boost";
    case HEATER_FAST_START_COAST:
        return "coast";
    case HEATER_FAST_START_HANDED_OVER:
        return "pi";
    case HEATER_FAST_START_IDLE:
    default:
        return "idle";
    }
}
