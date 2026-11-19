#include "control_filter.h"

#include <math.h>
#include <string.h>

bool control_filter_configure(control_filter_t *filter,
                              float time_constant_s,
                              float sample_period_s)
{
    if (filter == NULL || !isfinite(time_constant_s) || !isfinite(sample_period_s) ||
        sample_period_s <= 0.0F) {
        return false;
    }
    memset(filter, 0, sizeof(*filter));
    if (time_constant_s <= 0.0F) {
        filter->alpha = 1.0F;  /* sin constante de tiempo no hay filtrado */
        return true;
    }
    filter->alpha = sample_period_s / (time_constant_s + sample_period_s);
    return true;
}

void control_filter_reset(control_filter_t *filter)
{
    if (filter == NULL) {
        return;
    }
    filter->value = 0.0F;
    filter->primed = false;
}

float control_filter_push(control_filter_t *filter, float sample)
{
    if (filter == NULL) {
        return sample;
    }
    /* Una muestra no finita no puede incorporarse ni descartarse en silencio:
     * se devuelve tal cual para que la califique quien evalúa la calidad de la
     * señal, y el estado del filtro queda intacto. */
    if (!isfinite(sample)) {
        return sample;
    }
    if (!filter->primed) {
        filter->value = sample;
        filter->primed = true;
        return filter->value;
    }
    filter->value += filter->alpha * (sample - filter->value);
    return filter->value;
}

bool control_filter_value(const control_filter_t *filter, float *value)
{
    if (filter == NULL || value == NULL || !filter->primed) {
        return false;
    }
    *value = filter->value;
    return true;
}

float control_filter_alpha(const control_filter_t *filter)
{
    return filter == NULL ? 0.0F : filter->alpha;
}
