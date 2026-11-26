#pragma once

/*
 * Historia del cuerpo de la caldera en el minuto previo a pulsar café.
 *
 * Existe porque la estabilidad térmica de la máquina, como la define la § 4.4
 * de la monografía, se mide **antes** de la preparación: el metal dentro de
 * ±0,8 °C de su referencia en el minuto previo a pulsar, con la máquina lista.
 * La serie medida empezaba al pulsar, de modo que la plataforma no tenía con
 * qué calcularla. Enmienda del 2026-10-19 a ADR-012 § 4.1.
 *
 * El dispositivo no sabe que alguien va a pulsar hasta que pulsa, así que no
 * puede «recordar el minuto anterior» sin guardarlo de antemano. Este anillo
 * guarda, mientras la máquina está lista, una muestra del cuerpo y de la presión
 * cada `PRE_START_HISTORY_INTERVAL_MS`, y al abrir la preparación la sesión
 * copia las que caen en el minuto previo, con instante negativo.
 *
 * Criterios, con su razón:
 *
 * - **Selección sobre la rejilla, no promedio ni interpolación**, igual que la
 *   serie de la preparación (`series_message`): cada punto es una muestra que
 *   el dispositivo tomó de verdad en el instante que declara. La rejilla no
 *   puede anclarse al instante de pulsar porque ese instante aún no existe
 *   cuando se guarda; queda anclada a la primera muestra de la racha, y los
 *   instantes negativos no son múltiplos exactos de 500.
 * - **La historia es una racha continua de máquina lista.** Si entre dos
 *   observaciones pasa más de `PRE_START_HISTORY_MAX_GAP_MS`, la máquina dejó
 *   de estar lista o nadie la observó, y lo anterior se descarta. Conservarlo
 *   haría pasar por «minuto previo con la máquina lista» un rato en que no lo
 *   estaba, y la fracción dentro de la banda saldría mejor de lo que fue.
 * - **Lo medido y lo simulado no se mezclan.** Cambiar de origen vacía el
 *   anillo; la sesión solo lo toma si su origen coincide con el de la
 *   preparación.
 * - **Una muestra no apta se guarda marcada**, no se salta ni se sustituye:
 *   quien consume la serie decide qué hacer con ella.
 * - **Sin historia suficiente se entrega la que haya.** Recién llegada a
 *   «lista» o tras un reinicio el anillo tiene menos de un minuto, y la serie
 *   lleva solo eso; la plataforma ve cuántas muestras previas hay.
 *
 * Los instantes van en `uint32_t` con aritmética modular: el reloj monótono
 * truncado da la vuelta cada 49 días de encendido, y las diferencias entre
 * instantes cercanos siguen siendo exactas aunque ocurra. Se descartó guardar
 * `uint64_t` por muestra porque la alineación subiría cada entrada de 16 a 24
 * bytes, y esto se paga dos veces (el anillo y la copia de la sesión).
 *
 * Qué **no** hace: no decide cuándo la máquina está lista —se le observa solo
 * entonces y se le vacía en cada cambio de estado—, no conoce el reloj ni la
 * máquina de estados, no reserva memoria y no calcula la estabilidad, que es de
 * la plataforma (`packages/espresso_stability/machine.py`).
 */

#include <stdbool.h>
#include <stdint.h>

/* La rejilla de la serie medida; `series_message.c` comprueba que coinciden. */
#define PRE_START_HISTORY_INTERVAL_MS 500U

/* El minuto que la § 4.4 de la monografía declara para la estabilidad térmica. */
#define PRE_START_HISTORY_WINDOW_MS 60000U

/*
 * Un punto más que los intervalos del minuto, para que la muestra que cae justo
 * en −60 s no expulse a la más reciente.
 */
#define PRE_START_HISTORY_CAPACITY \
    (PRE_START_HISTORY_WINDOW_MS / PRE_START_HISTORY_INTERVAL_MS + 1U)

/*
 * Hueco que rompe la racha. Dos intervalos de rejilla: con la tarea a 100 ms,
 * entre dos muestras conservadas pasan 500 ms más la latencia de la tarea, y
 * superar dos intervalos significa que al menos un punto de la rejilla no se
 * observó. No es una cifra física sino de diseño: cualquier valor entre uno y
 * pocos intervalos cumple el mismo papel.
 */
#define PRE_START_HISTORY_MAX_GAP_MS (2U * PRE_START_HISTORY_INTERVAL_MS)

/* Lo que se observa del proceso en cada vuelta mientras la máquina está lista. */
typedef struct {
    float temperature_c;
    float pressure_bar;
    bool temperature_valid;
    bool pressure_valid;
    /* Falso es simulado; el origen desconocido no se observa. */
    bool measured;
} pre_start_observation_t;

/* Muestra previa ya situada respecto al instante de pulsar. */
typedef struct {
    int32_t elapsed_ms;   /* siempre negativo: antes de pulsar */
    float temperature_c;
    float pressure_bar;
    bool temperature_valid;
    bool pressure_valid;
} pre_start_sample_t;

typedef struct {
    uint32_t at_ms;
    float temperature_c;
    float pressure_bar;
    bool temperature_valid;
    bool pressure_valid;
} pre_start_entry_t;

typedef struct {
    pre_start_entry_t entries[PRE_START_HISTORY_CAPACITY];
    uint32_t oldest;
    uint32_t count;
    uint32_t next_target_ms;
    uint32_t last_observed_ms;
    bool measured;
} pre_start_history_t;

/* Vacía la historia. Una estructura puesta a cero ya está vacía. */
void pre_start_history_reset(pre_start_history_t *history);

/*
 * Ofrece una observación tomada en `now_ms`. Se conserva si alcanza el
 * siguiente punto de la rejilla; si llega tras un hueco o con otro origen, la
 * racha anterior se descarta y esta la empieza. Devuelve si se conservó.
 */
bool pre_start_history_observe(pre_start_history_t *history,
                               uint32_t now_ms,
                               const pre_start_observation_t *observation);

/*
 * Copia en `out`, en orden cronológico, las muestras anteriores a `press_ms`
 * dentro del minuto previo, con su instante relativo negativo. Devuelve cuántas
 * escribió: cero si no hay historia o si su origen no es el que declara
 * `measured`, porque una serie medida no puede llevar delante un minuto
 * simulado ni al revés. Una muestra tomada en el mismo milisegundo que la
 * pulsación no se copia: su instante sería cero y chocaría con la primera de la
 * preparación.
 */
uint32_t pre_start_history_freeze(const pre_start_history_t *history,
                                  uint32_t press_ms,
                                  bool measured,
                                  pre_start_sample_t *out,
                                  uint32_t capacity);
