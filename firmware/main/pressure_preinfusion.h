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
 *
 * ## Dos formas de arrancar el PI
 *
 * Con la integral fija del acta, el relevo es un salto: la bomba viene del
 * techo y el PI empieza desde la integral de arranque, porque su referencia
 * interna parte de la medida y el proporcional vale casi cero en el primer
 * paso. Con el 15 % de la v12 el mando bajó de 75 a 40–49 % y la presión cayó a
 * 6,59–6,72 bar durante unos 2 s en las nueve extracciones de banco del
 * 2026-11-06, y a 6,1–6,7 bar en la catación del 2026-11-04. Subir la integral
 * fija al 40 % solo movió el problema: el mínimo subió a 7,37–7,78 bar, pero con
 * la molienda más fina la presión llegó a 10,09 bar. Ningún valor fijo sirve
 * para las dos moliendas, porque el mando en régimen depende de la pastilla.
 *
 * Arrancar desde el techo es la transferencia sin salto: la integral toma el
 * mando que la bomba ya traía, y el PI lo baja desde ahí hasta lo que pida la
 * pastilla, que en régimen quedó en 62–70 % a 45 y a 50 clics. Lo elige el
 * acta; el cero del enumerado es la integral fija, el comportamiento de las
 * plantas ya firmadas, para que una planta que no lo declara no cambie.
 *
 * La protección de la presión atrapada vale en los dos modos: si al relevo la
 * presión ya llegó a la referencia, la integral arranca en cero.
 */

#include <stdbool.h>

typedef enum {
    /* El cero es «nadie ha decidido»: sin secuencia, la bomba no pide nada. */
    PRESSURE_PREINFUSION_IDLE = 0,
    PRESSURE_PREINFUSION_WETTING,
    PRESSURE_PREINFUSION_REGULATING,
} pressure_preinfusion_phase_t;

typedef enum {
    /* El cero es el arranque de las plantas firmadas: la integral del acta. */
    PRESSURE_PREINFUSION_START_FIXED = 0,
    /* Desde el techo que traía la bomba: el relevo sin salto (2026-11-06). */
    PRESSURE_PREINFUSION_START_CEILING,
} pressure_preinfusion_start_t;

typedef struct {
    float handover_margin_bar;      /* el PI entra en P ≥ referencia − margen */
    float integral_start_percent;   /* la integral con que arranca el PI */
    pressure_preinfusion_start_t start;
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
 * y finito, la integral de arranque no está entre 0 y 100 o el modo de arranque
 * no es uno de los declarados.
 */
bool pressure_preinfusion_configure(pressure_preinfusion_t *preinfusion,
                                    const pressure_preinfusion_parameters_t *parameters);

pressure_preinfusion_decision_t pressure_preinfusion_step(
    pressure_preinfusion_t *preinfusion, const pressure_preinfusion_inputs_t *inputs);

pressure_preinfusion_phase_t pressure_preinfusion_phase(const pressure_preinfusion_t *preinfusion);

const char *pressure_preinfusion_phase_name(pressure_preinfusion_phase_t phase);
