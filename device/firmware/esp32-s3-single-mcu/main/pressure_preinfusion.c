#include "pressure_preinfusion.h"

#include <math.h>
#include <stddef.h>

bool pressure_preinfusion_configure(pressure_preinfusion_t *preinfusion,
                                    const pressure_preinfusion_parameters_t *parameters)
{
    if (preinfusion == NULL) {
        return false;
    }
    *preinfusion = (pressure_preinfusion_t){0};
    if (parameters == NULL) {
        return false;
    }
    /* Las comparaciones con NaN son falsas: un valor no finito no pasa. */
    const bool margin_ok = parameters->handover_margin_bar > 0.0F &&
                           isfinite(parameters->handover_margin_bar);
    const bool integral_ok = parameters->integral_start_percent >= 0.0F &&
                             parameters->integral_start_percent <= 100.0F;
    if (!margin_ok || !integral_ok) {
        return false;
    }
    preinfusion->parameters = *parameters;
    preinfusion->configured = true;
    return true;
}

pressure_preinfusion_decision_t pressure_preinfusion_step(
    pressure_preinfusion_t *preinfusion, const pressure_preinfusion_inputs_t *inputs)
{
    pressure_preinfusion_decision_t decision = {0};
    if (preinfusion == NULL || inputs == NULL) {
        return decision;
    }
    /* Fuera de la secuencia, o en su segundo de cero, todo vuelve al principio:
     * la extracción siguiente vuelve a mojar la pastilla desde cero. */
    if (!(inputs->ceiling_percent > 0.0F)) {
        preinfusion->phase = PRESSURE_PREINFUSION_IDLE;
        return decision;
    }
    if (preinfusion->phase == PRESSURE_PREINFUSION_IDLE) {
        preinfusion->phase = PRESSURE_PREINFUSION_WETTING;
    }
    if (preinfusion->phase == PRESSURE_PREINFUSION_WETTING) {
        /* Sin configurar no se pasa nunca. Una referencia o una medida no
         * finitas tampoco hacen pasar: la comparación con NaN es falsa. */
        const float handover =
            inputs->setpoint_bar - preinfusion->parameters.handover_margin_bar;
        if (preinfusion->configured && inputs->setpoint_bar > 0.0F &&
            inputs->pressure_bar >= handover) {
            preinfusion->phase = PRESSURE_PREINFUSION_REGULATING;
            decision.hand_over = true;
        }
    }
    decision.regulate = preinfusion->phase == PRESSURE_PREINFUSION_REGULATING;
    /* La integral de arranque está para la pastilla que acaba de cerrar, con
     * la presión subiendo hacia la referencia. Si al pasar ya está en ella o
     * por encima —presión atrapada de la extracción anterior, o una referencia
     * baja— no hace falta bombear, y precargarla dio un golpe de 0,35 s que
     * subió la presión de 3,3 a 5,9 bar con la referencia en 3 (2026-10-12).
     * El PI arranca entonces en cero y bombea solo cuando la presión baja. */
    decision.integral_start_percent =
        inputs->pressure_bar < inputs->setpoint_bar
            ? preinfusion->parameters.integral_start_percent
            : 0.0F;
    return decision;
}

pressure_preinfusion_phase_t pressure_preinfusion_phase(const pressure_preinfusion_t *preinfusion)
{
    return preinfusion == NULL ? PRESSURE_PREINFUSION_IDLE : preinfusion->phase;
}

const char *pressure_preinfusion_phase_name(pressure_preinfusion_phase_t phase)
{
    switch (phase) {
    case PRESSURE_PREINFUSION_WETTING:
        return "wetting";
    case PRESSURE_PREINFUSION_REGULATING:
        return "pi";
    case PRESSURE_PREINFUSION_IDLE:
    default:
        return "idle";
    }
}
