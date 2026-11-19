#include "telemetry_message.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "extraction_message.h"

/*
 * Nombre de la procedencia según el contrato. El vocabulario del firmware
 * distingue tres casos y el contrato solo admite dos: una muestra sin origen
 * no se emite, en lugar de elegir uno por omisión.
 */
static const char *source_name(process_source_t source)
{
    switch (source) {
    case PROCESS_SOURCE_MEASURED:
        return "measured";
    case PROCESS_SOURCE_SIMULATED:
        return "simulation";
    case PROCESS_SOURCE_NONE:
    default:
        return NULL;
    }
}

/*
 * Calidad de un canal según el contrato. Una lectura simulada se declara como
 * tal aunque sea internamente consistente: llamarla válida la haría
 * indistinguible de una medición.
 */
static const char *channel_quality_name(process_source_t source, signal_quality_t quality)
{
    if (source == PROCESS_SOURCE_SIMULATED) {
        return "simulated";
    }
    return quality == SIGNAL_VALID ? "valid" : "invalid";
}

/* Un valor solo se transmite si la lectura es utilizable; si no, viaja nulo. */
static void format_value(float value, const char *quality, char *buffer, size_t buffer_size)
{
    const bool usable = strcmp(quality, "invalid") != 0 && isfinite(value);
    if (usable) {
        snprintf(buffer, buffer_size, "%.3f", (double)value);
    } else {
        snprintf(buffer, buffer_size, "null");
    }
}

const char *telemetry_message_status_name(telemetry_message_status_t status)
{
    switch (status) {
    case TELEMETRY_MESSAGE_OK:
        return "ok";
    case TELEMETRY_MESSAGE_INVALID_ARGUMENT:
        return "invalid-argument";
    case TELEMETRY_MESSAGE_BUFFER_TOO_SMALL:
        return "buffer-too-small";
    case TELEMETRY_MESSAGE_SOURCE_UNAVAILABLE:
        return "source-unavailable";
    case TELEMETRY_MESSAGE_TIME_BASE_UNAVAILABLE:
        return "time-base-unavailable";
    case TELEMETRY_MESSAGE_VALUE_NOT_FINITE:
        return "value-not-finite";
    default:
        return "unknown";
    }
}

telemetry_message_status_t telemetry_message_build(const telemetry_message_context_t *context,
                                                   char *buffer,
                                                   size_t buffer_size,
                                                   char *message_id,
                                                   size_t message_id_size)
{
    if (context == NULL || buffer == NULL || buffer_size == 0U || message_id == NULL ||
        message_id_size < TELEMETRY_MESSAGE_ID_LENGTH + 1U || context->device_id == NULL) {
        return TELEMETRY_MESSAGE_INVALID_ARGUMENT;
    }

    const char *source = source_name(context->process.source);
    if (source == NULL) {
        return TELEMETRY_MESSAGE_SOURCE_UNAVAILABLE;
    }

    /* Sin hora de pared fiable no se inventa una fecha, igual que en el
     * registro de la preparación. */
    if (context->epoch_ms < EXTRACTION_MESSAGE_MINIMUM_EPOCH_MS) {
        return TELEMETRY_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    if (!isfinite(context->target_temperature_c) || !isfinite(context->target_pressure_bar)) {
        return TELEMETRY_MESSAGE_VALUE_NOT_FINITE;
    }

    char occurred_at[32];
    if (!extraction_message_format_instant(context->epoch_ms, occurred_at, sizeof(occurred_at))) {
        return TELEMETRY_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    /* El instante entra en el identificador porque la secuencia se reinicia con
     * el dispositivo y dos arranques podrían repetirla. */
    const int identifier_written = snprintf(message_id, message_id_size, "tlm-%" PRIu64 "-%08" PRIu32,
                                            context->epoch_ms, context->sequence);
    if (identifier_written <= 0 || (size_t)identifier_written >= message_id_size) {
        return TELEMETRY_MESSAGE_BUFFER_TOO_SMALL;
    }

    const char *temperature_quality =
        channel_quality_name(context->process.source, context->process.temperature_quality);
    const char *pressure_quality =
        channel_quality_name(context->process.source, context->process.pressure_quality);

    char temperature_value[24];
    char pressure_value[24];
    format_value(context->process.temperature_c, temperature_quality, temperature_value,
                 sizeof(temperature_value));
    format_value(context->process.pressure_bar, pressure_quality, pressure_value,
                 sizeof(pressure_value));

    char preparation[EXTRACTION_MESSAGE_ID_LENGTH + 3U];
    if (context->preparation_id != NULL && context->preparation_id[0] != '\0') {
        snprintf(preparation, sizeof(preparation), "\"%s\"", context->preparation_id);
    } else {
        snprintf(preparation, sizeof(preparation), "null");
    }

    char heater_applied[40] = "";
    if (context->heater_applied_known) {
        snprintf(heater_applied, sizeof(heater_applied), ",\"heaterAppliedMs\":%" PRIu64,
                 context->heater_applied_ms);
    }

    const int written = snprintf(
        buffer, buffer_size,
        "{\"schemaVersion\":\"1.0\""
        ",\"messageId\":\"%s\""
        ",\"messageType\":\"telemetry.sample\""
        ",\"deviceId\":\"%s\""
        ",\"sequence\":%" PRIu32
        ",\"occurredAt\":\"%s\""
        ",\"payload\":{"
        "\"source\":\"%s\""
        ",\"preparationId\":%s"
        ",\"state\":\"%s\""
        ",\"elapsedMs\":%" PRIu32
        ",\"temperature\":{\"value\":%s,\"unit\":\"Cel\",\"quality\":\"%s\"}"
        ",\"pressure\":{\"value\":%s,\"unit\":\"bar\",\"quality\":\"%s\"}"
        ",\"targets\":{\"temperatureC\":%.2f,\"pressureBar\":%.2f}"
        ",\"actuators\":{\"heaterOn\":%s%s,\"pumpDutyPercent\":%u,\"virtual\":%s}"
        "}}",
        message_id, context->device_id, context->sequence, occurred_at, source, preparation,
        operational_state_name(context->state), context->elapsed_ms, temperature_value,
        temperature_quality, pressure_value, pressure_quality,
        (double)context->target_temperature_c, (double)context->target_pressure_bar,
        context->heater_on ? "true" : "false", heater_applied, (unsigned)context->pump_duty_percent,
        context->virtual_actuation ? "true" : "false");

    if (written <= 0 || (size_t)written >= buffer_size) {
        return TELEMETRY_MESSAGE_BUFFER_TOO_SMALL;
    }
    return TELEMETRY_MESSAGE_OK;
}
