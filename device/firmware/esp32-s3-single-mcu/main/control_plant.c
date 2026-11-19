#include "control_plant.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

control_plant_parameters_t control_plant_pressure_bench(void)
{
    return (control_plant_parameters_t){
        .gain = 0.09F,            /* 9 bar al 100 % */
        .time_constant_s = 0.40F,
        .dead_time_s = 0.05F,
        .sample_period_s = 0.020F,  /* el periodo del lazo de presión */
        .rest_value = 0.0F,
    };
}

control_plant_parameters_t control_plant_temperature_bench(void)
{
    return (control_plant_parameters_t){
        /* 2,8 °C por punto sobre 200 s de constante dan 1,4 K/s de ascenso
         * inicial al 100 %, que es 1050 W / 750 J/K (ADR-009 § 3.7). */
        .gain = 2.80F,
        .time_constant_s = 200.0F,
        .dead_time_s = 3.0F,        /* la pared de 2,8 mm entre agua y sensor */
        .sample_period_s = 0.200F,  /* el periodo del lazo térmico */
        .rest_value = 22.0F,        /* ambiente */
    };
}

bool control_plant_configure(control_plant_t *plant,
                             const control_plant_parameters_t *parameters)
{
    if (plant == NULL || parameters == NULL) {
        return false;
    }
    if (!isfinite(parameters->gain) || !isfinite(parameters->time_constant_s) ||
        !isfinite(parameters->dead_time_s) || !isfinite(parameters->sample_period_s) ||
        !isfinite(parameters->rest_value)) {
        return false;
    }
    if (parameters->time_constant_s <= 0.0F || parameters->sample_period_s <= 0.0F ||
        parameters->dead_time_s < 0.0F) {
        return false;
    }

    const float samples = parameters->dead_time_s / parameters->sample_period_s;
    const uint32_t delay_samples = (uint32_t)(samples + 0.5F);
    if (delay_samples > CONTROL_PLANT_MAX_DELAY_SAMPLES) {
        return false;
    }

    memset(plant, 0, sizeof(*plant));
    plant->parameters = *parameters;
    plant->delay_samples = delay_samples;
    plant->alpha = parameters->sample_period_s /
                   (parameters->time_constant_s + parameters->sample_period_s);
    plant->value = parameters->rest_value;
    return true;
}

void control_plant_reset(control_plant_t *plant, float initial_value)
{
    if (plant == NULL) {
        return;
    }
    memset(plant->delay_line, 0, sizeof(plant->delay_line));
    plant->delay_index = 0U;
    plant->value = isfinite(initial_value) ? initial_value : plant->parameters.rest_value;
}

float control_plant_steady_state(const control_plant_t *plant, float actuation_percent)
{
    if (plant == NULL) {
        return 0.0F;
    }
    const float actuation = isfinite(actuation_percent) ? actuation_percent : 0.0F;
    return plant->parameters.rest_value + plant->parameters.gain * actuation;
}

float control_plant_step(control_plant_t *plant, float actuation_percent)
{
    if (plant == NULL || plant->parameters.sample_period_s <= 0.0F) {
        return 0.0F;
    }
    const float actuation = isfinite(actuation_percent) ? actuation_percent : 0.0F;

    float effective = actuation;
    if (plant->delay_samples > 0U) {
        /* Se lee antes de escribir sobre la misma posición: el anillo entrega
         * entonces la actuación de hace exactamente `delay_samples` periodos. */
        effective = plant->delay_line[plant->delay_index];
        plant->delay_line[plant->delay_index] = actuation;
        plant->delay_index = (plant->delay_index + 1U) % plant->delay_samples;
    }

    const float target = plant->parameters.rest_value + plant->parameters.gain * effective;
    plant->value += plant->alpha * (target - plant->value);
    return plant->value;
}

float control_plant_value(const control_plant_t *plant)
{
    return plant == NULL ? 0.0F : plant->value;
}
