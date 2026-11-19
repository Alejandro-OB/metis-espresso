#include "open_extraction.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool copy_text(char *destination, size_t size, const char *source)
{
    if (source == NULL) {
        destination[0] = '\0';
        return true;
    }
    const int written = snprintf(destination, size, "%s", source);
    return written >= 0 && (size_t)written < size;
}

static bool terminated(const char *text, size_t size)
{
    return memchr(text, '\0', size) != NULL;
}

bool open_extraction_mark(open_extraction_marker_t *marker,
                          const open_extraction_opening_t *opening)
{
    if (marker == NULL) {
        return false;
    }
    *marker = (open_extraction_marker_t){0};
    if (opening == NULL || opening->session_id == NULL || opening->session_id[0] == '\0' ||
        opening->profile == NULL) {
        return false;
    }
    open_extraction_marker_t candidate = {
        .format_version = OPEN_EXTRACTION_FORMAT_VERSION,
        .profile = *opening->profile,
        .started_epoch_ms = opening->started_epoch_ms,
        .source = opening->source,
        .calibration_version = opening->calibration_version,
    };
    if (!copy_text(candidate.session_id, sizeof(candidate.session_id), opening->session_id) ||
        !copy_text(candidate.calibration_id, sizeof(candidate.calibration_id),
                   opening->calibration_id) ||
        !copy_text(candidate.firmware_version, sizeof(candidate.firmware_version),
                   opening->firmware_version)) {
        return false;
    }
    *marker = candidate;
    return true;
}

bool open_extraction_is_valid(const open_extraction_marker_t *marker)
{
    if (marker == NULL || marker->format_version != OPEN_EXTRACTION_FORMAT_VERSION) {
        return false;
    }
    return terminated(marker->session_id, sizeof(marker->session_id)) &&
           marker->session_id[0] != '\0' &&
           terminated(marker->profile.id, sizeof(marker->profile.id)) &&
           terminated(marker->profile.name, sizeof(marker->profile.name)) &&
           terminated(marker->calibration_id, sizeof(marker->calibration_id)) &&
           terminated(marker->firmware_version, sizeof(marker->firmware_version)) &&
           marker->source <= OPEN_EXTRACTION_SOURCE_SIMULATED;
}

bool open_extraction_recover(const open_extraction_marker_t *marker,
                             extraction_summary_t *summary)
{
    if (summary == NULL) {
        return false;
    }
    *summary = (extraction_summary_t){0};
    if (!open_extraction_is_valid(marker)) {
        return false;
    }
    (void)snprintf(summary->id, sizeof(summary->id), "%s", marker->session_id);
    summary->effective_profile = marker->profile;
    summary->termination = OP_TERMINATION_UNEXPECTED_POWER_LOSS;
    summary->window_status = EXTRACTION_WINDOW_UNDETERMINED;
    /* Cerrada para la cola, no medida hasta el final: la causa lo dice. */
    summary->complete = true;
    return true;
}
