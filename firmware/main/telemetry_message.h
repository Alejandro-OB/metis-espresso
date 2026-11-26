#pragma once

/*
 * Composición de la muestra de telemetría del contrato v1.
 *
 * La telemetría describe el proceso mientras ocurre y llega a la plataforma sin
 * pasar por la cola persistente: es observación, no evidencia acumulada. La
 * evidencia de una preparación viaja en `extraction.completed` (ADR-012 § 4).
 *
 * Esta unidad no decide cuándo se emite ni la envía: solo construye el sobre a
 * partir de un estado ya observado, de modo que pueda probarse en el equipo de
 * desarrollo sin red ni FreeRTOS.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "operational_fsm.h"
#include "process_variables.h"

#define TELEMETRY_MESSAGE_ID_LENGTH 48U
#define TELEMETRY_MESSAGE_JSON_BYTES 768U

typedef enum {
    TELEMETRY_MESSAGE_OK = 0,
    TELEMETRY_MESSAGE_INVALID_ARGUMENT,
    TELEMETRY_MESSAGE_BUFFER_TOO_SMALL,
    /* Sin origen declarado no se emite: una muestra que no sabe de dónde viene
     * no puede presentarse como medición ni como simulación. */
    TELEMETRY_MESSAGE_SOURCE_UNAVAILABLE,
    /* El contrato exige instantes reales, igual que en `extraction.completed`. */
    TELEMETRY_MESSAGE_TIME_BASE_UNAVAILABLE,
    TELEMETRY_MESSAGE_VALUE_NOT_FINITE,
} telemetry_message_status_t;

typedef struct {
    const char *device_id;
    uint32_t sequence;
    uint64_t epoch_ms;
    /* NULL cuando no hay preparación en curso: el contrato lo admite nulo. */
    const char *preparation_id;
    uint32_t elapsed_ms;
    operational_state_t state;
    process_snapshot_t process;
    float target_temperature_c;
    float target_pressure_bar;
    bool heater_on;
    /* Conducción acumulada del calentador desde el arranque, en ms. Sin ella
     * (`heater_applied_known` falso) el campo no se emite: un cero diría que no
     * calentó. */
    bool heater_applied_known;
    uint64_t heater_applied_ms;
    uint8_t pump_duty_percent;
    /* Cierto cuando la actuación no llega a la máquina, sea porque las salidas
     * están inhibidas o porque el lazo se cierra contra un modelo de banco. */
    bool virtual_actuation;
} telemetry_message_context_t;

telemetry_message_status_t telemetry_message_build(const telemetry_message_context_t *context,
                                                   char *buffer,
                                                   size_t buffer_size,
                                                   char *message_id,
                                                   size_t message_id_size);

const char *telemetry_message_status_name(telemetry_message_status_t status);
