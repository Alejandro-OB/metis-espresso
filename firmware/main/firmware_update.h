#pragma once

/*
 * Lo que decide si la placa puede cambiar su propio firmware por Wi-Fi, y si lo
 * que llegó es lo que se publicó (ADR-021).
 *
 * Con la cafetera sellada el conector USB queda dentro, y la única vía para
 * llevarle un firmware nuevo es la red. La red trae el archivo; **la orden la da
 * alguien delante de la máquina**, con un gesto del mando (`panel_gesture`).
 * Ningún mensaje de red puede pedir una actualización: igual que la
 * configuración del enlace (`config_command`), toda autoridad sobre el
 * dispositivo se queda fuera de la red.
 *
 * ## Por qué solo con la máquina apagada
 *
 * Reiniciar a mitad de una preparación la cortaría sin cerrarla, y escribir la
 * memoria de programa detiene la caché durante cada sector: el lazo y la
 * adquisición se retrasan. Con la máquina apagada (`OP_STATE_SAFE_INACTIVE`) no
 * hay nada que regular ni que perder. Además, mientras dura la actualización el
 * guardián corta las dos salidas por la causa `ACTUATION_INHIBITED_STARTING_UP`,
 * que existía desde el principio para esto.
 *
 * Si durante la descarga alguien enciende la máquina, la actualización se
 * abandona y la placa sigue con el firmware que tenía. La alarma no la abandona:
 * en alarma las salidas ya están cortadas, y una alarma provocada por los
 * retrasos de la propia escritura no debe dejar la actualización a medias.
 *
 * ## Por qué se compara la huella además de la que verifica ESP-IDF
 *
 * `esp_ota_end` ya comprueba que la imagen está entera y es de este chip. Eso
 * dice que el archivo no se corrompió, no que sea **el que se publicó**: un
 * archivo viejo que quedó en la carpeta, o el de otro binario, pasaría igual.
 * La huella del manifiesto la calcula el computador al publicar; si la imagen
 * escrita no la reproduce, no se arranca.
 *
 * ## Lo que no hace
 *
 * - **No autentica el origen.** Quien suplante al computador en la red local y
 *   espere a que alguien haga el gesto podría servir otro firmware con su propio
 *   manifiesto. Firmar las imágenes lo cerraría, y se descartó por ahora: exige
 *   custodiar una clave y cambiar el cargador de arranque, y la red del
 *   laboratorio está aislada (ADR-014). Queda escrito en ADR-021 § 7.
 * - **No descarga ni escribe nada.** Es puro: recibe lo que la envoltura leyó y
 *   devuelve lo que decidió.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "operational_fsm.h"

#define FIRMWARE_UPDATE_DIGEST_BYTES 32U
#define FIRMWARE_UPDATE_PROJECT_BYTES 32U   /* como `esp_app_desc_t.project_name` */
#define FIRMWARE_UPDATE_VERSION_BYTES 32U   /* como `esp_app_desc_t.version` */

/*
 * Si se puede empezar. El cero es «nadie ha decidido», que no autoriza: una
 * estructura sin escribir no puede poner en marcha una escritura de la memoria
 * de programa.
 */
typedef enum {
    FIRMWARE_UPDATE_UNDECIDED = 0,
    FIRMWARE_UPDATE_ALLOWED,
    FIRMWARE_UPDATE_REFUSED_STATE,          /* la máquina no está apagada */
    FIRMWARE_UPDATE_REFUSED_PUMP,           /* el guardián admite la bomba */
    FIRMWARE_UPDATE_REFUSED_HEATER,         /* el guardián admite el calentador */
    FIRMWARE_UPDATE_REFUSED_IDENTIFICATION, /* modo restringido armado o en curso */
    FIRMWARE_UPDATE_REFUSED_NO_NETWORK,
    FIRMWARE_UPDATE_REFUSED_NO_SOURCE,      /* sin dirección del computador */
    FIRMWARE_UPDATE_REFUSED_BUSY,           /* ya hay una en curso */
} firmware_update_verdict_t;

typedef struct {
    operational_state_t state;
    bool pump_allowed;     /* `actuation_verdict_allows` del veredicto vigente */
    bool heater_allowed;
    bool identification_active;
    bool network_ready;    /* asociada a la red y con dirección IP */
    bool source_configured;
    bool in_progress;
} firmware_update_conditions_t;

firmware_update_verdict_t firmware_update_decide(const firmware_update_conditions_t *conditions);

/* Si una actualización ya empezada puede seguir con la máquina en `state`. */
bool firmware_update_may_continue(operational_state_t state);

const char *firmware_update_verdict_name(firmware_update_verdict_t verdict);

/* Lo que el computador declara al publicar. */
typedef struct {
    char project[FIRMWARE_UPDATE_PROJECT_BYTES];
    char version[FIRMWARE_UPDATE_VERSION_BYTES];
    uint32_t size_bytes;
    uint8_t digest[FIRMWARE_UPDATE_DIGEST_BYTES];
} firmware_manifest_t;

typedef enum {
    /* El cero no es «válido»: un manifiesto sin leer no autoriza a escribir. */
    FIRMWARE_MANIFEST_UNREAD = 0,
    FIRMWARE_MANIFEST_OK,
    FIRMWARE_MANIFEST_MALFORMED,     /* falta un campo o no tiene su forma */
    FIRMWARE_MANIFEST_WRONG_PROJECT, /* es de otro binario */
    FIRMWARE_MANIFEST_TOO_LARGE,     /* no cabe en la partición libre */
    FIRMWARE_MANIFEST_ALREADY_RUNNING,
} firmware_manifest_status_t;

/*
 * Lee el manifiesto y lo contrasta con lo que corre.
 *
 * Formato, un objeto JSON plano con cuatro campos:
 *
 *   {"project":"…","version":"…","size":1022656,"imageSha256":"<64 hex>"}
 *
 * `imageSha256` es la huella de validación que ESP-IDF anexa a la imagen, la
 * misma que devuelve `esp_partition_get_sha256` sobre la partición escrita; no
 * la del archivo entero. `running_digest` es la de la imagen que corre: si
 * coinciden, no hay nada que instalar.
 */
firmware_manifest_status_t firmware_manifest_parse(const char *json,
                                                   const char *running_project,
                                                   const uint8_t running_digest[FIRMWARE_UPDATE_DIGEST_BYTES],
                                                   uint32_t partition_bytes,
                                                   firmware_manifest_t *manifest);

/* Si la imagen escrita es la que el manifiesto declaró. */
bool firmware_manifest_digest_matches(const firmware_manifest_t *manifest,
                                      const uint8_t written_digest[FIRMWARE_UPDATE_DIGEST_BYTES]);

const char *firmware_manifest_status_name(firmware_manifest_status_t status);
