#include "heater_recovery_return.h"

#include <math.h>
#include <stddef.h>

/* La ventana dura tres constantes de tiempo: ahí la exponencial ha recorrido
 * el 95 % del camino y el resto lo deja al PI. */
#define WINDOW_TAUS 3.0F

bool heater_recovery_return_configure(heater_recovery_return_t *recovery, float tau_s)
{
    if (recovery == NULL) {
        return false;
    }
    heater_recovery_return_reset(recovery);
    if (!isfinite(tau_s) || tau_s < 0.0F) {
        recovery->tau_s = 0.0F;
        return false;
    }
    recovery->tau_s = tau_s;
    return true;
}

void heater_recovery_return_reset(heater_recovery_return_t *recovery)
{
    if (recovery == NULL) {
        return;
    }
    recovery->pumping = false;
    recovery->active = false;
    recovery->latched = false;
    recovery->i0 = 0.0F;
    recovery->elapsed_s = 0.0F;
}

heater_recovery_return_decision_t heater_recovery_return_step(heater_recovery_return_t *recovery,
                                                              bool pump_allowed, float integral,
                                                              float elapsed_s)
{
    heater_recovery_return_decision_t decision = {0};
    if (recovery == NULL || recovery->tau_s <= 0.0F || !isfinite(elapsed_s) || elapsed_s <= 0.0F) {
        return decision;
    }
    if (pump_allowed) {
        /* Al arrancar la bomba se guarda el sostenimiento, salvo que la
         * devolución del café anterior siga en curso: ese valor ya no lo es. */
        if (!recovery->pumping && !recovery->latched && isfinite(integral)) {
            recovery->i0 = integral;
            recovery->latched = true;
        }
        recovery->pumping = true;
        return decision;
    }
    if (recovery->pumping) {
        recovery->pumping = false;
        recovery->active = recovery->latched;
        recovery->elapsed_s = 0.0F;
    }
    if (!recovery->active) {
        return decision;
    }
    recovery->elapsed_s += elapsed_s;
    if (recovery->elapsed_s > WINDOW_TAUS * recovery->tau_s) {
        recovery->active = false;
        recovery->latched = false;
        return decision;
    }
    decision.apply = true;
    decision.target = recovery->i0;
    decision.fraction = elapsed_s / recovery->tau_s;
    if (decision.fraction > 1.0F) {
        decision.fraction = 1.0F;
    }
    return decision;
}
