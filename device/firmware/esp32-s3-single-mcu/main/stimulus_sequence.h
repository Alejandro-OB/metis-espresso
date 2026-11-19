#pragma once

/*
 * Secuencias de estímulo del modo restringido (ADR-007 § 5, enmienda del
 * 2026-09-21; puesta en servicio, etapas 5 y 6).
 *
 * Una secuencia es una tabla fija de escalones —demanda y duración— que se
 * elige al compilar y lleva su versión en el nombre. El operador no mueve la
 * salida: la mueve la secuencia, que termina sola. Un humano apretando un botón
 * produce flancos que no se repiten ni se fechan con precisión, y la
 * identificación vive justamente de la forma del flanco.
 *
 * ## Una secuencia por escalón
 *
 * La enmienda manda empezar por la escalera 1, 2, 3, 5 y 10 % y no subir de
 * escalón mientras el anterior no haya dado una respuesta legible sobre el
 * ruido de reposo. Una sola secuencia que recorriera la escalera entera subiría
 * sin que nadie hubiera mirado el escalón anterior, así que **cada escalón es
 * una secuencia propia** y subir exige compilar otra. Es más incómodo y es lo
 * que la regla pide.
 *
 * ## Los números de esta tabla no están medidos
 *
 * Las duraciones son **provisionales** y salen del modelo de banco de
 * `control_plant`, no de la máquina: la planta real es precisamente lo que se
 * va a medir. Se dimensionan con holgura sobre ese modelo y se revisan después
 * del primer registro real, publicando una versión nueva; nunca editando la
 * existente, porque un registro que dice `pump-step-1pct-v1` debe significar
 * siempre lo mismo.
 *
 * Este módulo es puro: no conoce el reloj ni el guardián. Dice qué demanda
 * corresponde a un instante y si una secuencia está bien formada.
 */

#include <stdbool.h>
#include <stdint.h>

#include "actuation_guard.h"

/*
 * Identificador versionado. El cero es «ninguna secuencia», que es lo que tiene
 * el binario de operación.
 *
 * Se añaden al final y no se reutilizan: el registro guarda el nombre, pero el
 * código es lo que viaja en la línea de evento.
 */
typedef enum {
    STIMULUS_SEQUENCE_NONE = 0,
    STIMULUS_SEQUENCE_HEATER_STEP_1PCT_V1,
    STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V1,
    STIMULUS_SEQUENCE_HEATER_STEP_3PCT_V1,
    STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V1,
    STIMULUS_SEQUENCE_HEATER_STEP_10PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_3PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_5PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_10PCT_V1,
    STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V2,
    STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V2,
    STIMULUS_SEQUENCE_PUMP_STEP_20PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_30PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_50PCT_V1,
    STIMULUS_SEQUENCE_PUMP_STEP_75PCT_V1,
    STIMULUS_SEQUENCE_HEATER_TUNING_10PCT_V1,
    STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_50PCT_V1,
    STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_100PCT_V1,
    STIMULUS_SEQUENCE_HEATER_PULSE_100PCT_V1,
    STIMULUS_SEQUENCE_PUMP_THERMAL_10PCT_V1,
    STIMULUS_SEQUENCE_PUMP_THERMAL_30PCT_V1,
    STIMULUS_SEQUENCE_PUMP_THERMAL_50PCT_V1,
    STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1,
    STIMULUS_SEQUENCE_COUNT,
} stimulus_sequence_id_t;

typedef struct {
    uint8_t demand_percent;
    uint32_t duration_ms;
} stimulus_step_t;

#define STIMULUS_SEQUENCE_MAXIMUM_STEPS 4U

typedef struct {
    stimulus_sequence_id_t id;
    identification_target_t target;
    uint32_t step_count;
    stimulus_step_t steps[STIMULUS_SEQUENCE_MAXIMUM_STEPS];
} stimulus_sequence_t;

/* NULL si el identificador no corresponde a ninguna secuencia declarada. */
const stimulus_sequence_t *stimulus_sequence_find(stimulus_sequence_id_t id);

const char *stimulus_sequence_name(stimulus_sequence_id_t id);

/* Suma de las duraciones; 0 si la secuencia no existe. */
uint32_t stimulus_sequence_duration_ms(const stimulus_sequence_t *sequence);

/* La mayor demanda de la tabla. */
uint8_t stimulus_sequence_peak_percent(const stimulus_sequence_t *sequence);

/*
 * Bien formada: tiene salida, al menos un escalón, ninguno de duración nula y
 * ninguna demanda por encima del 100 %. No compara con el tope: eso lo hace
 * quien arma el modo, y lo vuelve a hacer el guardián en cada ciclo.
 */
bool stimulus_sequence_is_valid(const stimulus_sequence_t *sequence);

/*
 * Demanda que corresponde a `elapsed_ms` desde el inicio.
 *
 * Fuera de la secuencia —antes de empezar o una vez agotada— devuelve 0: una
 * secuencia terminada no deja nada encendido.
 */
uint8_t stimulus_sequence_demand_at(const stimulus_sequence_t *sequence,
                                    uint32_t elapsed_ms);
