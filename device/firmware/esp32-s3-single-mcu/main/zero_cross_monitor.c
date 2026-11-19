#include "zero_cross_monitor.h"

#include <math.h>
#include <string.h>

zero_cross_limits_t zero_cross_limits_for(float nominal_hz,
                                          float tolerance_fraction,
                                          uint32_t settling_events)
{
    zero_cross_limits_t limits = {0};
    if (!isfinite(nominal_hz) || nominal_hz <= 0.0F || !isfinite(tolerance_fraction) ||
        tolerance_fraction < 0.0F || tolerance_fraction >= 1.0F) {
        return limits;
    }
    const float nominal_period_us = 1000000.0F / nominal_hz;
    /* Una frecuencia más alta es un periodo más corto: la tolerancia se aplica
     * al periodo, de modo que el signo se invierte respecto a la frecuencia. */
    limits.minimum_period_us = (uint32_t)(nominal_period_us * (1.0F - tolerance_fraction));
    limits.maximum_period_us = (uint32_t)(nominal_period_us * (1.0F + tolerance_fraction));
    limits.timeout_us = (uint32_t)(nominal_period_us * 3.0F);
    limits.settling_events = settling_events;
    limits.bounce_window_us = ZERO_CROSS_BOUNCE_WINDOW_US;
    return limits;
}

bool zero_cross_monitor_configure(zero_cross_monitor_t *monitor,
                                  const zero_cross_limits_t *limits)
{
    if (monitor == NULL || limits == NULL) {
        return false;
    }
    if (limits->minimum_period_us == 0U || limits->maximum_period_us <= limits->minimum_period_us ||
        limits->timeout_us == 0U || limits->bounce_window_us >= limits->minimum_period_us) {
        return false;
    }
    memset(monitor, 0, sizeof(*monitor));
    monitor->limits = *limits;
    return true;
}

void zero_cross_monitor_reset(zero_cross_monitor_t *monitor)
{
    if (monitor == NULL) {
        return;
    }
    const zero_cross_limits_t limits = monitor->limits;
    memset(monitor, 0, sizeof(*monitor));
    monitor->limits = limits;
}

bool zero_cross_monitor_event(zero_cross_monitor_t *monitor, uint64_t timestamp_us)
{
    if (monitor == NULL) {
        return false;
    }
    monitor->total_events++;
    monitor->last_was_bounce = false;

    if (!monitor->has_previous) {
        /* El primer flanco no define ningún periodo: solo establece el origen
         * contra el que se medirá el siguiente. */
        monitor->last_event_us = timestamp_us;
        monitor->has_previous = true;
        return false;
    }

    /* Una marca anterior a la previa solo puede venir de un reloj que retrocede.
     * Se trata como rechazo y se reancla el origen, para no calcular un periodo
     * enorme por desbordamiento de la resta sin signo. */
    if (timestamp_us <= monitor->last_event_us) {
        monitor->rejected_events++;
        monitor->consecutive_good = 0U;
        monitor->last_event_us = timestamp_us;
        return false;
    }

    const uint64_t interval = timestamp_us - monitor->last_event_us;

    if (interval < (uint64_t)monitor->limits.minimum_period_us) {
        /*
         * Demasiado corto para ser un cruce: es un rebote del detector. No se
         * adopta como nuevo origen a propósito. Si se adoptara, el siguiente
         * flanco real se mediría contra el rebote y produciría un periodo corto
         * que también sería rechazado, de modo que un único rebote invalidaría
         * dos periodos en lugar de uno.
         */
        monitor->rejected_events++;
        const uint32_t since = (uint32_t)interval;
        /* Pegado al cruce anterior es el mismo cruce rebotando: se descarta y
         * se cuenta, pero no dice nada de la estabilidad de la señal, que es lo
         * que mide la cuenta de periodos buenos. Más lejos, sí la reinicia. */
        if (since < monitor->limits.bounce_window_us) {
            monitor->last_was_bounce = true;
        } else {
            monitor->consecutive_good = 0U;
        }
        monitor->short_rejections++;
        if (monitor->shortest_short_us == 0U || since < monitor->shortest_short_us) {
            monitor->shortest_short_us = since;
        }
        if (since > monitor->longest_short_us) {
            monitor->longest_short_us = since;
        }
        return false;
    }

    monitor->last_event_us = timestamp_us;
    /* Se registra antes de juzgarlo: un intervalo fuera de rango es
     * precisamente el que hay que poder leer en la puesta en marcha. */
    monitor->last_interval_us =
        interval > (uint64_t)UINT32_MAX ? UINT32_MAX : (uint32_t)interval;

    if (interval > (uint64_t)monitor->limits.maximum_period_us) {
        monitor->rejected_events++;
        monitor->consecutive_good = 0U;
        return false;
    }

    monitor->last_period_us = (uint32_t)interval;
    if (monitor->consecutive_good < UINT32_MAX) {
        monitor->consecutive_good++;
    }
    return true;
}

bool zero_cross_monitor_usable(const zero_cross_monitor_t *monitor, uint64_t now_us)
{
    if (monitor == NULL || !monitor->has_previous) {
        return false;
    }
    if (monitor->consecutive_good < monitor->limits.settling_events) {
        return false;
    }
    if (now_us <= monitor->last_event_us) {
        return true;  /* el flanco es de ahora mismo */
    }
    return (now_us - monitor->last_event_us) <= (uint64_t)monitor->limits.timeout_us;
}

float zero_cross_monitor_frequency_hz(const zero_cross_monitor_t *monitor)
{
    if (monitor == NULL || monitor->last_period_us == 0U) {
        return 0.0F;
    }
    return 1000000.0F / (float)monitor->last_period_us;
}

uint32_t zero_cross_monitor_last_period_us(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? 0U : monitor->last_period_us;
}

uint32_t zero_cross_monitor_last_interval_us(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? 0U : monitor->last_interval_us;
}

float zero_cross_monitor_observed_frequency_hz(const zero_cross_monitor_t *monitor)
{
    if (monitor == NULL || monitor->last_interval_us == 0U) {
        return 0.0F;
    }
    return 1000000.0F / (float)monitor->last_interval_us;
}

uint32_t zero_cross_monitor_total_events(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? 0U : monitor->total_events;
}

uint32_t zero_cross_monitor_rejected_events(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? 0U : monitor->rejected_events;
}

uint32_t zero_cross_monitor_short_rejections(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? 0U : monitor->short_rejections;
}

uint32_t zero_cross_monitor_shortest_short_us(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? 0U : monitor->shortest_short_us;
}

uint32_t zero_cross_monitor_longest_short_us(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? 0U : monitor->longest_short_us;
}

bool zero_cross_monitor_last_was_bounce(const zero_cross_monitor_t *monitor)
{
    return monitor == NULL ? false : monitor->last_was_bounce;
}
