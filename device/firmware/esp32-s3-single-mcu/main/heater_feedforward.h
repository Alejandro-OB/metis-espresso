#pragma once

/*
 * Prealimentación del calentador por la entrada de agua fría, `ff-v1`
 * (ADR-009 § 3.4; puesta en servicio 7.9).
 *
 * Cuando la bomba arranca, entra agua fría en la caldera y el sensor, sobre el
 * cuerpo de aluminio, la ve tarde. La escalera del 2026-10-10 lo midió: al 50 %
 * la caldera cayó de 91,6 a 83 °C y el PI solo reaccionó cuando la caída ya
 * estaba. La perturbación, en cambio, se conoce de antemano: el firmware es
 * quien ordena la bomba. `ff-v1` suma potencia al calentador en proporción a la
 * densidad de la bomba desde ese mismo instante.
 *
 *     u_ff = ganancia · densidad    (solo con la bomba autorizada)
 *
 * La densidad mide el esfuerzo de la bomba, no el caudal: a igual densidad, una
 * cama más cerrada deja pasar menos agua. Es una aproximación declarada; la
 * versión con caudal estimado, `ff-v2`, llega con la báscula.
 *
 * ## Qué no hace
 *
 * - **No inventa la ganancia.** Sale del acta de temperatura, o en el binario
 *   de identificación de una opción provisional; sin ninguna vale cero, y con
 *   cero el calentador se comporta como antes. La 7.9 la midió en 1,15.
 * - **No pasa por el tope del PI.** Desde la enmienda del 2026-10-12 (las tres
 *   puertas) se suma después del PI, que satura solo su parte;
 *   `heater_feedforward_apply` recorta la suma al 100 %.
 * - **No se adelanta ni se queda después.** Sigue a la densidad de cada ciclo:
 *   sin bomba autorizada, cero.
 * - **No calienta si no entra agua.** La densidad dice que la bomba trabaja,
 *   no que pase agua. El 2026-10-17, con el depósito vacío, la bomba trabajó
 *   27 s en seco, la aportación tuvo el calentador al 59 % y el metal
 *   filtrado llegó a 112,3 °C (`evidencias/presion-ensayos/e3-2026-10-16.md`;
 *   ADR-009, enmienda del 2026-10-17 a la § 3.4). Si entra agua
 *   fría, el metal baja al arrancar la bomba. En los 190 bombeos registrados
 *   del 11 al 17 de octubre, el único donde subió más de 0,5 °C sobre el valor
 *   al pulsar fue ese, con +6,3 °C; con aportación, los demás no pasaron de
 *   +0,36 °C. `heater_feedforward_step` corta la aportación cuando el metal
 *   supera el valor al pulsar en más de la tolerancia con que el supervisor
 *   declara «lista» (`ready_tolerance_c`, 0,5 °C), y no la devuelve hasta el
 *   próximo arranque de la bomba.
 *   Con ese registro el corte habría llegado a los 19 s de 27: reduce el
 *   exceso, no lo evita. Se descartó la báscula, que no siempre está bajo
 *   el portafiltro, y la presión, que en seco (5,4–5,8 bar) quedó cerca de
 *   la de un purgado sin canastilla (6,8 bar al 75 %).
 *
 * Puro: no conoce la bomba, el reloj ni el calentador.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* Puntos de calentador por cada punto de densidad de la bomba. Cero, que es
     * lo que deja una estructura sin rellenar, es «sin prealimentación». */
    float gain;
    /* Estado del bombeo en curso: el metal al arrancar y si ya se vio que no
     * entra agua. A cero, «no hay bombeo en curso». */
    bool pumping;
    float start_temperature_c;
    bool no_water;
} heater_feedforward_t;

/*
 * Configura la ganancia. Rechaza —y deja la prealimentación apagada— una
 * ganancia negativa o no finita: ninguna puede venir de una medida.
 *
 * No rechaza una ganancia que a densidad máxima pida más del 100 %: la
 * primera versión lo hacía, y la 7.9 midió 1,15 % por punto de densidad, más
 * de lo que el calentador puede dar con la bomba a fondo. Pedir más de lo que
 * hay no es un error de la ganancia sino un límite del calentador, y la
 * saturación de `pi_controller` ya lo aplica sobre la suma.
 */
bool heater_feedforward_configure(heater_feedforward_t *feedforward, float gain);

/*
 * La ganancia que rige en un ciclo: la reforzada del primer uso si es mayor que
 * la del acta (ADR-009, enmienda del 2026-10-27), la del acta si no. Quién la
 * admitió y cuándo deja de regir lo decide `first_use_offset`; esto solo evita
 * que una ganancia ilegible o menor llegue al calentador.
 */
float heater_feedforward_gain_for(float base_gain, float reinforced_gain);

/* La aportación en % de calentador para la densidad dada (0–100 %). */
float heater_feedforward_output(const heater_feedforward_t *feedforward, uint8_t pump_density,
                                bool pump_allowed);

/*
 * La aportación de este ciclo, con el corte por falta de agua. Con la bomba
 * autorizada guarda `temperature_c` del primer ciclo como valor al pulsar; si
 * después la medida lo supera en más de `rise_limit_c`, o no es finita,
 * devuelve cero hasta que la bomba pare y vuelva a arrancar. Sin bomba
 * autorizada olvida el bombeo y devuelve cero; con densidad cero, cero, sin
 * olvidarlo. Un `rise_limit_c` no finito o
 * no positivo corta siempre: sin tolerancia no se puede comprobar el agua.
 */
float heater_feedforward_step(heater_feedforward_t *feedforward, uint8_t pump_density,
                              bool pump_allowed, float temperature_c, float rise_limit_c);

/* Si el bombeo en curso quedó sin aportación por falta de agua. */
bool heater_feedforward_no_water(const heater_feedforward_t *feedforward);

/* Olvida el bombeo en curso sin tocar la ganancia. */
void heater_feedforward_reset(heater_feedforward_t *feedforward);

/*
 * Puerta 2 (ADR-009, enmienda del 2026-10-12): suma la aportación a la salida
 * del PI **ya limitada por su tope**, y recorta la suma al 100 %. El PI no ve la
 * prealimentación: su integral responde solo a su error y su anti-*windup* actúa
 * contra su propio tope. Sin aportación devuelve la salida del PI tal cual.
 */
float heater_feedforward_apply(float pi_output, float feedforward_output);
