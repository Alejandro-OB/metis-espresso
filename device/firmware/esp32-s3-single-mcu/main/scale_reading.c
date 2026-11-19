#include "scale_reading.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

int32_t scale_reading_sign_extend(uint32_t raw)
{
    const uint32_t value = raw & 0x00FFFFFFU;
    if ((value & 0x00800000U) != 0U) {
        /* Se completan los ocho bits altos con unos. La resta explícita evita
         * depender de cómo convierte la implementación un `uint32_t` grande a
         * `int32_t`, que antes de C23 no está definido para todos los valores. */
        return (int32_t)(value) - 16777216;
    }
    return (int32_t)value;
}

scale_reading_t scale_reading_evaluate(int32_t counts,
                                       const scale_calibration_t *calibration,
                                       uint32_t age_ms,
                                       uint32_t maximum_age_ms,
                                       bool acquired,
                                       bool released)
{
    scale_reading_t reading = {
        .counts = counts,
        .grams = 0.0F,
        .mass_available = false,
        .quality = SIGNAL_NOT_ACQUIRED,
    };

    if (!acquired) {
        /* El hilo de datos nunca se puso a nivel bajo: no hay conversión, y las
         * cuentas que se hubieran leído no significan nada. */
        reading.counts = 0;
        return reading;
    }

    /*
     * El orden va de la causa más específica a la más general, igual que en
     * `signal_quality_evaluate`, para que el diagnóstico señale el fallo y no su
     * consecuencia.
     *
     * Los dos extremos del convertidor no son «mucho peso»: con una célula de
     * carga sana no se alcanzan, y delatan una célula desconectada, un puente
     * roto o una excitación ausente.
     */
    if (!released) {
        reading.quality = SIGNAL_SHORT_CIRCUIT;
        return reading;
    }
    if (counts <= SCALE_COUNTS_MINIMUM || counts >= SCALE_COUNTS_MAXIMUM) {
        reading.quality = SIGNAL_SATURATED;
        return reading;
    }
    if (maximum_age_ms > 0U && age_ms > maximum_age_ms) {
        reading.quality = SIGNAL_STALE;
        return reading;
    }

    reading.quality = SIGNAL_VALID;

    /*
     * Los gramos solo existen si alguien midió el factor con una masa patrón.
     * Sin él la lectura es válida —las cuentas son reales— pero la masa no está
     * disponible, y eso se declara en vez de rellenarse.
     */
    if (calibration != NULL && isfinite(calibration->counts_per_gram) &&
        calibration->counts_per_gram > 0.0F) {
        const float net = (float)(counts - calibration->tare_counts);
        reading.grams = net / calibration->counts_per_gram;
        reading.mass_available = isfinite(reading.grams);
    }
    return reading;
}

void scale_stability_reset(scale_stability_t *window)
{
    if (window == NULL) {
        return;
    }
    memset(window, 0, sizeof(*window));
}

uint32_t scale_stability_spread(const scale_stability_t *window)
{
    if (window == NULL || window->count == 0U) {
        return 0U;
    }
    int32_t minimum = window->samples[0];
    int32_t maximum = window->samples[0];
    for (uint32_t index = 1U; index < window->count; index++) {
        const int32_t value = window->samples[index];
        if (value < minimum) {
            minimum = value;
        }
        if (value > maximum) {
            maximum = value;
        }
    }
    /* En 64 bits para que dos extremos opuestos del convertidor no desborden. */
    return (uint32_t)((int64_t)maximum - (int64_t)minimum);
}

int32_t scale_stability_average(const scale_stability_t *window)
{
    if (window == NULL || window->count == 0U) {
        return 0;
    }
    int64_t total = 0;
    for (uint32_t index = 0U; index < window->count; index++) {
        total += (int64_t)window->samples[index];
    }
    return (int32_t)(total / (int64_t)window->count);
}

bool scale_stability_push(scale_stability_t *window, int32_t counts, uint32_t tolerance_counts)
{
    if (window == NULL) {
        return false;
    }
    window->samples[window->index] = counts;
    window->index = (window->index + 1U) % SCALE_STABILITY_SAMPLES;
    if (window->count < SCALE_STABILITY_SAMPLES) {
        window->count++;
    }

    /* Con la ventana a medias no se afirma que nada esté quieto. */
    if (window->count < SCALE_STABILITY_SAMPLES) {
        return false;
    }
    return scale_stability_spread(window) <= tolerance_counts;
}
