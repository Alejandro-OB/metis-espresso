#pragma once

/*
 * Serialización del resumen de una preparación a la envolvente del contrato v1.
 *
 * Módulo puro: no depende de ESP-IDF ni de FreeRTOS, de modo que puede
 * verificarse en el host sin la tarjeta. El resultado debe cumplir
 * `platform/contracts/device/envelope.schema.json` con un `payload`
 * conforme a `extraction.schema.json`.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "extraction_session.h"
#include "water_reference.h"

/* Instante mínimo aceptado como hora de pared fiable: 2020-01-01T00:00:00Z. */
#define EXTRACTION_MESSAGE_MINIMUM_EPOCH_MS UINT64_C(1577836800000)

#define EXTRACTION_MESSAGE_ID_LENGTH 80U

typedef enum {
    EXTRACTION_MESSAGE_OK = 0,
    EXTRACTION_MESSAGE_INVALID_ARGUMENT,
    EXTRACTION_MESSAGE_SESSION_INCOMPLETE,
    EXTRACTION_MESSAGE_TIME_BASE_UNAVAILABLE,
    EXTRACTION_MESSAGE_VALUE_NOT_FINITE,
    EXTRACTION_MESSAGE_BUFFER_TOO_SMALL,
} extraction_message_status_t;

typedef struct {
    const char *device_id;
    const char *barista;          /* opcional; NULL se serializa como null */
    bool measured;                /* falso mientras la planta sea simulada */
    uint32_t sequence;
    uint64_t epoch_ms_at_finish;  /* hora de pared al cerrar; 0 si no es fiable */
    /*
     * Hora de pared al abrir. Solo la usa un registro recuperado tras un corte
     * (`unexpected-power-loss`), que no tiene duración de la que deducirla; 0
     * la envía como null. En ese registro `epoch_ms_at_finish` es el instante
     * en que se recuperó, que va como `occurredAt` y no como `finishedAt`.
     */
    uint64_t epoch_ms_at_start;
    /*
     * Con qué se midió (RF-014): la calibración vigente y la versión del
     * firmware. Sin ellas una medida archivada no puede reinterpretarse cuando
     * la calibración cambie. NULL se serializa como null: no se inventa una
     * versión que nadie declaró.
     */
    const char *calibration_id;
    uint32_t calibration_version;
    const char *firmware_version;
    /*
     * La recta caldera → agua con que la placa convirtió el agua del perfil, y
     * el acta de temperatura de la que salió (ADR-009, enmienda del
     * 2026-10-18). La recta cambia con el acta, y sin ella el servicio externo
     * no sabe qué caldera se buscaba en una preparación hecha con otra: la
     * recalcularía con la vigente. Viaja la recta entera, no solo el acta, para
     * que el registro se lea sin una tabla de actas al otro lado. NULL o una
     * relación no válida se escriben null: no consta, como en una preparación
     * recuperada tras un corte, que pudo cerrarse con otro firmware.
     */
    const char *temperature_plant;
    water_reference_t water_reference;
} extraction_message_context_t;

/* Identificador estable del mensaje, derivado de la sesión: la reemisión de una
 * misma preparación no puede duplicar el registro en el servicio externo. */
extraction_message_status_t extraction_message_identifier(
    const extraction_summary_t *summary, char *buffer, size_t buffer_size);

extraction_message_status_t extraction_message_build(
    const extraction_summary_t *summary,
    const extraction_message_context_t *context,
    char *buffer,
    size_t buffer_size,
    size_t *written);

/*
 * Escribe un instante en el formato del contrato. Se expone porque la
 * telemetría necesita el mismo formato y duplicar la conversión de fechas
 * abriría la puerta a que ambas divergieran.
 */
bool extraction_message_format_instant(uint64_t epoch_ms, char *buffer, size_t buffer_size);

const char *extraction_message_status_name(extraction_message_status_t status);
