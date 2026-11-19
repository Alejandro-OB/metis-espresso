#pragma once

/*
 * Lectura de una configuración propuesta desde la respuesta de sincronización.
 *
 * Es la única vía por la que una configuración aceptada por el barista llega a
 * la máquina, y sigue sin ser una orden: el dispositivo la valida contra sus
 * límites y puede rechazarla con causa (ADR-001 § 6, ADR-003 § 6).
 *
 * El módulo es puro y deliberadamente desconfiado: ante cualquier campo que
 * falte, esté vacío o no pueda leerse, **rechaza**. No completa valores por
 * omisión ni deduce lo que falta, porque un perfil a medio leer gobernaría
 * temperatura y presión de una máquina real.
 *
 * Tampoco se fía de que la plataforma haya filtrado: solo admite lo que declare
 * haber sido aceptado por el barista.
 *
 * Los campos opcionales (masa esperada, corte del purgado y desplazamiento de
 * caldera del primer uso) pueden faltar; si vienen y no se pueden leer, la
 * propuesta entera se rechaza. Su rango no es cosa del lector.
 */

#include <stdbool.h>
#include <stddef.h>

#include "extraction_profile.h"

typedef enum {
    PROFILE_MESSAGE_OK = 0,
    PROFILE_MESSAGE_INVALID_ARGUMENT,
    /* La respuesta no traía ninguna propuesta: es lo normal. */
    PROFILE_MESSAGE_ABSENT,
    /* Traía algo que no puede leerse como configuración completa. */
    PROFILE_MESSAGE_MALFORMED,
    /* Traía una configuración que nadie declaró aceptada. */
    PROFILE_MESSAGE_NOT_ACCEPTED,
} profile_message_status_t;

/*
 * Lee la primera propuesta de la respuesta. Solo se atiende una por
 * sincronización: aplicar varias en el mismo ciclo dejaría sin saber cuál rige.
 */
profile_message_status_t profile_message_parse_first(const char *response,
                                                     extraction_profile_t *profile);

const char *profile_message_status_name(profile_message_status_t status);
