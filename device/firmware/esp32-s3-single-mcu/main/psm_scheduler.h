#pragma once

/*
 * Planificador por densidad de pulsos para la bomba (ADR-007 § 3.4).
 *
 * La bomba se gobierna aplicando u omitiendo **actuaciones útiles completas**,
 * nunca recortando una onda por ángulo de fase. Este módulo decide, en cada
 * oportunidad sincronizada con `PUMP_ZC`, si esa oportunidad se usa o se deja
 * pasar, de modo que la fracción de oportunidades aprovechadas siga a la demanda
 * del controlador.
 *
 * El algoritmo es un acumulador: en cada oportunidad se suma la densidad
 * solicitada y se emite cuando el acumulador alcanza el 100 %, restándolo. Es el
 * mismo principio que el trazado de rectas de Bresenham, y tiene dos propiedades
 * que importan aquí:
 *
 * **No hay deriva.** Cada 100 oportunidades se emiten exactamente tantas
 * actuaciones como indica la densidad, sin error acumulado. Por eso el
 * acumulador es entero y no de coma flotante: un residuo fraccionario
 * arrastrado durante horas de funcionamiento terminaría desplazando la densidad
 * real respecto a la solicitada, y ese sesgo sería indistinguible de un error de
 * calibración.
 *
 * **La distribución es la más uniforme posible.** Entre dos actuaciones
 * consecutivas, el hueco nunca difiere en más de una oportunidad respecto al
 * siguiente. Importa porque agrupar las actuaciones en ráfagas produciría el
 * mismo promedio con un rizado de presión mucho mayor.
 *
 * **La actuación útil es un ciclo completo, no un semiciclo** (enmienda a
 * ADR-007 § 3.4 del 2026-10-11). La bomba lleva un diodo en serie y solo trabaja
 * con los semiciclos de una polaridad. Decidiendo por semiciclo, toda densidad
 * cuyo periodo es par —1, 2, 5, 10, 50 %— dejaba caer todas las actuaciones en
 * la misma polaridad, y cuál dependía del instante de arranque: la mitad de las
 * veces la bomba no recibía nada en todo el escalón aunque el registro contara
 * cada disparo. Se vio en la escalera de la etapa 6 el 2026-10-10: el 10 % y el
 * 50 % respondían en una sesión y callaban en la siguiente, y el 3, 20, 30 y
 * 75 % nunca fallaron. Por eso `psm_scheduler_half_cycle` decide en el primer
 * semiciclo de cada pareja y repite la decisión en el segundo: cada actuación
 * lleva las dos polaridades y la que conduce siempre está. El acumulador cuenta
 * ciclos, de modo que las propiedades de abajo valen por ciclo.
 *
 * ADR-007 fija la resolución solicitada en 1 % y no impone densidad mínima: una
 * solicitud de 0 % no genera disparos, y si durante la puesta en marcha aparece
 * una zona sin respuesta se documentará como característica de la planta, sin
 * alterar el significado de 0 %.
 */

#include <stdbool.h>
#include <stdint.h>

#define PSM_DENSITY_FULL_SCALE 100U

typedef struct {
    uint32_t accumulator;    /* residuo, siempre por debajo del fondo de escala */
    uint32_t opportunities;  /* ciclos ofrecidos desde el último reinicio */
    uint32_t emitted;        /* ciclos emitidos, para el registro */
    bool second_half;        /* el próximo semiciclo cierra un ciclo ya decidido */
    bool cycle_emits;        /* la decisión tomada en su primer semiciclo */
} psm_scheduler_t;

void psm_scheduler_reset(psm_scheduler_t *scheduler);

/*
 * Lleva una demanda del controlador a la escala del planificador.
 *
 * Acota a [0, 100] y redondea al entero más próximo, que es la resolución que
 * ADR-007 § 3.3 deja cerrada. Una demanda no finita se traduce como 0: ante un
 * número imposible, la única respuesta defendible es no actuar.
 */
uint8_t psm_scheduler_quantize(float demand_percent);

/*
 * Ofrece una oportunidad de actuación y responde si debe aprovecharse.
 *
 * `density` viene ya cuantizada. Los valores por encima del fondo de escala se
 * tratan como 100 %, de modo que una demanda mal formada nunca produce una
 * densidad mayor que la máxima.
 */
bool psm_scheduler_step(psm_scheduler_t *scheduler, uint8_t density);

/*
 * Ofrece un semiciclo, que es lo que entrega cada cruce por cero, y responde si
 * la salida debe dispararse en él.
 *
 * En el primer semiciclo de cada pareja decide con `psm_scheduler_step`; en el
 * segundo repite esa decisión sin consultar el acumulador. La única excepción es
 * una densidad nula, que corta también en el segundo semiciclo y descarta la
 * pareja: cuando el guardián ordena cero, no se termina un ciclo empezado.
 */
bool psm_scheduler_half_cycle(psm_scheduler_t *scheduler, uint8_t density);

uint32_t psm_scheduler_emitted(const psm_scheduler_t *scheduler);
uint32_t psm_scheduler_opportunities(const psm_scheduler_t *scheduler);
uint32_t psm_scheduler_residue(const psm_scheduler_t *scheduler);
