#include "firmware_update.h"

#include <string.h>

firmware_update_verdict_t firmware_update_decide(const firmware_update_conditions_t *conditions)
{
    if (conditions == NULL) {
        return FIRMWARE_UPDATE_UNDECIDED;
    }
    /* El orden es el de la explicación que necesita quien hizo el gesto: primero
     * lo que depende de él (apagar la máquina), después lo que no. */
    if (conditions->in_progress) {
        return FIRMWARE_UPDATE_REFUSED_BUSY;
    }
    if (conditions->state != OP_STATE_SAFE_INACTIVE) {
        return FIRMWARE_UPDATE_REFUSED_STATE;
    }
    /* Apagada, el guardián no debería admitir ninguna salida. Se comprueba de
     * todos modos: el estado lo dice la máquina de estados y el veredicto lo da
     * el guardián, y si discrepan manda el que corta. */
    if (conditions->pump_allowed) {
        return FIRMWARE_UPDATE_REFUSED_PUMP;
    }
    if (conditions->heater_allowed) {
        return FIRMWARE_UPDATE_REFUSED_HEATER;
    }
    if (conditions->identification_active) {
        return FIRMWARE_UPDATE_REFUSED_IDENTIFICATION;
    }
    if (!conditions->source_configured) {
        return FIRMWARE_UPDATE_REFUSED_NO_SOURCE;
    }
    if (!conditions->network_ready) {
        return FIRMWARE_UPDATE_REFUSED_NO_NETWORK;
    }
    return FIRMWARE_UPDATE_ALLOWED;
}

bool firmware_update_may_continue(operational_state_t state)
{
    return state == OP_STATE_SAFE_INACTIVE || state == OP_STATE_ALARM;
}

const char *firmware_update_verdict_name(firmware_update_verdict_t verdict)
{
    switch (verdict) {
    case FIRMWARE_UPDATE_ALLOWED:
        return "admitida";
    case FIRMWARE_UPDATE_REFUSED_STATE:
        return "la máquina no está apagada";
    case FIRMWARE_UPDATE_REFUSED_PUMP:
        return "el guardián admite la bomba";
    case FIRMWARE_UPDATE_REFUSED_HEATER:
        return "el guardián admite el calentador";
    case FIRMWARE_UPDATE_REFUSED_IDENTIFICATION:
        return "modo restringido armado o en curso";
    case FIRMWARE_UPDATE_REFUSED_NO_NETWORK:
        return "sin red";
    case FIRMWARE_UPDATE_REFUSED_NO_SOURCE:
        return "sin dirección del computador";
    case FIRMWARE_UPDATE_REFUSED_BUSY:
        return "ya hay una en curso";
    case FIRMWARE_UPDATE_UNDECIDED:
    default:
        return "sin decidir";
    }
}

/* Devuelve el primer carácter después de `"clave":`, saltando espacios. */
static const char *value_of(const char *json, const char *key)
{
    char pattern[24];
    const size_t key_length = strlen(key);
    if (key_length + 3U > sizeof(pattern)) {
        return NULL;
    }
    pattern[0] = '"';
    memcpy(pattern + 1, key, key_length);
    pattern[key_length + 1U] = '"';
    pattern[key_length + 2U] = '\0';

    const char *found = strstr(json, pattern);
    if (found == NULL) {
        return NULL;
    }
    const char *cursor = found + key_length + 2U;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
        cursor++;
    }
    if (*cursor != ':') {
        return NULL;
    }
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
        cursor++;
    }
    return cursor;
}

/*
 * Copia una cadena JSON sin secuencias de escape. Un nombre de proyecto o una
 * versión con barras invertidas no los produce ESP-IDF: si aparecen, el
 * manifiesto no es de los que escribe el computador, y se rechaza en lugar de
 * interpretarlo a medias.
 */
static bool read_string(const char *json, const char *key, char *out, size_t capacity)
{
    const char *cursor = value_of(json, key);
    if (cursor == NULL || *cursor != '"') {
        return false;
    }
    cursor++;
    size_t length = 0U;
    while (cursor[length] != '"') {
        if (cursor[length] == '\0' || cursor[length] == '\\' || length + 1U >= capacity) {
            return false;
        }
        length++;
    }
    if (length == 0U) {
        return false;
    }
    memcpy(out, cursor, length);
    out[length] = '\0';
    return true;
}

static bool read_size(const char *json, uint32_t *out)
{
    const char *cursor = value_of(json, "size");
    if (cursor == NULL || *cursor < '0' || *cursor > '9') {
        return false;
    }
    uint64_t value = 0U;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10U + (uint64_t)(*cursor - '0');
        if (value > UINT32_MAX) {
            return false;
        }
        cursor++;
    }
    if (value == 0U) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool read_digest(const char *json, uint8_t out[FIRMWARE_UPDATE_DIGEST_BYTES])
{
    char text[FIRMWARE_UPDATE_DIGEST_BYTES * 2U + 1U];
    if (!read_string(json, "imageSha256", text, sizeof(text)) ||
        strlen(text) != FIRMWARE_UPDATE_DIGEST_BYTES * 2U) {
        return false;
    }
    for (size_t index = 0U; index < FIRMWARE_UPDATE_DIGEST_BYTES; index++) {
        const int high = hex_value(text[2U * index]);
        const int low = hex_value(text[2U * index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[index] = (uint8_t)((high << 4) | low);
    }
    return true;
}

firmware_manifest_status_t firmware_manifest_parse(const char *json,
                                                   const char *running_project,
                                                   const uint8_t running_digest[FIRMWARE_UPDATE_DIGEST_BYTES],
                                                   uint32_t partition_bytes,
                                                   firmware_manifest_t *manifest)
{
    if (json == NULL || running_project == NULL || manifest == NULL) {
        return FIRMWARE_MANIFEST_UNREAD;
    }
    firmware_manifest_t read = {0};
    if (!read_string(json, "project", read.project, sizeof(read.project)) ||
        !read_string(json, "version", read.version, sizeof(read.version)) ||
        !read_size(json, &read.size_bytes) || !read_digest(json, read.digest)) {
        return FIRMWARE_MANIFEST_MALFORMED;
    }
    *manifest = read;
    if (strcmp(read.project, running_project) != 0) {
        return FIRMWARE_MANIFEST_WRONG_PROJECT;
    }
    if (read.size_bytes > partition_bytes) {
        return FIRMWARE_MANIFEST_TOO_LARGE;
    }
    if (running_digest != NULL &&
        memcmp(read.digest, running_digest, FIRMWARE_UPDATE_DIGEST_BYTES) == 0) {
        return FIRMWARE_MANIFEST_ALREADY_RUNNING;
    }
    return FIRMWARE_MANIFEST_OK;
}

bool firmware_manifest_digest_matches(const firmware_manifest_t *manifest,
                                      const uint8_t written_digest[FIRMWARE_UPDATE_DIGEST_BYTES])
{
    if (manifest == NULL || written_digest == NULL) {
        return false;
    }
    return memcmp(manifest->digest, written_digest, FIRMWARE_UPDATE_DIGEST_BYTES) == 0;
}

const char *firmware_manifest_status_name(firmware_manifest_status_t status)
{
    switch (status) {
    case FIRMWARE_MANIFEST_OK:
        return "válido";
    case FIRMWARE_MANIFEST_MALFORMED:
        return "mal formado";
    case FIRMWARE_MANIFEST_WRONG_PROJECT:
        return "de otro binario";
    case FIRMWARE_MANIFEST_TOO_LARGE:
        return "no cabe en la partición";
    case FIRMWARE_MANIFEST_ALREADY_RUNNING:
        return "es el que ya corre";
    case FIRMWARE_MANIFEST_UNREAD:
    default:
        return "sin leer";
    }
}
