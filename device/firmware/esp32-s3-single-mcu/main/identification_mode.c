#include "identification_mode.h"

#include <stddef.h>

static uint32_t saturating_add_u32(uint32_t value, uint32_t increment)
{
    return UINT32_MAX - value < increment ? UINT32_MAX : value + increment;
}

identification_setup_t identification_mode_configure(identification_mode_t *mode,
                                                     const stimulus_sequence_t *sequence,
                                                     float maximum_percent,
                                                     bool target_commissioned)
{
    if (mode == NULL) {
        return IDENTIFICATION_SETUP_NO_SEQUENCE;
    }
    *mode = (identification_mode_t){.phase = IDENTIFICATION_DISABLED};

    if (sequence == NULL) {
        return IDENTIFICATION_SETUP_NO_SEQUENCE;
    }
    if (!stimulus_sequence_is_valid(sequence)) {
        return IDENTIFICATION_SETUP_INVALID_SEQUENCE;
    }
    /* Mismo criterio que el guardián: lo que no se entiende no es un tope. */
    if (!(maximum_percent > 0.0F && maximum_percent <= 100.0F)) {
        return IDENTIFICATION_SETUP_INVALID_LIMIT;
    }
    if ((float)stimulus_sequence_peak_percent(sequence) > maximum_percent) {
        return IDENTIFICATION_SETUP_EXCEEDS_LIMIT;
    }
    /* Un lazo ya puesto en servicio se ensaya con su acta, no a lazo abierto:
     * el modo restringido existe solo para llegar hasta ella. */
    if (target_commissioned) {
        return IDENTIFICATION_SETUP_ALREADY_COMMISSIONED;
    }

    mode->sequence = sequence;
    mode->maximum_percent = maximum_percent;
    mode->phase = IDENTIFICATION_AWAITING_CONFIRMATION;
    return IDENTIFICATION_SETUP_ACCEPTED;
}

bool identification_mode_observe_panel(identification_mode_t *mode,
                                       operational_state_t state,
                                       bool brew_pressed,
                                       bool tare_pressed,
                                       uint32_t elapsed_ms)
{
    if (mode == NULL || mode->phase != IDENTIFICATION_AWAITING_CONFIRMATION) {
        return false;
    }
    if (state != OP_STATE_SAFE_INACTIVE) {
        mode->phase = IDENTIFICATION_DECLINED;
        return false;
    }

    mode->window_ms = saturating_add_u32(mode->window_ms, elapsed_ms);
    if (!brew_pressed && !tare_pressed) {
        mode->released_seen = true;
        mode->hold_ms = 0U;
    } else if (brew_pressed && tare_pressed && mode->released_seen) {
        mode->hold_ms = saturating_add_u32(mode->hold_ms, elapsed_ms);
        if (mode->hold_ms >= IDENTIFICATION_CONFIRMATION_HOLD_MS) {
            mode->phase = IDENTIFICATION_ARMED;
            return true;
        }
    } else {
        /* Uno solo, o los dos sin haberlos visto sueltos: la cuenta empieza de
         * nuevo. Soltar a medias no se tolera. */
        mode->hold_ms = 0U;
    }

    if (mode->window_ms >= IDENTIFICATION_CONFIRMATION_WINDOW_MS) {
        mode->phase = IDENTIFICATION_DECLINED;
    }
    return false;
}

identification_request_t identification_mode_request(const identification_mode_t *mode)
{
    identification_request_t request = {0};
    if (mode == NULL || mode->sequence == NULL ||
        (mode->phase != IDENTIFICATION_ARMED && mode->phase != IDENTIFICATION_RUNNING)) {
        return request;
    }
    const uint32_t elapsed = mode->phase == IDENTIFICATION_RUNNING ? mode->running_ms : 0U;
    request.target = mode->sequence->target;
    request.demand_percent = (float)stimulus_sequence_demand_at(mode->sequence, elapsed);
    request.maximum_percent = mode->maximum_percent;
    return request;
}

identification_event_t identification_mode_update(identification_mode_t *mode,
                                                  actuation_verdict_t verdict,
                                                  uint32_t elapsed_ms)
{
    if (mode == NULL || mode->sequence == NULL) {
        return IDENTIFICATION_EVENT_NONE;
    }

    identification_event_t event = IDENTIFICATION_EVENT_NONE;
    if (mode->phase == IDENTIFICATION_ARMED) {
        if (verdict == ACTUATION_INHIBITED_STIMULUS_LIMIT) {
            /* El guardián rechaza el primer escalón: la declaración es
             * incoherente y esperar no la arreglará. */
            mode->phase = IDENTIFICATION_ABORTED;
            mode->stop_verdict = verdict;
            return IDENTIFICATION_EVENT_ABORTED;
        }
        if (verdict != ACTUATION_ALLOWED_IDENTIFICATION) {
            /* Esperar a que el estado operativo autorice es lo normal: el
             * operador enciende o abre la extracción cuando está listo. */
            return IDENTIFICATION_EVENT_NONE;
        }
        mode->phase = IDENTIFICATION_RUNNING;
        mode->running_ms = 0U;
        event = IDENTIFICATION_EVENT_STARTED;
    } else if (mode->phase != IDENTIFICATION_RUNNING) {
        return IDENTIFICATION_EVENT_NONE;
    } else if (verdict != ACTUATION_ALLOWED_IDENTIFICATION) {
        mode->phase = IDENTIFICATION_ABORTED;
        mode->stop_verdict = verdict;
        return IDENTIFICATION_EVENT_ABORTED;
    }

    /* La demanda que el guardián acaba de admitir se aplica durante este
     * periodo; solo después cuenta como secuencia recorrida. */
    mode->running_ms = saturating_add_u32(mode->running_ms, elapsed_ms);
    if (mode->running_ms >= stimulus_sequence_duration_ms(mode->sequence)) {
        mode->phase = IDENTIFICATION_FINISHED;
        /* Una secuencia más corta que un periodo empezaría y acabaría a la vez.
         * Prevalece el inicio, que es el que fecha el flanco; el final lo dice
         * la fase. Las secuencias declaradas duran segundos. */
        return event == IDENTIFICATION_EVENT_STARTED ? IDENTIFICATION_EVENT_STARTED
                                                     : IDENTIFICATION_EVENT_FINISHED;
    }
    return event;
}

bool identification_mode_rearm(identification_mode_t *mode,
                               const stimulus_sequence_t *sequence)
{
    if (mode == NULL || mode->sequence == NULL || sequence == NULL ||
        mode->phase != IDENTIFICATION_FINISHED || !stimulus_sequence_is_valid(sequence) ||
        sequence->target != mode->sequence->target ||
        (float)stimulus_sequence_peak_percent(sequence) > mode->maximum_percent) {
        return false;
    }
    mode->sequence = sequence;
    mode->running_ms = 0U;
    mode->stop_verdict = ACTUATION_UNDECIDED;
    mode->phase = IDENTIFICATION_ARMED;
    return true;
}

const char *identification_phase_name(identification_phase_t phase)
{
    switch (phase) {
    case IDENTIFICATION_AWAITING_CONFIRMATION:
        return "awaiting-confirmation";
    case IDENTIFICATION_DECLINED:
        return "declined";
    case IDENTIFICATION_ARMED:
        return "armed";
    case IDENTIFICATION_RUNNING:
        return "running";
    case IDENTIFICATION_FINISHED:
        return "finished";
    case IDENTIFICATION_ABORTED:
        return "aborted";
    case IDENTIFICATION_DISABLED:
    default:
        return "disabled";
    }
}

const char *identification_setup_name(identification_setup_t setup)
{
    switch (setup) {
    case IDENTIFICATION_SETUP_ACCEPTED:
        return "accepted";
    case IDENTIFICATION_SETUP_INVALID_SEQUENCE:
        return "invalid-sequence";
    case IDENTIFICATION_SETUP_INVALID_LIMIT:
        return "invalid-limit";
    case IDENTIFICATION_SETUP_EXCEEDS_LIMIT:
        return "exceeds-limit";
    case IDENTIFICATION_SETUP_ALREADY_COMMISSIONED:
        return "already-commissioned";
    case IDENTIFICATION_SETUP_NO_SEQUENCE:
    default:
        return "no-sequence";
    }
}
