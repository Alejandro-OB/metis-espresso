#include "signal_quality.h"

#include <math.h>
#include <stddef.h>

#include "sensor_scaling.h"

signal_limits_t signal_limits_temperature(void)
{
    /*
     * El umbral alto de 3,25 V corresponde a unos −12 °C con el divisor del
     * prototipo: por debajo, la tensión se acerca tanto a la alimentación que no
     * puede distinguirse de un sensor desconectado. Cubre cualquier ambiente
     * realista y deja 50 mV de margen frente a la desconexión, muy por encima
     * del ruido del convertidor. Subirlo mediría más frío a costa de ese
     * margen; bajarlo a 3,15 V rechazaría ya lecturas por debajo de 9 °C.
     */
    return (signal_limits_t){
        .minimum_volts = 0.05F,
        .maximum_volts = 3.25F,
        .minimum_value = -10.0F,
        .maximum_value = 160.0F,
        /* Cinco ciclos de su canal, que se convierte cada 100 ms. La caldera es
         * lenta, pero el plazo no mide el proceso sino la pérdida del sensor, y
         * medio segundo sin lectura ya debe retirar el permiso de calentar. */
        .maximum_age_ms = 500U,
    };
}

signal_limits_t signal_limits_pressure(void)
{
    /*
     * El HK1100C entrega 0,5 V a presión nula, que tras el divisor son 0,333 V:
     * una tensión claramente inferior delata el sensor desconectado o su
     * alimentación ausente, no una presión negativa.
     */
    return (signal_limits_t){
        .minimum_volts = 0.20F,
        .maximum_volts = 3.20F,
        .minimum_value = -0.5F,
        .maximum_value = 12.5F,
        /* Cinco ciclos de adquisición de presión, que corre cada 10 ms. El
         * plazo acompaña a la cadencia: con los 500 ms anteriores, una medida
         * de hace medio segundo —cincuenta ciclos— habría seguido gobernando la
         * bomba. */
        .maximum_age_ms = 50U,
    };
}

signal_quality_t signal_quality_evaluate(float volts,
                                         float value,
                                         uint32_t age_ms,
                                         bool acquired,
                                         const signal_limits_t *limits)
{
    if (limits == NULL) {
        return SIGNAL_NOT_ACQUIRED;
    }
    if (!acquired) {
        return SIGNAL_NOT_ACQUIRED;
    }
    if (!isfinite(volts) || !isfinite(value)) {
        return SIGNAL_OUT_OF_RANGE;
    }
    /* La saturación del convertidor se detecta antes que nada: por encima de su
     * fondo de escala la lectura ya no representa la señal. */
    if (volts >= ADS1115_FULL_SCALE_V) {
        return SIGNAL_SATURATED;
    }
    if (volts <= limits->minimum_volts) {
        return SIGNAL_SHORT_CIRCUIT;
    }
    if (volts >= limits->maximum_volts) {
        return SIGNAL_OPEN_CIRCUIT;
    }
    if (value < limits->minimum_value || value > limits->maximum_value) {
        return SIGNAL_OUT_OF_RANGE;
    }
    if (age_ms > limits->maximum_age_ms) {
        return SIGNAL_STALE;
    }
    return SIGNAL_VALID;
}

bool signal_quality_is_usable(signal_quality_t quality)
{
    return quality == SIGNAL_VALID;
}

const char *signal_quality_name(signal_quality_t quality)
{
    switch (quality) {
    case SIGNAL_VALID:
        return "valid";
    case SIGNAL_OPEN_CIRCUIT:
        return "open-circuit";
    case SIGNAL_SHORT_CIRCUIT:
        return "short-circuit";
    case SIGNAL_SATURATED:
        return "saturated";
    case SIGNAL_OUT_OF_RANGE:
        return "out-of-range";
    case SIGNAL_STALE:
        return "stale";
    case SIGNAL_NOT_ACQUIRED:
    default:
        return "not-acquired";
    }
}
