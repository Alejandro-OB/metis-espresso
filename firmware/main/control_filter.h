#pragma once

/*
 * Filtro de primer orden para el interior del lazo de control.
 *
 * No sustituye a `signal_filter`, que es una mediana móvil: los dos trabajan en
 * cascada y responden a problemas distintos. La mediana rechaza impulsos —una
 * conmutación de carga, un pico de rizado— sin arrastrar el valor; este filtro
 * fija el ancho de banda con el que el controlador ve la planta. Una mediana no
 * tiene constante de tiempo declarable, y un controlador sintonizado necesita
 * una: es la diferencia entre despicar y filtrar.
 *
 * ADR-007 § 3.3 lo decide para la presión con `τf = 50 ms`, y ADR-009 § 3.6
 * para la temperatura con `τf = 1 s`. Ambos valores son iniciales y ajustables
 * con registros reales, de modo que el módulo recibe la constante y el periodo
 * en lugar de fijarlos.
 *
 * Forma recursiva, con `α = Ts / (τf + Ts)`:
 *
 *     y[k] = y[k-1] + α · (x[k] − y[k-1])
 *
 * Esa expresión de `α` es la discretización de Euler hacia atrás del filtro
 * continuo `1/(τf·s + 1)`. Se prefiere a la de Euler hacia adelante,
 * `α = Ts/τf`, porque mantiene `α` dentro de `(0, 1]` para cualquier periodo:
 * la versión hacia adelante supera la unidad cuando `Ts > τf` y el filtro
 * oscila en lugar de suavizar. Aquí eso importa porque el periodo del lazo es
 * un parámetro de puesta en marcha, no una constante del código.
 */

#include <stdbool.h>

typedef struct {
    float alpha;   /* coeficiente derivado de la constante y el periodo */
    float value;   /* última salida */
    bool primed;   /* ya recibió una muestra */
} control_filter_t;

/*
 * Deriva `α` de la constante de tiempo y el periodo de muestreo, ambos en
 * segundos, y deja el filtro sin cebar.
 *
 * Una constante de tiempo nula o negativa produce `α = 1`, es decir, ausencia
 * de filtrado. Es la interpretación honesta de «sin constante de tiempo», y
 * evita que un cero de configuración se convierta en una división indefinida.
 * Devuelve false si los argumentos no permiten un filtro utilizable, sin dejar
 * la estructura a medio configurar.
 */
bool control_filter_configure(control_filter_t *filter,
                              float time_constant_s,
                              float sample_period_s);

/*
 * Olvida el estado y conserva el coeficiente.
 *
 * Se invoca cuando la señal deja de ser apta. Al volver, el filtro se ceba de
 * nuevo con la primera muestra válida en lugar de arrastrar un valor anterior a
 * un hueco de duración desconocida, que es la misma regla que sigue la mediana.
 */
void control_filter_reset(control_filter_t *filter);

/*
 * Incorpora una muestra y devuelve la salida filtrada.
 *
 * La primera muestra tras un `reset` se adopta tal cual. Arrancar desde cero
 * introduciría un transitorio que el controlador no podría distinguir de una
 * caída real de la magnitud medida.
 */
float control_filter_push(control_filter_t *filter, float sample);

/* Última salida; false mientras el filtro no haya recibido ninguna muestra. */
bool control_filter_value(const control_filter_t *filter, float *value);

float control_filter_alpha(const control_filter_t *filter);
