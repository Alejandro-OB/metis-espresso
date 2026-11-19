#include "calibration_command.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char PREFIX[] = "K,";

/* Campos de `set` tras la orden: identificador, versión y ocho coeficientes. */
#define SET_FIELDS 10U
/* Holgura para el campo más largo: el identificador. */
#define FIELD_BYTES SENSOR_CALIBRATION_ID_LENGTH

/* Longitud sin el salto final ni los espacios que lo acompañen. */
static size_t trimmed_length(const char *text)
{
    size_t length = strlen(text);
    while (length > 0U) {
        const char last = text[length - 1U];
        if (last == '\n' || last == '\r' || last == ' ' || last == '\t') {
            --length;
            continue;
        }
        break;
    }
    return length;
}

static bool is_word(const char *rest, size_t length, const char *word)
{
    return length == strlen(word) && strncmp(rest, word, length) == 0;
}

/*
 * Parte `text` (de longitud `length`) por comas en exactamente `expected`
 * campos. Un campo vacío o demasiado largo invalida la línea entera: no se
 * recorta ni se completa nada.
 */
static bool split_fields(const char *text, size_t length, char fields[][FIELD_BYTES],
                         size_t expected)
{
    size_t field = 0U;
    size_t used = 0U;
    for (size_t index = 0U; index <= length; index++) {
        const bool end = index == length || text[index] == ',';
        if (!end) {
            if (used + 1U >= FIELD_BYTES || field >= expected) {
                return false;
            }
            fields[field][used++] = text[index];
            continue;
        }
        if (field >= expected || used == 0U) {
            return false;
        }
        fields[field][used] = '\0';
        field++;
        used = 0U;
    }
    return field == expected;
}

/* Un número entero y nada más. `strtof` aceptaría «12abc»; aquí no. */
static bool parse_float(const char *text, float *value)
{
    char *end = NULL;
    errno = 0;
    const float parsed = strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_version(const char *text, uint32_t *value)
{
    if (text[0] < '0' || text[0] > '9') {
        return false;  /* sin signo: strtoul aceptaría «-1» como un número enorme */
    }
    char *end = NULL;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (*end != '\0' || errno == ERANGE || parsed == 0UL || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static calibration_command_t rejected(calibration_reject_t reject)
{
    calibration_command_t command = {0};
    command.kind = CALIBRATION_COMMAND_INVALID;
    command.reject = reject;
    return command;
}

static calibration_command_t parse_set(const char *text, size_t length)
{
    char fields[SET_FIELDS][FIELD_BYTES];
    memset(fields, 0, sizeof(fields));
    if (!split_fields(text, length, fields, SET_FIELDS)) {
        return rejected(CALIBRATION_REJECT_SYNTAX);
    }

    sensor_calibration_t calibration = {0};
    /*
     * El identificador nominal está reservado: una calibración enviada que se
     * llamara así haría pasar por coeficientes del esquema lo que son medidos, y
     * cada medida mentiría sobre su conversión.
     */
    const sensor_calibration_t nominal = sensor_calibration_nominal();
    if (!sensor_calibration_id_is_usable(fields[0]) || strcmp(fields[0], nominal.id) == 0) {
        return rejected(CALIBRATION_REJECT_IDENTIFIER);
    }
    snprintf(calibration.id, sizeof(calibration.id), "%s", fields[0]);
    if (!parse_version(fields[1], &calibration.version)) {
        return rejected(CALIBRATION_REJECT_VERSION);
    }

    float *const targets[] = {
        &calibration.temperature.supply_v,     &calibration.temperature.series_ohms,
        &calibration.temperature.reference_ohms, &calibration.temperature.reference_c,
        &calibration.temperature.beta_k,       &calibration.pressure.gain_bar_per_v,
        &calibration.pressure.offset_bar,      &calibration.scale_counts_per_gram,
    };
    for (size_t index = 0U; index < sizeof(targets) / sizeof(targets[0]); index++) {
        if (!parse_float(fields[2U + index], targets[index])) {
            return rejected(CALIBRATION_REJECT_NUMBER);
        }
    }
    if (!sensor_calibration_is_valid(&calibration)) {
        return rejected(CALIBRATION_REJECT_IMPLAUSIBLE);
    }

    calibration_command_t command = {0};
    command.kind = CALIBRATION_COMMAND_SET;
    command.calibration = calibration;
    return command;
}

calibration_command_t calibration_command_parse(const char *line)
{
    calibration_command_t command = {0};
    if (line == NULL || strncmp(line, PREFIX, sizeof(PREFIX) - 1U) != 0) {
        return command;
    }
    const char *rest = line + sizeof(PREFIX) - 1U;
    const size_t length = trimmed_length(rest);

    if (length > 4U && strncmp(rest, "set,", 4U) == 0) {
        return parse_set(rest + 4U, length - 4U);
    }
    if (is_word(rest, length, "show")) {
        command.kind = CALIBRATION_COMMAND_SHOW;
        return command;
    }
    if (is_word(rest, length, "reset")) {
        command.kind = CALIBRATION_COMMAND_RESET;
        return command;
    }
    if (is_word(rest, length, "tare")) {
        command.kind = CALIBRATION_COMMAND_TARE;
        return command;
    }
    return rejected(CALIBRATION_REJECT_SYNTAX);
}

int calibration_command_format(const sensor_calibration_t *calibration,
                               char *buffer,
                               size_t size)
{
    if (calibration == NULL || buffer == NULL || size == 0U) {
        return -1;
    }
    const int written = snprintf(
        buffer, size, "%s,%lu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g", calibration->id,
        (unsigned long)calibration->version, (double)calibration->temperature.supply_v,
        (double)calibration->temperature.series_ohms,
        (double)calibration->temperature.reference_ohms,
        (double)calibration->temperature.reference_c,
        (double)calibration->temperature.beta_k,
        (double)calibration->pressure.gain_bar_per_v,
        (double)calibration->pressure.offset_bar,
        (double)calibration->scale_counts_per_gram);
    if (written < 0 || (size_t)written >= size) {
        buffer[0] = '\0';
        return -1;
    }
    return written;
}

const char *calibration_reject_name(calibration_reject_t reject)
{
    switch (reject) {
    case CALIBRATION_REJECT_SYNTAX:
        return "syntax";
    case CALIBRATION_REJECT_IDENTIFIER:
        return "identifier";
    case CALIBRATION_REJECT_VERSION:
        return "version";
    case CALIBRATION_REJECT_NUMBER:
        return "number";
    case CALIBRATION_REJECT_IMPLAUSIBLE:
        return "implausible";
    case CALIBRATION_REJECT_NONE:
    default:
        return "none";
    }
}
