#include "heater_recovery_hold.h"

#include <math.h>
#include <stddef.h>

void heater_recovery_hold_reset(heater_recovery_hold_t *hold)
{
    if (hold != NULL) {
        hold->holding = false;
    }
}

bool heater_recovery_hold_step(heater_recovery_hold_t *hold, bool pump_allowed,
                               float setpoint_c, float measurement_c)
{
    if (hold == NULL) {
        return false;
    }
    if (pump_allowed) {
        hold->holding = true;
    } else if (hold->holding && isfinite(setpoint_c) && isfinite(measurement_c) &&
               measurement_c >= setpoint_c) {
        hold->holding = false;
    }
    return hold->holding;
}
