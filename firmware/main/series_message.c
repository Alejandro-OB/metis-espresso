#include "series_message.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "extraction_message.h"
#include "pre_start_history.h"

/* El minuto previo se guarda ya sobre la rejilla de la serie: si las dos
 * separaciones divergieran, la serie cambiaría de densidad al pulsar. */
_Static_assert(PRE_START_HISTORY_INTERVAL_MS == SERIES_MESSAGE_INTERVAL_MS,
               "el minuto previo y la serie deben compartir rejilla");

/* Un punto de la serie, venga del minuto previo o de la preparación. */
typedef struct {
    int64_t elapsed_ms;
    float temperature_c;
    float pressure_bar;
    bool temperature_valid;
    bool pressure_valid;
} series_point_t;

/*
 * Elige la muestra que ocupa una posición de la rejilla.
 *
 * El criterio es **seleccionar**, no promediar ni interpolar: cada punto que se
 * conserva es una muestra que el dispositivo tomó de verdad, en el instante que
 * declara. Construir un valor intermedio produciría una serie que no ocurrió y
 * que después se compararía con el modelo como si hubiera ocurrido.
 *
 * El objetivo avanza hasta superar el instante ya alcanzado, de modo que un
 * hueco en el registro no provoque una ráfaga de muestras seguidas al otro
 * lado: tras el hueco se retoma la rejilla, no se recupera lo perdido.
 */
static bool sample_at_grid(const extraction_session_t *session,
                           uint32_t grid_index,
                           const extraction_sample_t **selected)
{
    uint32_t next_target_ms = 0U;
    uint32_t kept = 0U;
    const uint32_t count = session->summary.sample_count < EXTRACTION_MAX_SAMPLES
                               ? session->summary.sample_count
                               : EXTRACTION_MAX_SAMPLES;

    for (uint32_t index = 0U; index < count; ++index) {
        const extraction_sample_t *sample = &session->samples[index];
        if (sample->elapsed_ms < next_target_ms) {
            continue;
        }
        if (kept == grid_index) {
            if (selected != NULL) {
                *selected = sample;
            }
            return true;
        }
        ++kept;
        do {
            next_target_ms += SERIES_MESSAGE_INTERVAL_MS;
        } while (next_target_ms <= sample->elapsed_ms);
    }
    return false;
}

static uint32_t pre_start_count(const extraction_session_t *session)
{
    return session->pre_start_count < PRE_START_HISTORY_CAPACITY ? session->pre_start_count
                                                                  : PRE_START_HISTORY_CAPACITY;
}

/*
 * Punto `index` de la serie completa. Las muestras previas van primero porque
 * sus instantes son negativos: así el orden de emisión es el del tiempo, y la
 * numeración de tramos cubre las dos partes sin que nadie tenga que saber
 * dónde termina una y empieza la otra. Las previas ya están sobre la rejilla
 * (`pre_start_history`), de modo que aquí no se vuelven a seleccionar.
 */
static bool point_at(const extraction_session_t *session, uint32_t index, series_point_t *point)
{
    const uint32_t previous = pre_start_count(session);
    if (index < previous) {
        const pre_start_sample_t *sample = &session->pre_start[index];
        *point = (series_point_t){
            .elapsed_ms = sample->elapsed_ms,
            .temperature_c = sample->temperature_c,
            .pressure_bar = sample->pressure_bar,
            .temperature_valid = sample->temperature_valid,
            .pressure_valid = sample->pressure_valid,
        };
        return true;
    }
    const extraction_sample_t *sample = NULL;
    if (!sample_at_grid(session, index - previous, &sample)) {
        return false;
    }
    *point = (series_point_t){
        .elapsed_ms = sample->elapsed_ms,
        .temperature_c = sample->observed_temperature_c,
        .pressure_bar = sample->observed_pressure_bar,
        .temperature_valid = sample->temperature_valid,
        .pressure_valid = sample->pressure_valid,
    };
    return true;
}

static uint32_t kept_sample_count(const extraction_session_t *session)
{
    uint32_t kept = 0U;
    while (sample_at_grid(session, kept, NULL)) {
        ++kept;
    }
    return pre_start_count(session) + kept;
}

uint32_t series_message_chunk_count(const extraction_session_t *session)
{
    if (session == NULL) {
        return 0U;
    }
    const uint32_t kept = kept_sample_count(session);
    if (kept == 0U) {
        return 0U;
    }
    return (kept + SERIES_MESSAGE_SAMPLES_PER_CHUNK - 1U) / SERIES_MESSAGE_SAMPLES_PER_CHUNK;
}

static const char *channel_quality_name(bool valid, bool measured)
{
    if (!measured) {
        return "simulated";
    }
    return valid ? "valid" : "invalid";
}

/*
 * Escribe un canal del contrato. Un valor que no es finito se emite como
 * ausente y no como número: un `nan` en el JSON lo rechazaría el validador, y
 * sustituirlo por cero afirmaría una medida que nadie tomó.
 */
static int write_channel(char *buffer, size_t capacity, float value, bool valid, bool measured)
{
    const bool usable = valid && isfinite((double)value);
    if (!usable) {
        return snprintf(buffer, capacity, "{\"value\":null,\"quality\":\"%s\"}",
                        channel_quality_name(false, measured));
    }
    return snprintf(buffer, capacity, "{\"value\":%.3f,\"quality\":\"%s\"}", (double)value,
                    channel_quality_name(true, measured));
}

series_message_status_t series_message_build(const extraction_session_t *session,
                                             const series_message_context_t *context,
                                             uint32_t chunk_index,
                                             char *buffer,
                                             size_t buffer_size,
                                             char *message_id,
                                             size_t message_id_size)
{
    if (session == NULL || context == NULL || buffer == NULL || buffer_size == 0U ||
        context->device_id == NULL || context->device_id[0] == '\0' ||
        session->summary.id[0] == '\0') {
        return SERIES_MESSAGE_INVALID_ARGUMENT;
    }
    if (context->epoch_ms == 0U) {
        return SERIES_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    const uint32_t chunks = series_message_chunk_count(session);
    if (chunk_index >= chunks) {
        return SERIES_MESSAGE_ABSENT;
    }

    char identifier[SERIES_MESSAGE_ID_LENGTH + 1U];
    const int identifier_length = snprintf(identifier, sizeof(identifier), "series-%s-%03" PRIu32,
                                           session->summary.id, chunk_index);
    if (identifier_length <= 0 || (size_t)identifier_length >= sizeof(identifier)) {
        return SERIES_MESSAGE_BUFFER_TOO_SMALL;
    }
    if (message_id != NULL) {
        if (message_id_size <= (size_t)identifier_length) {
            return SERIES_MESSAGE_BUFFER_TOO_SMALL;
        }
        memcpy(message_id, identifier, (size_t)identifier_length + 1U);
    }

    char occurred_at[32];
    if (!extraction_message_format_instant(context->epoch_ms, occurred_at, sizeof(occurred_at))) {
        return SERIES_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    int written = snprintf(
        buffer, buffer_size,
        "{\"schemaVersion\":\"1.0\""
        ",\"messageId\":\"%s\""
        ",\"messageType\":\"extraction.series\""
        ",\"deviceId\":\"%s\""
        ",\"sequence\":%" PRIu32
        ",\"occurredAt\":\"%s\""
        ",\"payload\":{"
        "\"preparationId\":\"%s\""
        ",\"source\":\"%s\""
        ",\"chunkIndex\":%" PRIu32
        ",\"chunkCount\":%" PRIu32
        ",\"intervalMs\":%u"
        ",\"samples\":[",
        identifier, context->device_id, context->sequence, occurred_at, session->summary.id,
        context->measured ? "measured" : "simulation", chunk_index, chunks,
        (unsigned)SERIES_MESSAGE_INTERVAL_MS);
    if (written <= 0 || (size_t)written >= buffer_size) {
        return SERIES_MESSAGE_BUFFER_TOO_SMALL;
    }
    size_t used = (size_t)written;

    const uint32_t first = chunk_index * SERIES_MESSAGE_SAMPLES_PER_CHUNK;
    for (uint32_t offset = 0U; offset < SERIES_MESSAGE_SAMPLES_PER_CHUNK; ++offset) {
        series_point_t point;
        if (!point_at(session, first + offset, &point)) {
            break;  /* el último tramo puede llevar menos muestras */
        }

        char temperature[64];
        char pressure[64];
        if (write_channel(temperature, sizeof(temperature), point.temperature_c,
                          point.temperature_valid, context->measured) <= 0 ||
            write_channel(pressure, sizeof(pressure), point.pressure_bar,
                          point.pressure_valid, context->measured) <= 0) {
            return SERIES_MESSAGE_BUFFER_TOO_SMALL;
        }

        written = snprintf(buffer + used, buffer_size - used,
                           "%s{\"elapsedMs\":%" PRId64 ",\"temperature\":%s,\"pressure\":%s}",
                           offset == 0U ? "" : ",", point.elapsed_ms, temperature, pressure);
        if (written <= 0 || (size_t)written >= buffer_size - used) {
            return SERIES_MESSAGE_BUFFER_TOO_SMALL;
        }
        used += (size_t)written;
    }

    written = snprintf(buffer + used, buffer_size - used, "]}}");
    if (written <= 0 || (size_t)written >= buffer_size - used) {
        return SERIES_MESSAGE_BUFFER_TOO_SMALL;
    }
    return SERIES_MESSAGE_OK;
}

const char *series_message_status_name(series_message_status_t status)
{
    switch (status) {
    case SERIES_MESSAGE_OK:
        return "ok";
    case SERIES_MESSAGE_INVALID_ARGUMENT:
        return "invalid-argument";
    case SERIES_MESSAGE_BUFFER_TOO_SMALL:
        return "buffer-too-small";
    case SERIES_MESSAGE_ABSENT:
        return "absent";
    case SERIES_MESSAGE_TIME_BASE_UNAVAILABLE:
        return "time-base-unavailable";
    default:
        return "unknown";
    }
}
