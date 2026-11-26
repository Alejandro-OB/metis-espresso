#pragma once

/*
 * Retención de la integral del PI térmico durante la extracción y la
 * recuperación (ADR-009, enmienda del 2026-10-16; puesta en servicio 7.6).
 *
 * El 15 de octubre el metal se pasó a 93,3–93,9 °C hacia los 9 min de cada
 * café, con y sin prealimentación. El registro lo explicó: mientras la caldera
 * se recupera, la integral sube 0,8–1,5 puntos sobre la potencia de
 * sostenimiento que ya tenía aprendida, y al cruzar la referencia se descarga a
 * 0,07 puntos cada 30 s; el proporcional no alcanza a compensarla
 * (evidencias/cafe-temperatura-2026-10-15.md, «Sobrepaso tras el café»).
 *
 * La caída del café no cambia la potencia que sostiene la caldera: es agua fría
 * que entra y se va. Por eso la integral se retiene desde que la bomba queda
 * autorizada hasta que la medida vuelve a la referencia. De la recuperación se
 * encargan el proporcional y la prealimentación; al llegar, la integral sigue
 * en el sostenimiento y no hay sobrante que descargar.
 *
 * ## Qué no hace
 *
 * - **No toca la sintonía.** Kp, Ki, el tope y el filtro siguen siendo los del
 *   acta. Subir Kp quitaba el sobrepaso en un modelo y oscilaba en el que tiene
 *   el retardo de 44 s; subir Ki lo empeoraba (misma evidencia, «simulación de
 *   arreglos»).
 * - **No vacía ni precarga la integral.** La deja como estaba. Vaciarla al
 *   cruzar dejaría la caldera sin su potencia de sostenimiento, como ya se
 *   descartó para el traspaso del arranque rápido.
 * - **No decide con un número nuevo.** Suelta al cruzar la referencia
 *   autorizada, sin margen ni tiempo.
 *
 * Puro: no conoce la bomba, el reloj ni el calentador. El cero de la estructura
 * es «sin retener», que es como el PI se comporta sin este módulo.
 */

#include <stdbool.h>

typedef struct {
    bool holding;
} heater_recovery_hold_t;

void heater_recovery_hold_reset(heater_recovery_hold_t *hold);

/*
 * Decide si en este paso se retiene la integral. Retiene mientras la bomba esté
 * autorizada y, tras pararla, hasta que la medida alcance la referencia. Una
 * medida o referencia no finita no suelta la retención: con un dato imposible
 * no se aprende nada.
 */
bool heater_recovery_hold_step(heater_recovery_hold_t *hold, bool pump_allowed,
                               float setpoint_c, float measurement_c);
