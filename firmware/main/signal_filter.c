#include "signal_filter.h"

#include <string.h>

void signal_filter_reset(signal_filter_t *filter)
{
    if (filter == NULL) {
        return;
    }
    memset(filter, 0, sizeof(*filter));
}

/* Inserción directa: la ventana es de cinco elementos y el orden importa más
 * que la complejidad asintótica. */
static void sort_copy(const signal_filter_t *filter, float *ordered, size_t count)
{
    for (size_t index = 0U; index < count; ++index) {
        ordered[index] = filter->samples[index];
    }
    for (size_t index = 1U; index < count; ++index) {
        const float current = ordered[index];
        size_t position = index;
        while (position > 0U && ordered[position - 1U] > current) {
            ordered[position] = ordered[position - 1U];
            --position;
        }
        ordered[position] = current;
    }
}

static float median_of(const signal_filter_t *filter, size_t count)
{
    float ordered[SIGNAL_FILTER_WINDOW] = {0};
    sort_copy(filter, ordered, count);
    if ((count % 2U) == 1U) {
        return ordered[count / 2U];
    }
    return (ordered[count / 2U - 1U] + ordered[count / 2U]) / 2.0F;
}

float signal_filter_push(signal_filter_t *filter, float sample)
{
    if (filter == NULL) {
        return sample;
    }
    filter->samples[filter->next] = sample;
    filter->next = (filter->next + 1U) % SIGNAL_FILTER_WINDOW;
    if (filter->count < SIGNAL_FILTER_WINDOW) {
        ++filter->count;
    }
    return median_of(filter, filter->count);
}

bool signal_filter_value(const signal_filter_t *filter, float *value)
{
    if (filter == NULL || value == NULL || filter->count == 0U) {
        return false;
    }
    *value = median_of(filter, filter->count);
    return true;
}

size_t signal_filter_count(const signal_filter_t *filter)
{
    return filter == NULL ? 0U : filter->count;
}
