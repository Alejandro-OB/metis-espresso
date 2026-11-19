#pragma once

/*
 * Devolución de la integral del PI térmico a su valor de antes del café
 * (ADR-009, segunda enmienda del 2026-10-16; puesta en servicio 7.6).
 *
 * Tras cada café el metal se pasa a 93,0–93,9 °C hacia los 8–10 min: la
 * integral sube durante la recuperación, porque hace falta calor para el agua
 * nueva, y no baja cuando el agua ya alcanzó al metal. Retenerla del todo (la
 * primera enmienda del 16) quitó el sobrepaso pero dejó el metal bajo la
 * referencia 554 s, porque mientras el agua se calienta sí hace falta más.
 *
 * Aquí la integral crece libre durante el café y la recuperación, y desde que
 * para la bomba se devuelve al valor que tenía al arrancarla, con la constante
 * de tiempo del equilibrio agua–metal:
 *
 *     dI/dt = Ki·e − (I − I0)/τ_eq      durante 3·τ_eq desde la parada
 *
 * Es la observación del director de la tesis (el agua y el metal se igualan, y
 * después hace falta menos calor), con τ_eq = 533 s del modelo de tres nodos
 * ajustado a los cafés del 15 y 16 (evidencias/cafe-temperatura-20261016/
 * sobrepaso/informe.md); el sumidero medido sin modelo da 568 ± 217 s. Es un
 * número provisional y sale del acta, o de la opción de ensayo.
 *
 * ## Qué no hace
 *
 * - **No toca la sintonía.** Kp, Ki, el techo y el filtro siguen siendo los
 *   del acta; la integral sigue sumando Ki·e cada paso.
 * - **No renueva I0 si un café llega con la devolución en curso:** el valor
 *   de antes del primer café sigue siendo el sostenimiento conocido.
 * - **No actúa después de 3·τ_eq** (el 95 % de una exponencial): pasado ese
 *   tiempo, la integral vuelve a ser solo la del PI, que corrige cualquier
 *   cambio de demanda que fuera real.
 *
 * Puro: no conoce la bomba, el reloj ni el calentador. Con τ en cero, que es
 * lo que deja una estructura sin configurar, no actúa nunca.
 */

#include <stdbool.h>

typedef struct {
    float tau_s;
    bool pumping;
    bool active;
    bool latched;
    float i0;
    float elapsed_s;
} heater_recovery_return_t;

typedef struct {
    bool apply;       /* en este paso hay que acercar la integral a target */
    float target;     /* I0 */
    float fraction;   /* parte de la distancia a recorrer en este paso, dt/τ */
} heater_recovery_return_decision_t;

/* Fija τ_eq. Cero apaga la devolución; un valor negativo o no finito se
 * rechaza y la deja apagada. */
bool heater_recovery_return_configure(heater_recovery_return_t *recovery, float tau_s);

/* Olvida el ciclo en curso (lazo inhibido, arranque rápido, sin medida). */
void heater_recovery_return_reset(heater_recovery_return_t *recovery);

/*
 * Un paso del lazo térmico. `integral` es la integral del PI tras este paso;
 * `elapsed_s`, el periodo del lazo.
 */
heater_recovery_return_decision_t heater_recovery_return_step(heater_recovery_return_t *recovery,
                                                              bool pump_allowed, float integral,
                                                              float elapsed_s);
