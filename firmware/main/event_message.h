#pragma once

/*
 * Composición del evento operativo del contrato v1.
 *
 * Un evento describe lo que le ocurrió a la máquina —un cambio de estado, una
 * alarma, el fin de una preparación— y es evidencia, no observación: viaja por
 * la cola persistente y se reintenta hasta que la plataforma lo confirma, a
 * diferencia de la telemetría, que se pierde si no hay enlace (ADR-012 § 4).
 *
 * Esta unidad no decide qué se emite ni lo envía: convierte una transición ya
 * ocurrida en el sobre correspondiente, de modo que pueda probarse en el equipo
 * de desarrollo sin red ni FreeRTOS.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "condition_monitor.h"
#include "operational_fsm.h"

#define EVENT_MESSAGE_ID_LENGTH 48U
#define EVENT_MESSAGE_JSON_BYTES 640U

typedef enum {
    EVENT_MESSAGE_OK = 0,
    EVENT_MESSAGE_INVALID_ARGUMENT,
    EVENT_MESSAGE_BUFFER_TOO_SMALL,
    EVENT_MESSAGE_TIME_BASE_UNAVAILABLE,
    /* Una transición rechazada no cambió nada: no se publica como si hubiera
     * ocurrido algo. */
    EVENT_MESSAGE_NOT_REPORTABLE,
} event_message_status_t;

typedef struct {
    const char *device_id;
    uint32_t sequence;
    uint64_t epoch_ms;
    /* NULL cuando el evento no pertenece a ninguna preparación. */
    const char *preparation_id;
    operational_transition_t transition;
} event_message_context_t;

event_message_status_t event_message_build(const event_message_context_t *context,
                                           char *buffer,
                                           size_t buffer_size,
                                           char *message_id,
                                           size_t message_id_size);

/*
 * Contexto de un aviso de condición. A diferencia de una transición, aquí no
 * hay estado anterior: la máquina no cambió de estado, cambió su capacidad de
 * actuar.
 */
typedef struct {
    const char *device_id;
    uint32_t sequence;
    uint64_t epoch_ms;
    const char *preparation_id;
    operational_state_t state;
    condition_event_t condition;
} condition_event_context_t;

event_message_status_t event_message_build_condition(const condition_event_context_t *context,
                                                     char *buffer,
                                                     size_t buffer_size,
                                                     char *message_id,
                                                     size_t message_id_size);

const char *event_message_status_name(event_message_status_t status);
