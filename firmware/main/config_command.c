#include "config_command.h"

#include <string.h>

static const char PREFIX[] = "C,";

/* Longitud de la línea sin el salto final ni los espacios que lo acompañen. */
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

bool config_command_url_is_usable(const char *url)
{
    if (url == NULL) {
        return false;
    }
    const size_t length = trimmed_length(url);
    if (length == 0U || length >= DEVICE_CONFIG_URL_BYTES) {
        return false;
    }
    return strncmp(url, "http://", 7U) == 0 || strncmp(url, "https://", 8U) == 0;
}

/*
 * Copia el valor de la orden al resultado. El valor es todo lo que sigue a la
 * coma, incluidas las comas siguientes, porque una dirección puede contenerlas
 * y partirla por la primera dejaría una dirección truncada que parece válida.
 */
static bool copy_value(const char *source, char *destination, size_t capacity)
{
    const size_t length = trimmed_length(source);
    if (length == 0U || length >= capacity) {
        return false;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

config_command_t config_command_parse(const char *line)
{
    config_command_t command = {0};
    if (line == NULL || strncmp(line, PREFIX, sizeof(PREFIX) - 1U) != 0) {
        return command;  /* no es una orden de configuración */
    }

    const char *rest = line + sizeof(PREFIX) - 1U;

    if (strncmp(rest, "url,", 4U) == 0) {
        command.kind = CONFIG_COMMAND_INVALID;
        const char *value = rest + 4U;
        if (copy_value(value, command.value, sizeof(command.value)) &&
            config_command_url_is_usable(command.value)) {
            command.kind = CONFIG_COMMAND_SET_URL;
        } else {
            command.value[0] = '\0';
        }
        return command;
    }

    if (strncmp(rest, "ota,", 4U) == 0) {
        command.kind = CONFIG_COMMAND_INVALID;
        const char *value = rest + 4U;
        if (copy_value(value, command.value, sizeof(command.value)) &&
            config_command_url_is_usable(command.value)) {
            command.kind = CONFIG_COMMAND_SET_UPDATE_URL;
        } else {
            command.value[0] = '\0';
        }
        return command;
    }

    if (strncmp(rest, "token,", 6U) == 0) {
        command.kind = CONFIG_COMMAND_INVALID;
        const char *value = rest + 6U;
        /* La credencial se mide contra su propio tamaño, que es menor que el de
         * la dirección: una que no quepa se rechaza aquí y no se guarda a
         * medias, porque una credencial truncada falla al entregar y el motivo
         * no se parece en nada a la causa. */
        if (copy_value(value, command.value, DEVICE_CONFIG_TOKEN_BYTES)) {
            command.kind = CONFIG_COMMAND_SET_TOKEN;
        } else {
            command.value[0] = '\0';
        }
        return command;
    }

    const size_t length = trimmed_length(rest);
    if (length == 4U && strncmp(rest, "show", 4U) == 0) {
        command.kind = CONFIG_COMMAND_SHOW;
        return command;
    }
    if (length == 5U && strncmp(rest, "reset", 5U) == 0) {
        command.kind = CONFIG_COMMAND_RESET;
        return command;
    }

    command.kind = CONFIG_COMMAND_INVALID;
    return command;
}
