#include "sync_transport.h"

#include <stdio.h>
#include <string.h>

static const char ACKNOWLEDGED_KEY[] = "\"acknowledged\"";
static const char SERVER_TIME_KEY[] = "\"serverTime\"";

sync_body_status_t sync_body_build(const char *device_id,
                                   const char *const *messages,
                                   size_t message_count,
                                   char *buffer,
                                   size_t buffer_size,
                                   size_t *written)
{
    if (device_id == NULL || device_id[0] == '\0' || messages == NULL ||
        buffer == NULL || buffer_size == 0U) {
        return SYNC_BODY_INVALID_ARGUMENT;
    }
    if (message_count == 0U || message_count > SYNC_MAX_BATCH) {
        return SYNC_BODY_EMPTY_BATCH;
    }

    int offset = snprintf(buffer, buffer_size, "{\"deviceId\":\"%s\",\"messages\":[", device_id);
    if (offset <= 0 || (size_t)offset >= buffer_size) {
        return SYNC_BODY_BUFFER_TOO_SMALL;
    }
    size_t used = (size_t)offset;
    for (size_t index = 0U; index < message_count; ++index) {
        if (messages[index] == NULL || messages[index][0] == '\0') {
            return SYNC_BODY_INVALID_ARGUMENT;
        }
        const char *separator = index == 0U ? "" : ",";
        const int chunk = snprintf(buffer + used, buffer_size - used, "%s%s",
                                   separator, messages[index]);
        if (chunk <= 0 || (size_t)chunk >= buffer_size - used) {
            return SYNC_BODY_BUFFER_TOO_SMALL;
        }
        used += (size_t)chunk;
    }
    const int tail = snprintf(buffer + used, buffer_size - used, "]}");
    if (tail <= 0 || (size_t)tail >= buffer_size - used) {
        return SYNC_BODY_BUFFER_TOO_SMALL;
    }
    used += (size_t)tail;
    if (written != NULL) {
        *written = used;
    }
    return SYNC_BODY_OK;
}

/* Devuelve el primer carácter del array de confirmados, o NULL. */
static const char *acknowledged_array(const char *response)
{
    if (response == NULL) {
        return NULL;
    }
    const char *key = strstr(response, ACKNOWLEDGED_KEY);
    if (key == NULL) {
        return NULL;
    }
    const char *cursor = key + sizeof(ACKNOWLEDGED_KEY) - 1U;
    while (*cursor == ' ' || *cursor == ':') {
        ++cursor;
    }
    return *cursor == '[' ? cursor + 1 : NULL;
}

sync_body_status_t sync_time_request_build(const char *device_id,
                                           char *buffer,
                                           size_t buffer_size,
                                           size_t *written)
{
    if (device_id == NULL || device_id[0] == '\0' || buffer == NULL || buffer_size == 0U) {
        return SYNC_BODY_INVALID_ARGUMENT;
    }
    const int length =
        snprintf(buffer, buffer_size, "{\"deviceId\":\"%s\",\"messages\":[]}", device_id);
    if (length <= 0 || (size_t)length >= buffer_size) {
        return SYNC_BODY_BUFFER_TOO_SMALL;
    }
    if (written != NULL) {
        *written = (size_t)length;
    }
    return SYNC_BODY_OK;
}

bool sync_response_acknowledges(const char *response, const char *message_id)
{
    const char *cursor = acknowledged_array(response);
    if (cursor == NULL || message_id == NULL || message_id[0] == '\0') {
        return false;
    }
    const size_t length = strlen(message_id);
    while (*cursor != '\0' && *cursor != ']') {
        if (*cursor == '"') {
            const char *start = cursor + 1;
            const char *end = strchr(start, '"');
            if (end == NULL) {
                return false;
            }
            if ((size_t)(end - start) == length && strncmp(start, message_id, length) == 0) {
                return true;
            }
            cursor = end + 1;
            continue;
        }
        ++cursor;
    }
    return false;
}

int sync_response_acknowledged_count(const char *response)
{
    const char *cursor = acknowledged_array(response);
    if (cursor == NULL) {
        return -1;
    }
    int count = 0;
    while (*cursor != '\0' && *cursor != ']') {
        if (*cursor == '"') {
            const char *end = strchr(cursor + 1, '"');
            if (end == NULL) {
                return -1;
            }
            ++count;
            cursor = end + 1;
            continue;
        }
        ++cursor;
    }
    return *cursor == ']' ? count : -1;
}

/*
 * Días transcurridos entre la época y una fecha del calendario civil. Se calcula
 * en lugar de usar las funciones de tiempo de la biblioteca porque este módulo
 * se compila y se prueba en el anfitrión igual que en el dispositivo, y
 * `timegm` no está en el estándar: el resultado debe ser el mismo en ambos.
 * Algoritmo de días civiles de Howard Hinnant, válido para el calendario
 * gregoriano proléptico.
 */
static int64_t days_from_civil(int64_t year, unsigned month, unsigned day)
{
    year -= month <= 2U;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year =
        (153U * (month + (month > 2U ? -3U : 9U)) + 2U) / 5U + day - 1U;
    const unsigned day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return era * 146097 + (int64_t)day_of_era - 719468;
}

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static unsigned days_in_month(int year, unsigned month)
{
    static const unsigned lengths[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                       31U, 31U, 30U, 31U, 30U, 31U};
    if (month == 2U && is_leap_year(year)) {
        return 29U;
    }
    return lengths[month - 1U];
}

bool sync_response_server_time_ms(const char *response, uint64_t *epoch_ms)
{
    if (response == NULL || epoch_ms == NULL) {
        return false;
    }
    const char *key = strstr(response, SERVER_TIME_KEY);
    if (key == NULL) {
        return false;
    }
    const char *cursor = strchr(key + sizeof(SERVER_TIME_KEY) - 1U, '"');
    if (cursor == NULL) {
        return false;
    }
    ++cursor;

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int consumed = 0;
    if (sscanf(cursor, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute,
               &second, &consumed) != 6 ||
        consumed != 19) {
        return false;
    }
    if (year < 1970 || month < 1 || month > 12 || day < 1 ||
        (unsigned)day > days_in_month(year, (unsigned)month) || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 60) {
        return false;
    }

    cursor += consumed;
    /* La fracción de segundo es opcional en el contrato y aquí solo interesan
     * los milisegundos: el resto de dígitos se descarta sin redondear, porque
     * una marca de hora no se mejora inventándole precisión. */
    unsigned milliseconds = 0U;
    if (*cursor == '.') {
        ++cursor;
        unsigned digits = 0U;
        while (*cursor >= '0' && *cursor <= '9') {
            if (digits < 3U) {
                milliseconds = milliseconds * 10U + (unsigned)(*cursor - '0');
                ++digits;
            }
            ++cursor;
        }
        if (digits == 0U) {
            return false;
        }
        while (digits < 3U) {
            milliseconds *= 10U;
            ++digits;
        }
    }
    if (*cursor != 'Z') {
        return false;
    }

    const int64_t days = days_from_civil(year, (unsigned)month, (unsigned)day);
    const int64_t seconds =
        days * INT64_C(86400) + (int64_t)hour * 3600 + (int64_t)minute * 60 + second;
    if (seconds < 0) {
        return false;
    }
    *epoch_ms = (uint64_t)seconds * UINT64_C(1000) + milliseconds;
    return true;
}

bool sync_clock_needs_correction(uint64_t device_ms, uint64_t service_ms, uint64_t tolerance_ms)
{
    if (device_ms == 0U) {
        return true;
    }
    const uint64_t apart = device_ms > service_ms ? device_ms - service_ms : service_ms - device_ms;
    return apart > tolerance_ms;
}
