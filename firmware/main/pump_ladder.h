#pragma once

/*
 * Escalera de la bomba en un solo binario (enmienda a ADR-007 § 5 del
 * 2026-10-10; puesta en servicio, etapa 6).
 *
 * La enmienda del 2026-09-21 hizo de cada escalón una secuencia propia para que
 * subir exigiera compilar otra: nadie pasaba al siguiente sin haber mirado la
 * respuesta del anterior. Sobre la máquina real eso costaba unos 8 min por
 * escalón —desenchufar para grabar, reiniciar, volver a confirmar, recalentar
 * la caldera con la integral a cero— para 12 s de bomba.
 *
 * Aquí la escalera es una tabla fija y el operador elige el escalón por el
 * puerto de servicio con `B,step,<porcentaje>`. Lo que la regla protegía se
 * conserva en este módulo, que es el que puede equivocarse:
 *
 * - **solo se elige con la secuencia anterior terminada entera**, no abortada
 *   ni en curso: la respuesta que se mira tiene que existir;
 * - **solo el mismo escalón o el siguiente**, nunca saltar: repetir sirve para
 *   la partición de calibración y validación (6.9), saltar es lo que la regla
 *   prohíbe;
 * - **cada escalón exige una pulsación de café nueva**: el elegido solo se arma
 *   con la máquina en un estado que no autoriza la bomba. Una secuencia armada
 *   arranca en cuanto el guardián lo permite, y la extracción dura más que la
 *   secuencia: armarlo con ella abierta lo arrancaba sin que nadie pulsara
 *   café. Ocurrió el 2026-10-10 con el 3 %, que la placa cortó a los 6,6 s al
 *   cerrarse esa extracción.
 *
 * ## Qué no hace
 *
 * - **No confirma nada.** La confirmación física en el mando sigue siendo una
 *   por arranque y la exige `identification_mode`.
 * - **No sube el tope.** El guardián juzga cada escalón contra el tope
 *   compilado, como siempre.
 * - **No entra por la red.** Solo el puerto USB, con el operador delante.
 * - **No reanuda tras un aborto.** Si el guardián cortó una secuencia, la
 *   sesión termina como hasta ahora.
 *
 * ## Dos escaleras
 *
 * La **hidráulica** (1–75 %, escalones de 6 s) es la de la etapa 6. La
 * **térmica** (10, 30 y 50 %, escalones de 25 s) es la de la 7.9: mide cuánto
 * calor se lleva el agua que entra, y en 6 s el sensor del cuerpo no alcanza a
 * verlo. Las reglas son las mismas en las dos.
 *
 * Puro: no conoce el reloj, el puerto ni el guardián.
 */

#include <stdbool.h>
#include <stdint.h>

#include "identification_mode.h"
#include "stimulus_sequence.h"

#define PUMP_LADDER_STEPS 9U

typedef enum {
    /* El cero es «esta línea no me incumbe». */
    PUMP_LADDER_COMMAND_NONE = 0,
    PUMP_LADDER_COMMAND_SHOW,
    PUMP_LADDER_COMMAND_STEP,
    PUMP_LADDER_COMMAND_INVALID,
} pump_ladder_command_kind_t;

typedef struct {
    pump_ladder_command_kind_t kind;
    uint8_t percent;
} pump_ladder_command_t;

typedef enum {
    /* El cero es el rechazo: una estructura sin rellenar no autoriza nada. */
    PUMP_LADDER_REJECTED_NOT_CONFIGURED = 0,
    PUMP_LADDER_ACCEPTED,
    PUMP_LADDER_REJECTED_NOT_FINISHED,
    PUMP_LADDER_REJECTED_NOT_IN_LADDER,
    PUMP_LADDER_REJECTED_SKIPS_A_STEP,
} pump_ladder_result_t;

typedef enum {
    PUMP_LADDER_HYDRAULIC = 0,
    PUMP_LADDER_THERMAL,
    /* Un solo escalón, el techo de la sintonía de presión, que se repite una
     * extracción por prueba (enmienda a ADR-007 § 5 del 2026-10-12). */
    PUMP_LADDER_PRESSURE_TUNING,
} pump_ladder_variant_t;

typedef struct {
    bool configured;
    pump_ladder_variant_t variant;
    uint8_t current;   /* índice del escalón armado o recorrido */
} pump_ladder_t;

/* Interpreta `B,show` o `B,step,<porcentaje>`. Otra línea devuelve NONE. */
pump_ladder_command_t pump_ladder_parse(const char *line);

/* Empieza en el primer escalón, que es la secuencia con la que arranca el binario. */
void pump_ladder_init(pump_ladder_t *ladder);
void pump_ladder_init_variant(pump_ladder_t *ladder, pump_ladder_variant_t variant);

/*
 * Decide si se puede armar el escalón pedido, con la fase del modo
 * restringido. Si se acepta, deja el escalón como actual y devuelve en
 * `sequence` la secuencia que hay que armar.
 */
pump_ladder_result_t pump_ladder_select(pump_ladder_t *ladder, uint8_t percent,
                                        identification_phase_t phase,
                                        stimulus_sequence_id_t *sequence);

/*
 * Si el escalón ya elegido puede armarse en este estado. Solo en los que no
 * autorizan la bomba —apagada, calentando, lista—, para que arranque con una
 * pulsación de café posterior y nunca con una extracción o una purga que ya
 * estaban abiertas. En alarma tampoco: no hay nada que preparar ahí.
 */
bool pump_ladder_may_arm(operational_state_t state);

/* Porcentaje del escalón actual, o 0 sin configurar. */
uint8_t pump_ladder_current_percent(const pump_ladder_t *ladder);

/* La secuencia del primer escalón, con la que arranca el binario. */
stimulus_sequence_id_t pump_ladder_first_sequence(void);
stimulus_sequence_id_t pump_ladder_variant_first_sequence(pump_ladder_variant_t variant);

const char *pump_ladder_result_name(pump_ladder_result_t result);
