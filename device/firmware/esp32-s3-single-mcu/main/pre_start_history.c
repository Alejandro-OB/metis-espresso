#include "pre_start_history.h"

#include <stddef.h>

/*
 * `a` es igual o posterior a `b` en el reloj modular. Restar en `uint32_t` y
 * mirar el bit alto evita convertir a entero con signo, que en C17 depende de
 * la implementación cuando el valor no cabe.
 */
static bool at_or_after(uint32_t a, uint32_t b)
{
    return (uint32_t)(a - b) < UINT32_C(0x80000000);
}

void pre_start_history_reset(pre_start_history_t *history)
{
    if (history == NULL) {
        return;
    }
    *history = (pre_start_history_t){0};
}

bool pre_start_history_observe(pre_start_history_t *history,
                               uint32_t now_ms,
                               const pre_start_observation_t *observation)
{
    if (history == NULL || observation == NULL) {
        return false;
    }

    /* Una racha rota no se remienda: lo anterior al hueco no es «minuto previo
     * con la máquina lista», y otro origen no es la misma serie. Un instante
     * anterior al último observado también rompe la racha, porque la diferencia
     * modular sale enorme: no se intenta interpretar un reloj que retrocede. */
    if (history->count > 0U &&
        ((uint32_t)(now_ms - history->last_observed_ms) > PRE_START_HISTORY_MAX_GAP_MS ||
         observation->measured != history->measured)) {
        pre_start_history_reset(history);
    }

    const bool first = history->count == 0U;
    if (!first) {
        history->last_observed_ms = now_ms;
        if (!at_or_after(now_ms, history->next_target_ms)) {
            return false;
        }
    }

    uint32_t slot;
    if (history->count < PRE_START_HISTORY_CAPACITY) {
        slot = (history->oldest + history->count) % PRE_START_HISTORY_CAPACITY;
        history->count++;
    } else {
        /* Lleno: la más antigua ya cae fuera del minuto y deja su sitio. */
        slot = history->oldest;
        history->oldest = (history->oldest + 1U) % PRE_START_HISTORY_CAPACITY;
    }
    history->entries[slot] = (pre_start_entry_t){
        .at_ms = now_ms,
        .temperature_c = observation->temperature_c,
        .pressure_bar = observation->pressure_bar,
        .temperature_valid = observation->temperature_valid,
        .pressure_valid = observation->pressure_valid,
    };

    if (first) {
        history->measured = observation->measured;
        history->last_observed_ms = now_ms;
        history->next_target_ms = now_ms;
    }
    /* El objetivo avanza hasta superar el instante conservado, como en
     * `series_message`: una vuelta tardía no provoca dos muestras seguidas. El
     * hueco máximo acota este bucle a pocas vueltas. */
    do {
        history->next_target_ms += PRE_START_HISTORY_INTERVAL_MS;
    } while (at_or_after(now_ms, history->next_target_ms));
    return true;
}

uint32_t pre_start_history_freeze(const pre_start_history_t *history,
                                  uint32_t press_ms,
                                  bool measured,
                                  pre_start_sample_t *out,
                                  uint32_t capacity)
{
    if (history == NULL || out == NULL || capacity == 0U || history->count == 0U ||
        history->measured != measured) {
        return 0U;
    }

    uint32_t written = 0U;
    const uint32_t count =
        history->count < PRE_START_HISTORY_CAPACITY ? history->count : PRE_START_HISTORY_CAPACITY;
    for (uint32_t index = 0U; index < count && written < capacity; ++index) {
        const pre_start_entry_t *entry =
            &history->entries[(history->oldest + index) % PRE_START_HISTORY_CAPACITY];
        /* Cuánto antes de pulsar se tomó. Cero o «después» (la resta modular
         * da un valor enorme) quedan fuera, igual que lo anterior al minuto. */
        const uint32_t before_ms = press_ms - entry->at_ms;
        if (before_ms == 0U || before_ms > PRE_START_HISTORY_WINDOW_MS) {
            continue;
        }
        out[written++] = (pre_start_sample_t){
            .elapsed_ms = -(int32_t)before_ms,
            .temperature_c = entry->temperature_c,
            .pressure_bar = entry->pressure_bar,
            .temperature_valid = entry->temperature_valid,
            .pressure_valid = entry->pressure_valid,
        };
    }
    return written;
}
