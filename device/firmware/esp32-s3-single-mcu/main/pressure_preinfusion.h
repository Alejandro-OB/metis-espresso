#pragma once

/*
 * Preinfusión a presión de extracción (ADR-007, enmienda del 2026-10-12, tarde).
 *
 * Con la canastilla presurizada, mientras la pastilla se moja la presión se
 * queda en la meseta de la válvula (6,6–7 bar) haga lo que haga la bomba: el
 * lazo no tiene ganancia y la integral del PI sube hasta el techo. Cuando la
 * pastilla cierra, esa integral es la del 75 %, y con una molienda fina, que en
 * régimen pide un 10 %, la presión llegó a 11,6 bar y en otra prueba disparó el
 * límite duro de 12 bar (2026-10-12, 44 clics).
 *
 * Este módulo es una fase anterior al PI. Mientras la pastilla se moja la bomba
 * va al techo que el guardián admitió, sin PI. Cuando la presión filtrada llega
 * a la referencia menos un margen, la pastilla ya cerró, y el mando pasa al PI
 * con su integral en un valor de arranque bajo:
 *
 *     inactivo ──(techo > 0)──▶ mojando, bomba al techo
 *         ▲                         │
 *         │               (P ≥ referencia − margen)
 *         │                         ▼
 *         └──(techo = 0)──────── regulando, PI desde la integral de arranque
 *
 * Es lo que hace la máquina del barista: unos segundos a presión de extracción
 * antes de regular. El paso depende de la presión y no del tiempo, porque el
 * mojado dura según la dosis y la molienda; con un tiempo fijo, una pastilla
 * que tarda más cierra con el PI ya cargado, que es el fallo que se corrige.
 *
 * ## Qué no hace
 *
 * - **No inventa los números.** El margen y la integral de arranque llegan
 *   configurados. Sin configurar, que es lo que deja una estructura en cero,
 *   no pasa nunca al PI: la bomba se queda al techo, en la meseta, como un
 *   escalón de identificación.
 * - **No precarga la integral si la presión ya llegó a la referencia** al
 *   pasar: arranca en cero (corrección del 2026-10-12, noche).
 * - **No vuelve atrás.** Una vez regulando, sigue así hasta que el techo vuelva
 *   a cero. Si el paso fuera prematuro, el PI recuperaría el comportamiento de
 *   antes de esta enmienda; el límite duro sigue protegiendo.
 * - **No sustituye al guardián ni al límite duro**, que deciden antes y por su
 *   ruta propia. El techo que recibe es el que el guardián ya admitió.
 * - **No conoce el reloj, la bomba ni el PI.** Recibe la presión, la referencia
 *   y el techo, y devuelve qué hacer.
 */

#include <stdbool.h>

typedef enum {
    /* El cero es «nadie ha decidido»: sin secuencia, la bomba no pide nada. */
    PRESSURE_PREINFUSION_IDLE = 0,
    PRESSURE_PREINFUSION_WETTING,
    PRESSURE_PREINFUSION_REGULATING,
} pressure_preinfusion_phase_t;

typedef struct {
    float handover_margin_bar;      /* el PI entra en P ≥ referencia − margen */
    float integral_start_percent;   /* la integral con que arranca el PI */
} pressure_preinfusion_parameters_t;

typedef struct {
    bool configured;
    pressure_preinfusion_parameters_t parameters;
    pressure_preinfusion_phase_t phase;
} pressure_preinfusion_t;

typedef struct {
    float ceiling_percent;   /* techo admitido en este ciclo; 0 fuera de la secuencia */
    float setpoint_bar;      /* referencia del perfil */
    float pressure_bar;      /* presión filtrada */
} pressure_preinfusion_inputs_t;

typedef struct {
    bool regulate;           /* el PI gobierna en este ciclo */
    bool hand_over;          /* este es el primer ciclo del PI: arrancarlo */
    float integral_start_percent;
} pressure_preinfusion_decision_t;

/*
 * Devuelve false, y deja el módulo sin configurar, si el margen no es positivo
 * y finito o la integral de arranque no está entre 0 y 100.
 */
bool pressure_preinfusion_configure(pressure_preinfusion_t *preinfusion,
                                    const pressure_preinfusion_parameters_t *parameters);

pressure_preinfusion_decision_t pressure_preinfusion_step(
    pressure_preinfusion_t *preinfusion, const pressure_preinfusion_inputs_t *inputs);

pressure_preinfusion_phase_t pressure_preinfusion_phase(const pressure_preinfusion_t *preinfusion);

const char *pressure_preinfusion_phase_name(pressure_preinfusion_phase_t phase);
