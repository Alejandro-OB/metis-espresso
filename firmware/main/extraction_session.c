#include "extraction_session.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

bool extraction_session_start(extraction_session_t *session,
                              const char *session_id,
                              const extraction_profile_t *profile,
                              uint64_t started_monotonic_ms)
{
    if (session == NULL || session_id == NULL || session_id[0] == '\0' ||
        profile == NULL || strlen(session_id) >= EXTRACTION_SESSION_ID_LENGTH) {
        return false;
    }

    *session = (extraction_session_t) {0};
    const int written = snprintf(session->summary.id,
                                 sizeof(session->summary.id), "%s", session_id);
    if (written < 0 || (size_t)written >= sizeof(session->summary.id)) {
        return false;
    }
    session->summary.effective_profile = *profile;
    session->summary.started_monotonic_ms = started_monotonic_ms;
    session->summary.termination = OP_TERMINATION_NONE;
    session->active = true;
    return true;
}

bool extraction_session_note_boiler_offset(extraction_session_t *session, float offset_c)
{
    if (session == NULL || !session->active || !isfinite(offset_c) || offset_c < 0.0F) {
        return false;
    }
    session->summary.boiler_offset_known = true;
    session->summary.boiler_offset_c = offset_c;
    return true;
}

bool extraction_session_note_feedforward_gain(extraction_session_t *session, float gain)
{
    if (session == NULL || !session->active || !isfinite(gain) || gain < 0.0F) {
        return false;
    }
    session->summary.feedforward_gain_known = true;
    session->summary.feedforward_gain = gain;
    return true;
}

bool extraction_session_record(extraction_session_t *session,
                               const extraction_sample_t *sample)
{
    if (session == NULL || sample == NULL || !session->active) {
        return false;
    }
    if (session->summary.sample_count >= EXTRACTION_MAX_SAMPLES) {
        session->summary.dropped_samples++;
        return false;
    }

    session->samples[session->summary.sample_count++] = *sample;
    if (!session->window_open) {
        const bool pressure_in_range =
            sample->pressure_valid &&
            fabs((double)sample->observed_pressure_bar -
                 (double)sample->target_pressure_bar) <=
                (double)session->summary.effective_profile.pressure_tolerance_bar;
        if (!pressure_in_range) {
            /* La muestra queda en la serie; solo no cuenta para la estabilidad. */
            return true;
        }
        session->window_open = true;
        session->summary.window_start_ms = sample->elapsed_ms;
    }
    if (sample->temperature_valid) {
        const double temperature_error = fabs(
            (double)sample->observed_temperature_c -
            (double)sample->target_temperature_c);
        session->accumulated_temperature_error += temperature_error;
        session->valid_temperature_samples++;
        if (temperature_error <=
            (double)session->summary.effective_profile.temperature_tolerance_c) {
            session->temperature_samples_within_tolerance++;
        }
    }
    if (sample->pressure_valid) {
        const double pressure_error = fabs(
            (double)sample->observed_pressure_bar -
            (double)sample->target_pressure_bar);
        session->accumulated_pressure_error += pressure_error;
        session->valid_pressure_samples++;
        if (pressure_error <=
            (double)session->summary.effective_profile.pressure_tolerance_bar) {
            session->pressure_samples_within_tolerance++;
        }
    }
    return true;
}

uint32_t extraction_session_take_pre_start(extraction_session_t *session,
                                           const pre_start_history_t *history,
                                           bool measured)
{
    /* Después de la primera muestra ya no es «al pulsar»: se rechaza en lugar
     * de anteponer una historia tomada con otro origen de tiempo. */
    if (session == NULL || history == NULL || !session->active ||
        session->summary.sample_count != 0U) {
        return 0U;
    }
    /* El anillo trabaja con el reloj monótono truncado a 32 bits; truncar aquí
     * del mismo modo mantiene exacta la diferencia entre los dos. */
    session->pre_start_count = pre_start_history_freeze(
        history, (uint32_t)session->summary.started_monotonic_ms, measured,
        session->pre_start, PRE_START_HISTORY_CAPACITY);
    return session->pre_start_count;
}

bool extraction_session_finish(extraction_session_t *session,
                               extraction_termination_t termination,
                               uint64_t finished_monotonic_ms)
{
    if (session == NULL || !session->active ||
        termination == OP_TERMINATION_NONE ||
        finished_monotonic_ms < session->summary.started_monotonic_ms) {
        return false;
    }

    session->summary.finished_monotonic_ms = finished_monotonic_ms;
    const uint64_t duration = finished_monotonic_ms -
                              session->summary.started_monotonic_ms;
    session->summary.duration_ms = duration > UINT32_MAX
                                       ? UINT32_MAX
                                       : (uint32_t)duration;
    session->summary.termination = termination;
    session->summary.window_status = session->window_open
                                         ? EXTRACTION_WINDOW_STABILIZED
                                         : EXTRACTION_WINDOW_NEVER_STABILIZED;
    session->summary.temperature_samples_in_window = session->valid_temperature_samples;
    session->summary.pressure_samples_in_window = session->valid_pressure_samples;
    if (session->valid_temperature_samples > 0U) {
        session->summary.mean_absolute_temperature_error_c =
            (float)(session->accumulated_temperature_error /
                    (double)session->valid_temperature_samples);
        session->summary.temperature_within_tolerance_percent =
            100.0F * (float)session->temperature_samples_within_tolerance /
            (float)session->valid_temperature_samples;
    }
    if (session->valid_pressure_samples > 0U) {
        session->summary.mean_absolute_pressure_error_bar =
            (float)(session->accumulated_pressure_error /
                    (double)session->valid_pressure_samples);
        session->summary.pressure_within_tolerance_percent =
            100.0F * (float)session->pressure_samples_within_tolerance /
            (float)session->valid_pressure_samples;
    }
    session->summary.complete = true;
    session->active = false;
    return true;
}
