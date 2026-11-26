#include "heater_feedforward.h"

#include <math.h>
#include <stddef.h>

#define DENSITY_FULL_SCALE 100.0F

bool heater_feedforward_configure(heater_feedforward_t *feedforward, float gain)
{
    if (feedforward == NULL) {
        return false;
    }
    feedforward->gain = 0.0F;
    heater_feedforward_reset(feedforward);
    if (!isfinite(gain) || gain < 0.0F) {
        return false;
    }
    feedforward->gain = gain;
    return true;
}

float heater_feedforward_gain_for(float base_gain, float reinforced_gain)
{
    const float base = isfinite(base_gain) && base_gain > 0.0F ? base_gain : 0.0F;
    return isfinite(reinforced_gain) && reinforced_gain > base ? reinforced_gain : base;
}

float heater_feedforward_apply(float pi_output, float feedforward_output)
{
    if (!isfinite(pi_output) || pi_output < 0.0F) {
        return 0.0F;
    }
    if (!isfinite(feedforward_output) || feedforward_output <= 0.0F) {
        return pi_output;
    }
    const float sum = pi_output + feedforward_output;
    return sum > 100.0F ? 100.0F : sum;
}

float heater_feedforward_output(const heater_feedforward_t *feedforward, uint8_t pump_density,
                                bool pump_allowed)
{
    if (feedforward == NULL || !pump_allowed || pump_density == 0U) {
        return 0.0F;
    }
    const float density = pump_density > DENSITY_FULL_SCALE ? DENSITY_FULL_SCALE
                                                            : (float)pump_density;
    return feedforward->gain * density;
}

float heater_feedforward_step(heater_feedforward_t *feedforward, uint8_t pump_density,
                              bool pump_allowed, float temperature_c, float rise_limit_c)
{
    if (feedforward == NULL) {
        return 0.0F;
    }
    if (!pump_allowed) {
        heater_feedforward_reset(feedforward);
        return 0.0F;
    }
    if (!feedforward->pumping) {
        feedforward->pumping = true;
        feedforward->no_water = !isfinite(temperature_c);
        feedforward->start_temperature_c = temperature_c;
    }
    /* Una medida que no se puede comparar no confirma el agua: sin ella la
     * aportación sería otra vez a ciegas, que es lo que este corte evita. */
    if (!isfinite(temperature_c) || !isfinite(rise_limit_c) || rise_limit_c <= 0.0F ||
        temperature_c > feedforward->start_temperature_c + rise_limit_c) {
        feedforward->no_water = true;
    }
    /* Con densidad cero dentro del bombeo (una pausa de la secuencia) no hay
     * aportación, pero el valor al pulsar se conserva: tomarlo otra vez en la
     * pausa, con el metal ya más caliente, aflojaría la comprobación. */
    if (feedforward->no_water || pump_density == 0U) {
        return 0.0F;
    }
    return heater_feedforward_output(feedforward, pump_density, pump_allowed);
}

bool heater_feedforward_no_water(const heater_feedforward_t *feedforward)
{
    return feedforward != NULL && feedforward->no_water;
}

void heater_feedforward_reset(heater_feedforward_t *feedforward)
{
    if (feedforward == NULL) {
        return;
    }
    feedforward->pumping = false;
    feedforward->start_temperature_c = 0.0F;
    feedforward->no_water = false;
}
