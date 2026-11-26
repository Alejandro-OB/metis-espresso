#pragma once

/*
 * Construcción del lote de sincronización y lectura de su confirmación.
 *
 * Módulo puro, verificable en el host: no depende de ESP-IDF ni de la pila de
 * red. El cuerpo resultante debe cumplir `sync.schema.json` y la confirmación
 * determina qué mensajes pueden borrarse de la cola.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* El servicio externo acepta como máximo 64 mensajes por petición. */
#define SYNC_MAX_BATCH 8U

typedef enum {
    SYNC_BODY_OK = 0,
    SYNC_BODY_INVALID_ARGUMENT,
    SYNC_BODY_EMPTY_BATCH,
    SYNC_BODY_BUFFER_TOO_SMALL,
} sync_body_status_t;

/* Envuelve los mensajes ya serializados en el cuerpo de `POST /api/v1/sync`. */
sync_body_status_t sync_body_build(const char *device_id,
                                   const char *const *messages,
                                   size_t message_count,
                                   char *buffer,
                                   size_t buffer_size,
                                   size_t *written);

/*
 * Compone una sincronización sin mensajes, cuyo único fin es que el servicio
 * responda con su hora.
 *
 * Es una petición distinta de la entrega de la cola y por eso tiene su propia
 * función: entregar exige lote no vacío —enviar una entrega sin nada que
 * entregar sería una llamada inútil—, mientras que pedir la hora requiere
 * exactamente lo contrario.
 */
sync_body_status_t sync_time_request_build(const char *device_id,
                                           char *buffer,
                                           size_t buffer_size,
                                           size_t *written);

/*
 * Indica si la respuesta confirma un identificador concreto.
 *
 * Solo se borran de la cola los mensajes que el servicio declara recibidos: una
 * respuesta truncada o parcial no puede provocar la pérdida de un registro.
 */
bool sync_response_acknowledges(const char *response, const char *message_id);

/* Número de identificadores confirmados; -1 si la respuesta no es legible. */
int sync_response_acknowledged_count(const char *response);

/*
 * Lee la hora declarada por el servicio en `serverTime` y la devuelve en
 * milisegundos desde la época.
 *
 * Es la fuente de hora de respaldo de ADR-013 § 4.4: cuando el dispositivo no
 * alcanza un servidor de hora —una instalación sin salida a Internet, o una
 * consulta que no responde— la propia plataforma con la que ya está hablando
 * puede fecharle la evidencia.
 *
 * Devuelve false cuando el campo falta, cuando no está en el formato del
 * contrato o cuando alguna de sus partes está fuera de rango. Solo se admite
 * la hora en UTC, terminada en `Z`: un instante con desfase horario se rechaza
 * en lugar de interpretarse, porque equivocarse ahí desplazaría en silencio la
 * fecha de toda la evidencia.
 */
bool sync_response_server_time_ms(const char *response, uint64_t *epoch_ms);

/*
 * Indica si la hora del dispositivo debe tomar la que declara el servicio.
 *
 * La hora se pedía solo al arrancar. El 2026-10-26 la plataforma respondió en
 * los 36 s en que su propio reloj seguía con la hora de la noche anterior, y el
 * dispositivo fechó todo 9 h 55 min atrás hasta reiniciarse. Cada respuesta
 * trae la hora del servicio; si la del dispositivo se aparta más que
 * `tolerance_ms`, se corrige. Sin hora propia (`device_ms` == 0) siempre.
 *
 * La tolerancia la elige quien llama: tiene que quedar muy por encima del
 * viaje de ida y vuelta de la petición, que es lo que separa los dos relojes
 * aunque ambos estén bien.
 */
bool sync_clock_needs_correction(uint64_t device_ms, uint64_t service_ms, uint64_t tolerance_ms);
