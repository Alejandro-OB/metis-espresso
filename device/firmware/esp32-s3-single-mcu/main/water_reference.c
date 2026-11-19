#include "water_reference.h"

#include <math.h>
#include <stddef.h>

bool water_reference_is_valid(const water_reference_t *relation)
{
    return relation != NULL && relation->defined &&
           isfinite(relation->gain) && relation->gain > 0.0F &&
           isfinite(relation->offset_c) &&
           isfinite(relation->boiler_minimum_c) &&
           isfinite(relation->boiler_maximum_c) &&
           relation->boiler_minimum_c > 0.0F &&
           relation->boiler_minimum_c < relation->boiler_maximum_c;
}

bool water_reference_boiler_setpoint(const water_reference_t *relation,
                                     float requested_c, float *boiler_c)
{
    if (relation == NULL || boiler_c == NULL || !isfinite(requested_c)) {
        return false;
    }
    if (!relation->defined) {
        *boiler_c = requested_c;
        return true;
    }
    if (!water_reference_is_valid(relation)) {
        return false;
    }
    const float boiler = (requested_c - relation->offset_c) / relation->gain;
    if (!isfinite(boiler) ||
        boiler < relation->boiler_minimum_c - WATER_REFERENCE_EDGE_C ||
        boiler > relation->boiler_maximum_c + WATER_REFERENCE_EDGE_C) {
        return false;
    }
    /* Dentro del margen de redondeo se lleva al extremo: el lazo nunca recibe
     * una caldera fuera de lo medido, ni por una millonésima. */
    *boiler_c = fminf(fmaxf(boiler, relation->boiler_minimum_c),
                      relation->boiler_maximum_c);
    return true;
}

float water_reference_water_for(const water_reference_t *relation, float boiler_c)
{
    if (relation == NULL || !relation->defined) {
        return boiler_c;
    }
    return relation->gain * boiler_c + relation->offset_c;
}
