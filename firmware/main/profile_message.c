#include "profile_message.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROPOSALS_KEY "\"profileProposals\""
#define STATUS_ACCEPTED "accepted"

/*
 * Una propuesta: desde su llave de apertura hasta la de cierre. El objeto del
 * perfil es plano, así que la primera llave de cierre es la suya.
 */
typedef struct {
    const char *begin;
    const char *end;
} proposal_t;

/*
 * Comienzo del valor asociado a una clave dentro de la propuesta.
 *
 * La búsqueda no pasa de la llave de cierre. Antes buscaba hasta el final de
 * la respuesta, y un campo opcional que faltara en la primera propuesta se
 * tomaba de la segunda: con el desplazamiento de caldera, eso sería subir la
 * caldera por la petición de otro perfil.
 */
static const char *value_of(const proposal_t *region, const char *key)
{
    char pattern[48];
    const int written = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (written <= 0 || (size_t)written >= sizeof(pattern)) {
        return NULL;
    }
    const char *found = strstr(region->begin, pattern);
    return found == NULL || found >= region->end ? NULL : found + written;
}

/* Copia una cadena entrecomillada. Rechaza si falta, está vacía o no cabe. */
static bool read_string(const proposal_t *region, const char *key, char *out, size_t out_size)
{
    const char *value = value_of(region, key);
    if (value == NULL || *value != '"') {
        return false;
    }
    value++;
    const char *end = strchr(value, '"');
    if (end == NULL) {
        return false;
    }
    const size_t length = (size_t)(end - value);
    if (length == 0U || length >= out_size) {
        return false;
    }
    memcpy(out, value, length);
    out[length] = '\0';
    return true;
}

/* Lee un número. Rechaza si falta o si el texto no es un número completo. */
static bool read_number(const proposal_t *region, const char *key, double *out)
{
    const char *value = value_of(region, key);
    if (value == NULL) {
        return false;
    }
    char *end = NULL;
    const double parsed = strtod(value, &end);
    if (end == value) {
        return false;
    }
    *out = parsed;
    return true;
}

const char *profile_message_status_name(profile_message_status_t status)
{
    switch (status) {
    case PROFILE_MESSAGE_OK:
        return "ok";
    case PROFILE_MESSAGE_INVALID_ARGUMENT:
        return "invalid-argument";
    case PROFILE_MESSAGE_ABSENT:
        return "absent";
    case PROFILE_MESSAGE_MALFORMED:
        return "malformed";
    case PROFILE_MESSAGE_NOT_ACCEPTED:
        return "not-accepted";
    default:
        return "unknown";
    }
}

profile_message_status_t profile_message_parse_first(const char *response,
                                                     extraction_profile_t *profile)
{
    if (response == NULL || profile == NULL) {
        return PROFILE_MESSAGE_INVALID_ARGUMENT;
    }

    const char *list = strstr(response, PROPOSALS_KEY);
    if (list == NULL) {
        return PROFILE_MESSAGE_ABSENT;
    }
    const char *open = strchr(list, '[');
    if (open == NULL) {
        return PROFILE_MESSAGE_MALFORMED;
    }
    const char *first = strchr(open, '{');
    const char *close = strchr(open, ']');
    if (first == NULL || close == NULL || first > close) {
        return PROFILE_MESSAGE_ABSENT;  /* lista vacía: no hay nada que aplicar */
    }
    const proposal_t proposal = {.begin = first, .end = strchr(first, '}')};
    if (proposal.end == NULL) {
        return PROFILE_MESSAGE_MALFORMED;
    }

    /* El estado se comprueba antes que nada: sin aceptación declarada no se
     * sigue leyendo, aunque el resto del objeto esté impecable. */
    char status[24];
    if (!read_string(&proposal, "status", status, sizeof(status))) {
        return PROFILE_MESSAGE_MALFORMED;
    }
    if (strcmp(status, STATUS_ACCEPTED) != 0) {
        return PROFILE_MESSAGE_NOT_ACCEPTED;
    }

    extraction_profile_t parsed = {0};
    double version = 0.0;
    double temperature = 0.0;
    double temperature_tolerance = 0.0;
    double pressure = 0.0;
    double pressure_tolerance = 0.0;
    double maximum_time = 0.0;

    if (!read_string(&proposal, "id", parsed.id, sizeof(parsed.id)) ||
        !read_string(&proposal, "name", parsed.name, sizeof(parsed.name)) ||
        !read_number(&proposal, "version", &version) ||
        !read_number(&proposal, "temperatureSetpointC", &temperature) ||
        !read_number(&proposal, "temperatureToleranceC", &temperature_tolerance) ||
        !read_number(&proposal, "pressureSetpointBar", &pressure) ||
        !read_number(&proposal, "pressureToleranceBar", &pressure_tolerance) ||
        !read_number(&proposal, "maximumTimeMs", &maximum_time)) {
        return PROFILE_MESSAGE_MALFORMED;
    }

    parsed.version = (uint32_t)version;
    parsed.temperature_setpoint_c = (float)temperature;
    parsed.temperature_tolerance_c = (float)temperature_tolerance;
    parsed.pressure_setpoint_bar = (float)pressure;
    parsed.pressure_tolerance_bar = (float)pressure_tolerance;
    parsed.maximum_time_ms = (uint32_t)maximum_time;

    /* La masa esperada es opcional en el contrato: su ausencia no invalida la
     * configuración, pero un valor ilegible sí, porque no se puede distinguir
     * de un cero deliberado. */
    const char *mass = value_of(&proposal, "expectedBeverageMassG");
    if (mass != NULL && strncmp(mass, "null", 4) != 0) {
        double expected = 0.0;
        if (!read_number(&proposal, "expectedBeverageMassG", &expected)) {
            return PROFILE_MESSAGE_MALFORMED;
        }
        parsed.expected_beverage_mass_defined = true;
        parsed.expected_beverage_mass_g = (float)expected;
    }

    /* El corte del purgado también es opcional, pero no admite null: el
     * contrato lo declara entero. Si viene, tiene que leerse como un número
     * entero positivo; el rango lo decide la validación del perfil, que lo
     * rechaza con causa en vez de recortarlo. */
    if (value_of(&proposal, "purgeMaximumTimeMs") != NULL) {
        double purge = 0.0;
        if (!read_number(&proposal, "purgeMaximumTimeMs", &purge) ||
            purge != floor(purge) || purge < 1.0 || purge > (double)UINT32_MAX) {
            return PROFILE_MESSAGE_MALFORMED;
        }
        parsed.purge_maximum_defined = true;
        parsed.purge_maximum_ms = (uint32_t)purge;
    }

    /* El desplazamiento de caldera del primer uso (ADR-009, enmienda del
     * 2026-10-19) también es opcional y tampoco admite null: su ausencia ya
     * dice «cero». Si viene tiene que leerse como número; el signo, el máximo
     * y el límite duro los juzga la activación, que rechaza con causa en vez
     * de recortar. */
    if (value_of(&proposal, "boilerOffsetC") != NULL) {
        double offset = 0.0;
        if (!read_number(&proposal, "boilerOffsetC", &offset)) {
            return PROFILE_MESSAGE_MALFORMED;
        }
        parsed.boiler_offset_c = (float)offset;
    }

    /* El refuerzo de la prealimentación (enmienda del 2026-10-27), con la
     * misma regla: opcional, sin null, número; lo juzga la activación. */
    if (value_of(&proposal, "firstUseFeedforwardGain") != NULL) {
        double gain = 0.0;
        if (!read_number(&proposal, "firstUseFeedforwardGain", &gain)) {
            return PROFILE_MESSAGE_MALFORMED;
        }
        parsed.first_use_feedforward_gain = (float)gain;
    }

    *profile = parsed;
    return PROFILE_MESSAGE_OK;
}
