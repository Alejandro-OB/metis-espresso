#pragma once

/*
 * Arranque rápido del calentador (ADR-009, enmienda del 2026-10-11).
 *
 * El acta v2 limita el PI de temperatura al 10 %: con el 100 % su integral y su
 * retardo lo llevan por encima de la referencia. Este módulo es una fase
 * anterior al PI, no un PI distinto: al empezar a calentar pone el calentador
 * al 100 % hasta el corte `r − δ`, lo apaga del todo mientras el calor
 * acumulado sigue subiendo la temperatura, y cuando deja de subir entrega el
 * mando al PI con la integral en cero.
 *
 *     inactivo ──(arma)──▶ al 100 % ──(T ≥ r − δ, o t_máx)──▶ espera a 0 %
 *         ▲                    │                                   │
 *         │               (abandona)                     (deja de subir)
 *         │                    ▼                                   ▼
 *         └──(sale de «calentando»)──────────────────────── entregado al PI
 *
 * El corte es la referencia del perfil menos un margen, no un número absoluto:
 * el barista elige la referencia. `δ` es un solo número del acta, medido desde
 * frío, que es el peor caso; desde una caldera tibia el mismo `δ` es
 * conservador y el PI completa los últimos grados.
 *
 * ## Qué no hace
 *
 * - **No inventa los números.** `δ`, la banda de activación, el tiempo máximo
 *   al 100 %, la referencia máxima y la caída que marca el fin de la espera
 *   son de puesta en servicio. Sin configurar, que es lo que deja una
 *   estructura en cero, no actúa nunca.
 * - **No calcula el corte con un modelo.** Un `δ` dependiente de la
 *   temperatura de partida sería más rápido desde tibio, pero pondría un
 *   modelo en el camino de una decisión de seguridad (ADR-009).
 * - **No sustituye al guardián ni al límite duro**, que siguen decidiendo antes
 *   por su ruta propia. Este módulo solo puede pedir; si el guardián corta, el
 *   calentador no conduce.
 * - **No vuelve a armarse en el mismo episodio.** Decide una vez, en el primer
 *   ciclo de «calentando»; si no arma o se abandona, el resto del
 *   calentamiento lo hace el PI.
 * - **No espera indefinidamente.** La espera a 0 % dura como mucho `t_máx`: una
 *   lectura que no baja nunca no debe dejar la caldera sin calentar.
 *
 * Puro: no conoce el reloj, la bomba ni el calentador. Recibe el paso de
 * tiempo y el estado, y devuelve lo que decidió.
 */

#include <stdbool.h>

typedef enum {
    /* Cero es «no actúa»: sin parámetros, o fuera de «calentando». */
    HEATER_FAST_START_IDLE = 0,
    HEATER_FAST_START_BOOST,
    HEATER_FAST_START_COAST,
    HEATER_FAST_START_HANDED_OVER,
} heater_fast_start_phase_t;

/* Por qué terminó la fase al 100 %, para el registro. */
typedef enum {
    HEATER_FAST_START_END_NONE = 0,
    HEATER_FAST_START_END_CUT,       /* llegó a r − δ */
    HEATER_FAST_START_END_TIMEOUT,   /* venció t_máx sin llegar: anomalía */
    HEATER_FAST_START_END_ABANDONED, /* sensor no apto o bomba autorizada */
} heater_fast_start_end_t;

typedef struct {
    float margin_c;          /* δ: el corte es r − δ */
    float activation_band_c; /* solo arma por debajo de r − δ − banda */
    float max_boost_s;       /* t_máx al 100 % */
    float max_setpoint_c;    /* por encima de esta referencia no se usa */
    float release_drop_c;    /* caída desde el máximo que termina la espera */
} heater_fast_start_parameters_t;

typedef struct {
    bool heating;           /* estado operativo «calentando» */
    bool measurement_valid; /* temperatura apta */
    bool pump_allowed;      /* con agua entrando manda la prealimentación */
    float setpoint_c;       /* referencia autorizada del perfil */
    float temperature_c;    /* temperatura filtrada */
    float elapsed_s;        /* tiempo desde la llamada anterior */
} heater_fast_start_inputs_t;

typedef struct {
    /* Verdadero mientras el módulo gobierna la salida; falso, manda el PI. */
    bool overrides;
    float output;   /* % de calentador pedido, solo si `overrides` */
    /* Verdadero en el único ciclo en que el PI recibe el mando: quien lo
     * llama reinicia la integral desde la medida. */
    bool hand_over;
} heater_fast_start_decision_t;

typedef struct {
    heater_fast_start_parameters_t parameters;
    bool configured;
    heater_fast_start_phase_t phase;
    heater_fast_start_end_t end;
    bool used_in_episode;
    float boost_s;
    float peak_c;
} heater_fast_start_t;

/*
 * Configura los parámetros. Rechaza —y deja el módulo sin actuar— cualquier
 * valor no finito, un margen o un tiempo no positivos, o una banda o una caída
 * negativas: ninguno puede venir de una medida.
 */
bool heater_fast_start_configure(heater_fast_start_t *fast_start,
                                 const heater_fast_start_parameters_t *parameters);

heater_fast_start_decision_t heater_fast_start_step(heater_fast_start_t *fast_start,
                                                    const heater_fast_start_inputs_t *inputs);

heater_fast_start_phase_t heater_fast_start_phase(const heater_fast_start_t *fast_start);
heater_fast_start_end_t heater_fast_start_end(const heater_fast_start_t *fast_start);
const char *heater_fast_start_phase_name(heater_fast_start_phase_t phase);
