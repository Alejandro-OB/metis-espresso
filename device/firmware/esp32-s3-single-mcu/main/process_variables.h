#pragma once

/*
 * Origen único de las variables del proceso para el resto del firmware.
 *
 * Decide de dónde provienen la temperatura y la presión, y **declara siempre el
 * origen**: si hay medida real y es apta, manda ella; si no, la planta simulada,
 * marcada como tal. Nunca se mezcla en silencio lo medido con lo simulado, ni se
 * presenta una simulación como medición.
 *
 * El respaldo por simulación solo opera mientras los sensores no hayan
 * funcionado nunca. Después de una primera medida apta, su fallo se entrega tal
 * cual, con su calidad no apta, para que el supervisor detenga la máquina en
 * lugar de recibir una planta ficticia sin enterarse.
 */

#include <stdbool.h>
#include <stdint.h>

#include "signal_quality.h"

typedef enum {
    PROCESS_SOURCE_NONE = 0,   /* el cero no puede significar «medido» */
    PROCESS_SOURCE_SIMULATED,
    PROCESS_SOURCE_MEASURED,
} process_source_t;

typedef struct {
    float temperature_c;
    float pressure_bar;
    signal_quality_t temperature_quality;
    signal_quality_t pressure_quality;
    uint64_t temperature_at_ms;   /* instante propio de cada canal */
    uint64_t pressure_at_ms;
    process_source_t source;
    const char *calibration_id;   /* NULL cuando la fuente es simulada */
} process_snapshot_t;

/*
 * Entrega el estado vigente del proceso. Devuelve false si no hay ninguna
 * fuente disponible, en cuyo caso el llamante no debe suponer nada.
 */
bool process_variables_get(process_snapshot_t *snapshot);

/* Indica si la última consulta provino de sensores reales. */
bool process_variables_measured(void);

const char *process_source_name(process_source_t source);
