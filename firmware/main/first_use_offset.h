#pragma once

/*
 * Compensación del agua fría del primer uso tras un reposo (ADR-009, enmienda
 * del 2026-10-19).
 *
 * Tras reposos de 20 min a 4 h el primer uso sale 2,4–3,9 °C más frío (media
 * del chorro entre 5 y 19 s) que en uso continuo; tras 10 min, no (evidencia
 * `temperatura-cierre-2026-10-17.md`, ensayos de reposo). El agua sigue al
 * metal al pulsar con ~0,81 °C por °C (acta v6), así que subir la referencia de
 * caldera antes de ese uso lo compensa.
 *
 * Quién decide cuánto: **el gemelo de la plataforma**. Predice el déficit del
 * próximo uso y pide, en el perfil (`boilerOffsetC`), un incremento de la
 * referencia de caldera para la próxima preparación. Este módulo es lo que la
 * placa hace con esa petición: la valida y la aplica una sola vez.
 *
 *     inactivo ──(adopta un desplazamiento admitido)──▶ armado
 *         ▲                                               │
 *         │                                (empieza una preparación)
 *         │                                               ▼
 *         └────────(termina esa preparación)─────── en preparación
 *
 * Mientras está armado o en preparación, la referencia de caldera es la del
 * perfil más el desplazamiento; en inactivo, la del perfil. La preparación que
 * lo consume tiene que **empezar después de adoptarlo**: si la adopción llegara
 * con un café en curso (la sincronización lo evita, pero el supervisor lo ve un
 * ciclo tarde), ese café no se lo lleva.
 *
 * El desplazamiento se suma después de convertir el agua pedida a caldera
 * (`water_reference`): no lo limita el intervalo medido de la recta, sino el
 * máximo admitido y el límite duro.
 *
 * ## Lo que no hace
 *
 * - **No predice.** No sabe cuánto lleva la máquina en reposo ni cuándo pulsará
 *   el barista: eso es del gemelo. Sin petición, desplazamiento cero.
 * - **No compensa los usos siguientes.** Termina la preparación y vuelve a cero,
 *   aunque el perfil siga diciendo lo mismo. Volver a pedirlo es una adopción
 *   nueva, y esa decisión la toma quien llama, no este módulo.
 * - **No extrapola.** El máximo sale de lo medido (acta, sección
 *   `first_use_offset`); sin él es cero y cualquier desplazamiento se rechaza.
 * - **No recorta.** Una petición negativa, no finita, por encima del máximo o
 *   que dejaría la referencia sin el margen que `process_limits_are_valid`
 *   exige bajo el límite duro se rechaza con su causa; no se rebaja a lo que
 *   cabe, porque eso sería aplicar un número que nadie pidió.
 * - **No caduca.** Si el barista tarda horas, la caldera espera más caliente;
 *   la petición era para la próxima preparación y sigue siéndolo. Una caldera
 *   que espera más alta no es un riesgo: el límite duro y el guardián deciden
 *   antes, por su ruta propia.
 * - **No cuenta los purgados.** Un purgado no es una preparación.
 *
 * ## El refuerzo de la prealimentación (enmienda del 2026-10-27)
 *
 * La misma petición puede traer una ganancia de prealimentación mayor que la
 * del acta para ese café (`firstUseFeedforwardGain`). Comparte el ciclo de la
 * subida —armado, en preparación, de vuelta a inactivo— porque es la misma
 * decisión del gemelo sobre el mismo café, pero se juzga aparte: lo medido es
 * el refuerzo sin subida, y la plataforma tiene que poder pedir uno sin el
 * otro. Se rechaza bajo la ganancia del acta o sobre el máximo medido; igual
 * que la subida, no se recorta.
 *
 * Puro: no conoce el reloj ni el perfil. Recibe la petición, los límites y el
 * estado operativo, y devuelve lo que decidió.
 */

#include <stdbool.h>

#include "operational_fsm.h"

/* Juicio de una petición. Cero es «sin juzgar», que no admite nada. */
typedef enum {
    FIRST_USE_OFFSET_UNCHECKED = 0,
    /* Se pidió cero: nada que aplicar, como sin el campo. */
    FIRST_USE_OFFSET_NONE,
    FIRST_USE_OFFSET_ACCEPTED,
    FIRST_USE_OFFSET_NOT_FINITE,
    FIRST_USE_OFFSET_NEGATIVE,
    FIRST_USE_OFFSET_ABOVE_MAXIMUM,
    /* La referencia subida no dejaría la banda de «lista» bajo el límite duro. */
    FIRST_USE_OFFSET_ABOVE_HARD_LIMIT,
} first_use_offset_check_t;

/* Juicio de una ganancia pedida. Cero es «sin juzgar», que no admite nada. */
typedef enum {
    FIRST_USE_FEEDFORWARD_UNCHECKED = 0,
    /* Sin petición, o la misma ganancia del acta: nada que reforzar. */
    FIRST_USE_FEEDFORWARD_NONE,
    FIRST_USE_FEEDFORWARD_ACCEPTED,
    FIRST_USE_FEEDFORWARD_NOT_FINITE,
    /* Por debajo de la del acta: el refuerzo no sirve para quitar calor. */
    FIRST_USE_FEEDFORWARD_BELOW_BASE,
    FIRST_USE_FEEDFORWARD_ABOVE_MAXIMUM,
} first_use_feedforward_check_t;

typedef enum {
    /* Cero es «sin desplazamiento»: la referencia es la del perfil. */
    FIRST_USE_OFFSET_IDLE = 0,
    FIRST_USE_OFFSET_ARMED,
    FIRST_USE_OFFSET_BREWING,
} first_use_offset_phase_t;

typedef struct {
    /* Del acta o, en el binario de ensayo, provisional. A cero no se admite
     * ningún desplazamiento: es el caso sin sección en el acta. */
    float maximum_c;
    /* Límite duro de temperatura vigente. */
    float hard_limit_c;
    /* Banda de «lista»: `process_limits_are_valid` exige que la referencia más
     * ella quede bajo el límite duro, y aquí se exige lo mismo. */
    float margin_c;
    /* Ganancia de la prealimentación del acta, en % de calentador por % de
     * bomba: pedir esa misma es no pedir refuerzo. */
    float feedforward_base_gain;
    /* Del acta (`first_use_offset.max_feedforward_gain`). Sin ella, o ilegible,
     * cuenta como la del acta: no se admite ningún refuerzo. */
    float feedforward_maximum_gain;
} first_use_offset_bounds_t;

typedef struct {
    first_use_offset_phase_t phase;
    float offset_c;
    /* Ganancia reforzada para la preparación que lo consume; cero es «la del
     * acta». */
    float feedforward_gain;
    /* Armado, ya se vio la máquina fuera de una preparación: la siguiente que
     * empiece es la que lo consume. */
    bool outside_seen;
} first_use_offset_t;

void first_use_offset_init(first_use_offset_t *state);

/*
 * Juzga una petición sobre la referencia de caldera del perfil, sin tocar
 * ningún estado. Un máximo no finito o negativo cuenta como cero.
 */
first_use_offset_check_t first_use_offset_check(float requested_c,
                                                const first_use_offset_bounds_t *bounds,
                                                float boiler_setpoint_c);

/* La petición puede adoptarse: cero o admitida. */
bool first_use_offset_check_allows(first_use_offset_check_t check);

/*
 * Adopta la petición de un perfil nuevo. Admitida y mayor que cero, arma; cero
 * o rechazada, deja el estado inactivo. En los dos casos sustituye a lo que
 * hubiera pendiente: el perfil nuevo es el que manda.
 */
first_use_offset_check_t first_use_offset_adopt(first_use_offset_t *state,
                                                float requested_c,
                                                const first_use_offset_bounds_t *bounds,
                                                float boiler_setpoint_c);

/*
 * Juzga una ganancia de refuerzo pedida, sin tocar ningún estado. Cero es «sin
 * petición».
 */
first_use_feedforward_check_t first_use_feedforward_check(float requested_gain,
                                                          const first_use_offset_bounds_t *bounds);

/* La ganancia puede adoptarse: sin petición o admitida. */
bool first_use_feedforward_check_allows(first_use_feedforward_check_t check);

/*
 * Adopta la subida y el refuerzo de un perfil nuevo. Si alguno de los dos no se
 * admite, no adopta ninguno y deja el estado inactivo: el perfil se rechaza
 * entero. Si alguno de los dos es mayor que nada, arma. `feedforward_check`
 * puede ser NULL; si no, recibe el juicio del refuerzo.
 */
first_use_offset_check_t first_use_offset_adopt_request(
    first_use_offset_t *state, float requested_c, float requested_gain,
    const first_use_offset_bounds_t *bounds, float boiler_setpoint_c,
    first_use_feedforward_check_t *feedforward_check);

/*
 * Avanza con el estado operativo de este ciclo. Devuelve true solo en el ciclo
 * en que termina la preparación que lo consumió y el desplazamiento vuelve a
 * cero: quien llama tiene que bajar entonces la referencia.
 */
bool first_use_offset_observe(first_use_offset_t *state, operational_state_t operational);

/* Desplazamiento que rige ahora: el adoptado mientras dura, cero después. */
float first_use_offset_applied_c(const first_use_offset_t *state);

/* Ganancia reforzada que rige ahora, o cero si rige la del acta. */
float first_use_offset_feedforward_gain(const first_use_offset_t *state);

const char *first_use_offset_check_name(first_use_offset_check_t check);
const char *first_use_feedforward_check_name(first_use_feedforward_check_t check);
const char *first_use_offset_phase_name(first_use_offset_phase_t phase);
