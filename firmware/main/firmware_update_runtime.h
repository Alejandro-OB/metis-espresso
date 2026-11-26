#pragma once

/*
 * Envoltura de la actualización del firmware por Wi-Fi (ADR-021).
 *
 * Toda decisión vive en `firmware_update`, que es puro y se prueba en el
 * anfitrión. Aquí solo se reúnen las condiciones, se baja el manifiesto y la
 * imagen del computador, se escriben en la partición libre y se arranca la
 * nueva.
 *
 * ## Dos copias y vuelta atrás
 *
 * La memoria tiene dos particiones de programa (`ota_0`, `ota_1`). La nueva
 * imagen se escribe en la que no corre; la que corre no se toca hasta que la
 * nueva demuestra que funciona. El cargador de arranque lleva la vuelta atrás
 * activada: la imagen recién instalada arranca «a prueba» y, si se reinicia
 * antes de declararse válida, el cargador vuelve solo a la anterior.
 *
 * La declaración la hace `firmware_update_runtime_start` en su propia tarea,
 * cuando se cumple lo mínimo para volver a actualizar sin USB: la supervisión
 * corre y el guardián ya decidió, y la placa obtuvo dirección en la red. Si eso
 * no ocurre en `FIRMWARE_UPDATE_CONFIRM_TIMEOUT_MS`, se marca inválida y se
 * vuelve a la anterior. Con la cafetera sellada, una imagen que arranca pero
 * no alcanza la red ya no se podría reemplazar: por eso la red forma parte de
 * la prueba.
 *
 * ## Lo que no hace
 *
 * - **No escucha la red.** La única forma de empezar es el gesto del mando
 *   (`firmware_update_runtime_request`). La placa pregunta; nadie le ordena.
 * - **No pasa por la plataforma externa.** El manifiesto y la imagen los sirve
 *   el computador de desarrollo (`docs/alcance-produccion-plataforma-externa.md`
 *   § 9: el firmware no pasa por la Raspberry).
 * - **No energiza nada.** Mientras dura, el guardián corta las dos salidas
 *   (`supervisor_runtime_set_updating`).
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "firmware_update.h"

/*
 * Plazo para que una imagen recién instalada demuestre que funciona. Dos
 * minutos cubren el diagnóstico de arranque y la asociación a la red con margen;
 * provisional, sin medida: si la red del laboratorio tardara más, la imagen
 * volvería atrás sin estar rota y habría que subirlo.
 */
#define FIRMWARE_UPDATE_CONFIRM_TIMEOUT_MS UINT32_C(120000)

typedef enum {
    /* El cero es «nada en marcha». */
    FIRMWARE_UPDATE_IDLE = 0,
    FIRMWARE_UPDATE_CONFIRMING,  /* imagen nueva a prueba tras el arranque */
    FIRMWARE_UPDATE_CHECKING,    /* bajando y leyendo el manifiesto */
    FIRMWARE_UPDATE_WRITING,     /* bajando y escribiendo la imagen */
    FIRMWARE_UPDATE_RESTARTING,
} firmware_update_phase_t;

typedef struct {
    firmware_update_phase_t phase;
    firmware_update_verdict_t last_verdict;
    firmware_manifest_status_t last_manifest;
    uint32_t written_bytes;
    uint32_t expected_bytes;
    /* Instante monotónico del último rechazo o fallo; 0, ninguno. El panel lo
     * muestra como cualquier botón negado. */
    uint64_t last_refused_us;
} firmware_update_status_t;

/*
 * Arranca la tarea. `core_started` dice si la adquisición y la supervisión
 * arrancaron: sin ellas una imagen a prueba no se declara válida.
 */
esp_err_t firmware_update_runtime_start(bool core_started);

/* Pide buscar un firmware nuevo. Lo llama el mando; no bloquea. */
void firmware_update_runtime_request(void);

bool firmware_update_runtime_get_status(firmware_update_status_t *status);

/* Si hay una actualización en marcha, para el panel. */
bool firmware_update_runtime_busy(void);

uint64_t firmware_update_runtime_last_refused_us(void);
