#pragma once

/*
 * Ensayo hidráulico E2 con las dos actas aprobadas (enmienda a ADR-007 § 5 del
 * 2026-10-16; puesta en servicio 9.3).
 *
 * E2 mide la curva presión-caudal de la bomba: cada restricción fija da un
 * punto, y cada orden de bomba una curva. Para eso la bomba tiene que trabajar
 * a una orden fija, sin el PI, porque el PI busca una presión y lo que se mide
 * es la presión que sale. El modo restringido de ADR-007 § 5 no sirve: se niega
 * a actuar sobre un lazo con acta, y su confirmación en el mando existe para
 * actuar sobre una máquina sin poner en servicio. Esta ya lo está.
 *
 * Aquí la orden se elige por el puerto de servicio con `H,order,<porcentaje>`
 * entre las tres que el binario declara, y se aplica durante la extracción
 * siguiente. Lo que puede equivocarse está en este módulo:
 *
 * - **solo las órdenes declaradas**, en cualquier orden y repetidas cuantas
 *   veces haga falta: E2 recorre cada restricción con las tres, y las tres
 *   ya se vieron en la máquina, así que la regla de la escalera (solo repetir
 *   o subir) no protege nada aquí;
 * - **ninguna por encima del techo del acta**, que es la zona de la bomba que
 *   el acta ya vio; se comprueba al configurar, no en cada ciclo;
 * - **cada orden exige una pulsación de café nueva**: se elige con la bomba
 *   quieta y se fija al abrir la extracción, de modo que cambiarla a mitad de
 *   una corrida no cambia esa corrida;
 * - **sin orden elegida, la bomba no se mueve**: el cero es el estado
 *   conservador, y una extracción abierta sin orden queda registrada con la
 *   bomba en cero.
 *
 * ## Qué no hace
 *
 * - **No autoriza nada.** La bomba la sigue permitiendo el guardián, con la
 *   máquina de estados y los límites del acta, igual que en operación.
 * - **No toca la temperatura.** El calentador lo regula su acta.
 * - **No entra por la red.** Solo el puerto USB, con el operador delante.
 *
 * Puro: no conoce el reloj, el puerto ni el guardián.
 */

#include <stdbool.h>
#include <stdint.h>

#include "operational_fsm.h"

#define HYDRAULIC_TEST_ORDERS 3U

typedef enum {
    /* El cero es «esta línea no me incumbe». */
    HYDRAULIC_TEST_COMMAND_NONE = 0,
    HYDRAULIC_TEST_COMMAND_SHOW,
    HYDRAULIC_TEST_COMMAND_ORDER,
    HYDRAULIC_TEST_COMMAND_INVALID,
} hydraulic_test_command_kind_t;

typedef struct {
    hydraulic_test_command_kind_t kind;
    uint8_t percent;
} hydraulic_test_command_t;

typedef enum {
    /* El cero es el rechazo: un resultado sin escribir no acepta nada. */
    HYDRAULIC_TEST_REJECTED_NOT_CONFIGURED = 0,
    HYDRAULIC_TEST_ACCEPTED,
    HYDRAULIC_TEST_REJECTED_NOT_DECLARED,
    HYDRAULIC_TEST_REJECTED_PUMP_RUNNING,
    HYDRAULIC_TEST_REJECTED_COMMAND,
} hydraulic_test_result_t;

typedef struct {
    bool configured;
    uint8_t orders[HYDRAULIC_TEST_ORDERS];
    uint8_t selected;   /* la elegida para la próxima extracción; 0, ninguna */
    uint8_t applied;    /* la fijada al abrir la extracción en curso */
    bool extracting;
} hydraulic_test_t;

/*
 * Prepara el ensayo con las órdenes del binario. Rechaza —y deja el ensayo sin
 * configurar, sin bomba— órdenes nulas, repetidas o por encima del techo.
 */
bool hydraulic_test_configure(hydraulic_test_t *test,
                              const uint8_t orders[HYDRAULIC_TEST_ORDERS],
                              float ceiling_percent);

hydraulic_test_command_t hydraulic_test_parse(const char *line);

/* Elige la orden de la próxima extracción, solo con la bomba sin autorizar. */
hydraulic_test_result_t hydraulic_test_select(hydraulic_test_t *test, uint8_t percent,
                                              operational_state_t state);

/*
 * Avanza con el estado de este ciclo y devuelve la orden que hay que aplicar:
 * la fijada al abrir la extracción mientras dure, cero fuera de ella.
 */
uint8_t hydraulic_test_step(hydraulic_test_t *test, operational_state_t state);

const char *hydraulic_test_result_name(hydraulic_test_result_t result);
