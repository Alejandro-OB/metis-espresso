#pragma once

/*
 * Marca de preparación abierta (ADR-005 § 3, RNF-006).
 *
 * La preparación vive en RAM hasta que se cierra y solo entonces va a la cola
 * persistente. Un corte de energía a mitad de ella la borraba sin dejar rastro:
 * ni el dispositivo ni la plataforma sabían que había existido, aunque el
 * barista la hubiera hecho. Lo comprobó la revisión del código antes de la
 * prueba 7.11 del 2026-10-12, que por eso no se hizo.
 *
 * Al abrir una preparación se guarda esta marca en NVS, en la misma escritura
 * que su número; al cerrarla se borra. Si el arranque siguiente la encuentra,
 * la preparación se cortó, y este módulo la convierte en un registro cerrado
 * con causa `unexpected-power-loss` que sigue el camino de cualquier otro.
 *
 * La marca guarda lo que hay **al abrir** —perfil, origen de la medida,
 * calibración, firmware, hora de pared— porque al recuperarla el dispositivo
 * ya arrancó otra vez, y la calibración o el firmware vigentes pueden no ser
 * los que midieron.
 *
 * ## Qué no hace
 *
 * - **No inventa el final.** No consta cuándo se cortó la energía: el registro
 *   recuperado no tiene duración ni hora de fin, y no la hora del rearranque,
 *   que afirmaría una duración que nadie midió.
 * - **No recupera muestras.** Vivían en RAM. El registro declara cero.
 * - **No escribe en NVS ni conoce el reloj.** Construye y valida la marca;
 *   guardarla y leerla es de `extraction_runtime`.
 * - Se descartó actualizar la marca durante la preparación para acotar la
 *   duración: cada escritura en flash detiene la caché, y los dos plazos
 *   incumplidos del 2026-10-12 coinciden con las escrituras del cierre.
 */

#include <stdbool.h>
#include <stdint.h>

#include "extraction_profile.h"
#include "extraction_session.h"
#include "sensor_calibration.h"

/* Cambiarla invalida las marcas guardadas con el formato anterior, que se
 * descartan: leerlas con otra disposición daría un registro falso. */
#define OPEN_EXTRACTION_FORMAT_VERSION UINT32_C(1)
#define OPEN_EXTRACTION_FIRMWARE_LENGTH 32U

typedef enum {
    /* El cero es «nadie declaró el origen»: no se archiva como medida. */
    OPEN_EXTRACTION_SOURCE_UNKNOWN = 0,
    OPEN_EXTRACTION_SOURCE_MEASURED,
    OPEN_EXTRACTION_SOURCE_SIMULATED,
} open_extraction_source_t;

typedef struct {
    uint32_t format_version;            /* 0 = no hay marca */
    char session_id[EXTRACTION_SESSION_ID_LENGTH];
    extraction_profile_t profile;
    uint64_t started_epoch_ms;          /* 0 = sin hora de pared al abrir */
    open_extraction_source_t source;
    char calibration_id[SENSOR_CALIBRATION_ID_LENGTH];  /* vacío = no consta */
    uint32_t calibration_version;
    char firmware_version[OPEN_EXTRACTION_FIRMWARE_LENGTH];  /* vacío = no consta */
} open_extraction_marker_t;

typedef struct {
    const char *session_id;
    const extraction_profile_t *profile;
    uint64_t started_epoch_ms;
    open_extraction_source_t source;
    const char *calibration_id;         /* NULL = no consta */
    uint32_t calibration_version;
    const char *firmware_version;       /* NULL = no consta */
} open_extraction_opening_t;

/*
 * Llena la marca de la preparación que se abre. Devuelve false, con la marca
 * en cero, si falta el identificador o el perfil, o si un texto no cabe: una
 * marca truncada recuperaría otra preparación.
 */
bool open_extraction_mark(open_extraction_marker_t *marker,
                          const open_extraction_opening_t *opening);

/*
 * Una marca leída de NVS solo vale si es de este formato, tiene identificador
 * y todos sus textos terminan dentro de su campo. Lo demás se descarta.
 */
bool open_extraction_is_valid(const open_extraction_marker_t *marker);

/*
 * Convierte una marca válida en el resumen de una preparación cerrada por
 * corte: causa `unexpected-power-loss`, cero muestras, ventana sin determinar
 * y duración cero, que el mensaje no envía como duración (ver
 * `extraction_message`). Devuelve false si la marca no es válida.
 */
bool open_extraction_recover(const open_extraction_marker_t *marker,
                             extraction_summary_t *summary);
