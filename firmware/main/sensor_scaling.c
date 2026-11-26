#include "sensor_scaling.h"

#include <math.h>
#include <stddef.h>

#define KELVIN_OFFSET 273.15F

ntc_scaling_t sensor_ntc_default(void)
{
    return (ntc_scaling_t){
        .supply_v = 3.3F,
        .series_ohms = 10000.0F,
        .reference_ohms = 100000.0F,
        .reference_c = 25.0F,
        .beta_k = 3950.0F,
    };
}

pressure_scaling_t sensor_pressure_default(void)
{
    /* Con el divisor 2/3 y la alimentación nominal de 5 V del HK1100C,
     * la recta del esquema es Pbar = 4,5 · Vadc − 1,5. */
    return (pressure_scaling_t){.gain_bar_per_v = 4.5F, .offset_bar = -1.5F};
}

float sensor_volts_from_counts(int16_t counts)
{
    return (float)counts * ADS1115_FULL_SCALE_V / ADS1115_FULL_SCALE_COUNTS;
}

float sensor_ntc_resistance_ohms(float volts, const ntc_scaling_t *scaling)
{
    if (scaling == NULL || scaling->supply_v <= 0.0F || scaling->series_ohms <= 0.0F) {
        return -1.0F;
    }
    /* El NTC ocupa la rama inferior: V = Vcc · Rntc / (Rserie + Rntc). */
    const float headroom = scaling->supply_v - volts;
    if (volts <= 0.0F || headroom <= 0.0F) {
        return -1.0F;   /* cortocircuito o circuito abierto */
    }
    return scaling->series_ohms * volts / headroom;
}

bool sensor_ntc_temperature_c(float resistance_ohms, const ntc_scaling_t *scaling,
                              float *temperature_c)
{
    if (scaling == NULL || temperature_c == NULL || resistance_ohms <= 0.0F ||
        scaling->reference_ohms <= 0.0F || scaling->beta_k <= 0.0F) {
        return false;
    }
    const float reference_k = scaling->reference_c + KELVIN_OFFSET;
    const float inverse_k =
        1.0F / reference_k + logf(resistance_ohms / scaling->reference_ohms) / scaling->beta_k;
    if (inverse_k <= 0.0F || !isfinite(inverse_k)) {
        return false;
    }
    *temperature_c = 1.0F / inverse_k - KELVIN_OFFSET;
    return isfinite(*temperature_c);
}

float sensor_pressure_bar(float volts, const pressure_scaling_t *scaling)
{
    if (scaling == NULL) {
        return 0.0F;
    }
    return scaling->gain_bar_per_v * volts + scaling->offset_bar;
}

float sensor_ntc_volts_at(float temperature_c, const ntc_scaling_t *scaling)
{
    if (scaling == NULL) {
        return 0.0F;
    }
    const float reference_k = scaling->reference_c + KELVIN_OFFSET;
    const float target_k = temperature_c + KELVIN_OFFSET;
    const float resistance =
        scaling->reference_ohms * expf(scaling->beta_k * (1.0F / target_k - 1.0F / reference_k));
    return scaling->supply_v * resistance / (scaling->series_ohms + resistance);
}
