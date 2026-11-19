#include "extraction_message.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *termination_name(extraction_termination_t termination)
{
    switch (termination) {
    case OP_TERMINATION_MANUAL_NORMAL_STOP:
        return "manual-normal-stop";
    case OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED:
        return "maximum-extraction-time-reached";
    case OP_TERMINATION_SAFETY_PROTECTION_TRIP:
        return "safety-protection-trip";
    case OP_TERMINATION_UNEXPECTED_POWER_LOSS:
        return "unexpected-power-loss";
    default:
        return NULL;
    }
}

/*
 * Conversión propia de epoch a calendario civil UTC.
 *
 * Se evita `gmtime_r`, que es POSIX y no está disponible bajo C17 estricto, y
 * con ello también cualquier dependencia de la zona horaria o del estado de la
 * biblioteca estándar del dispositivo. El algoritmo es el de días civiles
 * proleptico gregoriano, exacto para todo el rango representable.
 */
static void civil_from_epoch_days(int64_t days, int *year, unsigned *month, unsigned *day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t day_of_era = days - era * 146097;
    const int64_t year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const int64_t civil_year = year_of_era + era * 400;
    const int64_t day_of_year =
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const int64_t shifted_month = (5 * day_of_year + 2) / 153;
    *day = (unsigned)(day_of_year - (153 * shifted_month + 2) / 5 + 1);
    *month = (unsigned)(shifted_month < 10 ? shifted_month + 3 : shifted_month - 9);
    *year = (int)(civil_year + (*month <= 2 ? 1 : 0));
}

/* Escribe un instante UTC con milisegundos, como exige el formato date-time. */
bool extraction_message_format_instant(uint64_t epoch_ms, char *buffer, size_t buffer_size)
{
    const uint64_t total_seconds = epoch_ms / 1000U;
    const unsigned milliseconds = (unsigned)(epoch_ms % 1000U);
    const uint64_t seconds_of_day = total_seconds % 86400U;
    const int64_t days = (int64_t)(total_seconds / 86400U);
    int year = 0;
    unsigned month = 0U;
    unsigned day = 0U;
    civil_from_epoch_days(days, &year, &month, &day);
    const int written = snprintf(buffer, buffer_size,
                                 "%04d-%02u-%02uT%02u:%02u:%02u.%03uZ",
                                 year, month, day,
                                 (unsigned)(seconds_of_day / 3600U),
                                 (unsigned)((seconds_of_day % 3600U) / 60U),
                                 (unsigned)(seconds_of_day % 60U), milliseconds);
    return written > 0 && (size_t)written < buffer_size;
}


/*
 * Un indicador sin muestras en la ventana se envía como null: el 0 que deja la
 * estructura puesta a cero afirmaría una preparación perfecta que nadie midió.
 */
static void format_indicator(char *buffer, size_t buffer_size, uint32_t samples,
                             const char *format, float value)
{
    if (samples == 0U) {
        snprintf(buffer, buffer_size, "null");
    } else {
        snprintf(buffer, buffer_size, format, (double)value);
    }
}

static const char *window_status_name(extraction_window_status_t status)
{
    switch (status) {
    case EXTRACTION_WINDOW_STABILIZED:
        return "stabilized";
    case EXTRACTION_WINDOW_NEVER_STABILIZED:
        return "never-stabilized";
    case EXTRACTION_WINDOW_UNDETERMINED:
    default:
        return "undetermined";
    }
}

static bool finite_values(const extraction_summary_t *summary)
{
    return isfinite(summary->mean_absolute_temperature_error_c) &&
           isfinite(summary->mean_absolute_pressure_error_bar) &&
           isfinite(summary->temperature_within_tolerance_percent) &&
           isfinite(summary->pressure_within_tolerance_percent) &&
           isfinite(summary->effective_profile.temperature_setpoint_c) &&
           isfinite(summary->effective_profile.temperature_tolerance_c) &&
           isfinite(summary->effective_profile.pressure_setpoint_bar) &&
           isfinite(summary->effective_profile.pressure_tolerance_bar);
}

extraction_message_status_t extraction_message_identifier(
    const extraction_summary_t *summary, char *buffer, size_t buffer_size)
{
    if (summary == NULL || buffer == NULL || buffer_size == 0U) {
        return EXTRACTION_MESSAGE_INVALID_ARGUMENT;
    }
    if (summary->id[0] == '\0') {
        return EXTRACTION_MESSAGE_INVALID_ARGUMENT;
    }
    const int written = snprintf(buffer, buffer_size, "extraction-%s", summary->id);
    if (written <= 0 || (size_t)written >= buffer_size) {
        return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
    }
    return EXTRACTION_MESSAGE_OK;
}

extraction_message_status_t extraction_message_build(
    const extraction_summary_t *summary,
    const extraction_message_context_t *context,
    char *buffer,
    size_t buffer_size,
    size_t *written)
{
    if (summary == NULL || context == NULL || buffer == NULL || buffer_size == 0U ||
        context->device_id == NULL || context->device_id[0] == '\0') {
        return EXTRACTION_MESSAGE_INVALID_ARGUMENT;
    }
    if (!summary->complete) {
        return EXTRACTION_MESSAGE_SESSION_INCOMPLETE;
    }
    const char *termination = termination_name(summary->termination);
    if (termination == NULL) {
        return EXTRACTION_MESSAGE_SESSION_INCOMPLETE;
    }
    /* Sin hora de pared fiable no se inventa una fecha: el registro espera. */
    if (context->epoch_ms_at_finish < EXTRACTION_MESSAGE_MINIMUM_EPOCH_MS) {
        return EXTRACTION_MESSAGE_TIME_BASE_UNAVAILABLE;
    }
    if (!finite_values(summary)) {
        return EXTRACTION_MESSAGE_VALUE_NOT_FINITE;
    }

    char message_id[EXTRACTION_MESSAGE_ID_LENGTH + 1U];
    const extraction_message_status_t identifier_status =
        extraction_message_identifier(summary, message_id, sizeof(message_id));
    if (identifier_status != EXTRACTION_MESSAGE_OK) {
        return identifier_status;
    }

    const uint64_t finished_epoch_ms = context->epoch_ms_at_finish;
    char occurred_at[32];
    if (!extraction_message_format_instant(finished_epoch_ms, occurred_at, sizeof(occurred_at))) {
        return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
    }
    /* Con comillas, porque un registro cortado envía null en su lugar. */
    char started_at[40];
    char finished_at[40];
    char duration[16];
    if (summary->termination == OP_TERMINATION_UNEXPECTED_POWER_LOSS) {
        /* No consta cuándo se cortó la energía: ni fin ni duración. El
         * comienzo, solo si había hora al abrir. */
        snprintf(finished_at, sizeof(finished_at), "null");
        snprintf(duration, sizeof(duration), "null");
        if (context->epoch_ms_at_start >= EXTRACTION_MESSAGE_MINIMUM_EPOCH_MS) {
            char instant[32];
            if (!extraction_message_format_instant(context->epoch_ms_at_start, instant,
                                                   sizeof(instant))) {
                return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
            }
            snprintf(started_at, sizeof(started_at), "\"%s\"", instant);
        } else {
            snprintf(started_at, sizeof(started_at), "null");
        }
    } else {
        const uint64_t started_epoch_ms = finished_epoch_ms >= summary->duration_ms
                                              ? finished_epoch_ms - summary->duration_ms
                                              : finished_epoch_ms;
        char instant[32];
        if (!extraction_message_format_instant(started_epoch_ms, instant, sizeof(instant))) {
            return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
        }
        snprintf(started_at, sizeof(started_at), "\"%s\"", instant);
        snprintf(finished_at, sizeof(finished_at), "\"%s\"", occurred_at);
        snprintf(duration, sizeof(duration), "%" PRIu32, summary->duration_ms);
    }

    const extraction_profile_t *profile = &summary->effective_profile;
    char barista[96];
    if (context->barista != NULL && context->barista[0] != '\0') {
        const int barista_written =
            snprintf(barista, sizeof(barista), "\"%s\"", context->barista);
        if (barista_written <= 0 || (size_t)barista_written >= sizeof(barista)) {
            return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
        }
    } else {
        snprintf(barista, sizeof(barista), "null");
    }
    char expected_mass[32];
    if (profile->expected_beverage_mass_defined &&
        isfinite(profile->expected_beverage_mass_g)) {
        snprintf(expected_mass, sizeof(expected_mass), "%.2f",
                 (double)profile->expected_beverage_mass_g);
    } else {
        snprintf(expected_mass, sizeof(expected_mass), "null");
    }

    char calibration[96];
    if (context->calibration_id != NULL && context->calibration_id[0] != '\0') {
        const int calibration_written =
            snprintf(calibration, sizeof(calibration), "{\"id\":\"%s\",\"version\":%" PRIu32 "}",
                     context->calibration_id, context->calibration_version);
        if (calibration_written <= 0 || (size_t)calibration_written >= sizeof(calibration)) {
            return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
        }
    } else {
        snprintf(calibration, sizeof(calibration), "null");
    }
    char firmware[64];
    if (context->firmware_version != NULL && context->firmware_version[0] != '\0') {
        const int firmware_written =
            snprintf(firmware, sizeof(firmware), "\"%s\"", context->firmware_version);
        if (firmware_written <= 0 || (size_t)firmware_written >= sizeof(firmware)) {
            return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
        }
    } else {
        snprintf(firmware, sizeof(firmware), "null");
    }

    char water_reference[192];
    if (context->temperature_plant != NULL && context->temperature_plant[0] != '\0' &&
        water_reference_is_valid(&context->water_reference)) {
        const water_reference_t *line = &context->water_reference;
        const int line_written = snprintf(
            water_reference, sizeof(water_reference),
            "{\"plant\":\"%s\",\"gain\":%.4f,\"offsetC\":%.3f"
            ",\"boilerMinimumC\":%.2f,\"boilerMaximumC\":%.2f}",
            context->temperature_plant, (double)line->gain, (double)line->offset_c,
            (double)line->boiler_minimum_c, (double)line->boiler_maximum_c);
        if (line_written <= 0 || (size_t)line_written >= sizeof(water_reference)) {
            return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
        }
    } else {
        snprintf(water_reference, sizeof(water_reference), "null");
    }

    /* Opcional en el contrato: sin constancia no se envía, en vez de un cero. */
    char boiler_offset[48];
    if (summary->boiler_offset_known && isfinite(summary->boiler_offset_c) &&
        summary->boiler_offset_c >= 0.0F) {
        const int offset_written =
            snprintf(boiler_offset, sizeof(boiler_offset), ",\"boilerOffsetAppliedC\":%.2f",
                     (double)summary->boiler_offset_c);
        if (offset_written <= 0 || (size_t)offset_written >= sizeof(boiler_offset)) {
            return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
        }
    } else {
        boiler_offset[0] = '\0';
    }

    char feedforward_gain[56];
    if (summary->feedforward_gain_known && isfinite(summary->feedforward_gain) &&
        summary->feedforward_gain >= 0.0F) {
        const int gain_written = snprintf(feedforward_gain, sizeof(feedforward_gain),
                                          ",\"firstUseFeedforwardGainApplied\":%.3f",
                                          (double)summary->feedforward_gain);
        if (gain_written <= 0 || (size_t)gain_written >= sizeof(feedforward_gain)) {
            return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
        }
    } else {
        feedforward_gain[0] = '\0';
    }

    char temperature_mae[24];
    char pressure_mae[24];
    char temperature_within[24];
    char pressure_within[24];
    format_indicator(temperature_mae, sizeof(temperature_mae),
                     summary->temperature_samples_in_window, "%.3f",
                     summary->mean_absolute_temperature_error_c);
    format_indicator(pressure_mae, sizeof(pressure_mae),
                     summary->pressure_samples_in_window, "%.3f",
                     summary->mean_absolute_pressure_error_bar);
    format_indicator(temperature_within, sizeof(temperature_within),
                     summary->temperature_samples_in_window, "%.2f",
                     summary->temperature_within_tolerance_percent);
    format_indicator(pressure_within, sizeof(pressure_within),
                     summary->pressure_samples_in_window, "%.2f",
                     summary->pressure_within_tolerance_percent);
    char window_start[16];
    if (summary->window_status == EXTRACTION_WINDOW_STABILIZED) {
        snprintf(window_start, sizeof(window_start), "%" PRIu32, summary->window_start_ms);
    } else {
        snprintf(window_start, sizeof(window_start), "null");
    }

    const int result = snprintf(
        buffer, buffer_size,
        "{\"schemaVersion\":\"1.0\""
        ",\"messageId\":\"%s\""
        ",\"messageType\":\"extraction.completed\""
        ",\"deviceId\":\"%s\""
        ",\"sequence\":%" PRIu32
        ",\"occurredAt\":\"%s\""
        ",\"payload\":{"
        "\"id\":\"%s\""
        ",\"source\":\"%s\""
        ",\"coffeeReference\":null"
        ",\"barista\":%s"
        ",\"profile\":{"
        "\"id\":\"%s\",\"name\":\"%s\",\"definedBy\":null,\"version\":%" PRIu32
        ",\"temperatureSetpointC\":%.2f,\"temperatureToleranceC\":%.2f"
        ",\"pressureSetpointBar\":%.2f,\"pressureToleranceBar\":%.2f"
        ",\"maximumTimeMs\":%" PRIu32 ",\"expectedBeverageMassG\":%s"
        ",\"status\":\"effective\"}"
        ",\"startedAt\":%s"
        ",\"finishedAt\":%s"
        ",\"durationMs\":%s"
        ",\"termination\":\"%s\""
        ",\"sampleCount\":%" PRIu32
        ",\"seriesRef\":null"
        ",\"calibration\":%s"
        ",\"firmwareVersion\":%s"
        ",\"waterReference\":%s"
        "%s"
        "%s"
        ",\"statistics\":{"
        "\"temperatureMaeC\":%s,\"pressureMaeBar\":%s"
        ",\"temperatureWithinTolerancePercent\":%s"
        ",\"pressureWithinTolerancePercent\":%s"
        /* El firmware mide el cuerpo de la caldera; la del agua exige ADR-019. */
        ",\"window\":{\"rule\":\"" EXTRACTION_STABILITY_WINDOW_RULE "\""
        ",\"status\":\"%s\",\"startMs\":%s}"
        ",\"temperatureSite\":\"boiler\"}"
        "}}",
        message_id, context->device_id, context->sequence, occurred_at,
        summary->id, context->measured ? "measured" : "simulation", barista,
        profile->id, profile->name, profile->version,
        (double)profile->temperature_setpoint_c, (double)profile->temperature_tolerance_c,
        (double)profile->pressure_setpoint_bar, (double)profile->pressure_tolerance_bar,
        profile->maximum_time_ms, expected_mass,
        started_at, finished_at, duration, termination, summary->sample_count,
        calibration, firmware, water_reference, boiler_offset, feedforward_gain,
        temperature_mae, pressure_mae, temperature_within, pressure_within,
        window_status_name(summary->window_status), window_start);

    if (result <= 0 || (size_t)result >= buffer_size) {
        return EXTRACTION_MESSAGE_BUFFER_TOO_SMALL;
    }
    if (written != NULL) {
        *written = (size_t)result;
    }
    return EXTRACTION_MESSAGE_OK;
}

const char *extraction_message_status_name(extraction_message_status_t status)
{
    switch (status) {
    case EXTRACTION_MESSAGE_OK:
        return "ok";
    case EXTRACTION_MESSAGE_INVALID_ARGUMENT:
        return "argumento inválido";
    case EXTRACTION_MESSAGE_SESSION_INCOMPLETE:
        return "sesión incompleta";
    case EXTRACTION_MESSAGE_TIME_BASE_UNAVAILABLE:
        return "sin hora de pared fiable";
    case EXTRACTION_MESSAGE_VALUE_NOT_FINITE:
        return "valor no finito";
    case EXTRACTION_MESSAGE_BUFFER_TOO_SMALL:
        return "búfer insuficiente";
    default:
        return "desconocido";
    }
}
