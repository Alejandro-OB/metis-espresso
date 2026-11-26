#pragma once

/*
 * Supervisión del proceso: decide qué eventos debe recibir la máquina de
 * estados a partir de lo que ocurre en la cafetera.
 *
 * Este módulo existe porque esa decisión vivía dentro de la planta simulada, de
 * modo que la supervisión solo funcionaba mientras el proceso fuera ficticio: al
 * conectar los sensores, la máquina de estados habría dejado de recibir la
 * parada por tiempo máximo. Separar modelar de supervisar
 * también mantiene el gemelo digital fuera del lazo: un
 * modelo no puede ser quien decida cuándo detener la máquina real.
 *
 * El supervisor recibe una observación —números y su aptitud— y nunca sabe si
 * proviene de los sensores o de la planta embebida. Esa ignorancia es
 * deliberada: es lo que garantiza que ambos orígenes se supervisen igual.
 */

#include <stdbool.h>
#include <stdint.h>

#include "operational_fsm.h"

/*
 * Límites de la máquina, no del perfil. El perfil aporta la referencia de café
 * y el tiempo máximo de la preparación; el resto describe a la cafetera y no lo
 * elige quien define un perfil.
 */
typedef struct {
    float coffee_setpoint_c;
    float ready_tolerance_c;      /* banda para declarar alcanzada la referencia */
    float lost_delta_c;           /* caída que hace perder la condición de preparada */
    float hard_overtemperature_c; /* límite duro; por encima se dispara la protección */
    float hard_overpressure_bar;
    uint32_t maximum_extraction_ms;
    uint32_t maximum_purge_ms;
} process_limits_t;

/*
 * Lo observado en un instante. `temperature_usable` y `pressure_usable` ya
 * incorporan el juicio de calidad de la señal: el supervisor no vuelve a
 * evaluarla, solo se niega a decidir con una medida no apta.
 *
 * El cruce por cero se declara en dos partes a propósito. Mientras no exista la
 * rutina que lo lee, nadie lo observa, y «no observado» no puede tratarse como
 * «presente» ni como «perdido»: lo primero afirmaría algo que no se sabe y lo
 * segundo impediría toda preparación. Cuando exista la lectura real, la
 * distinción desaparece porque siempre se observará.
 */
typedef struct {
    float temperature_c;
    float pressure_bar;
    bool temperature_usable;
    bool pressure_usable;
    bool zero_cross_observed;
    bool zero_cross_available;
    /* El detector de cruce por cero no ve la red en este instante. Se declara
     * en negativo: a cero «hay red», y las protecciones que dependen de la
     * presión siguen aplicando hasta que alguien vea que la red falta. */
    bool mains_absent;
} process_observation_t;

/*
 * Cuánto tiene que faltar la red, seguida, para devolver la máquina a reposo.
 * Provisional, sin medida: el detector ya declara perdida la señal a los tres
 * periodos (25 ms), y un periodo fuera de rango la deja unos 42 ms sin declarar
 * apta. Un segundo son 120 periodos: ninguna de esas intermitencias dura tanto,
 * y desenchufar sí.
 */
#define PROCESS_MAINS_LOSS_MS 1000U

typedef struct {
    operational_state_t previous_state;
    uint32_t state_elapsed_ms;
    uint32_t mains_absent_ms;
    bool initialized;
} process_supervisor_t;

/* Límites nominales del prototipo, no valores puestos en servicio. */
process_limits_t process_limits_default(void);
bool process_limits_are_valid(const process_limits_t *limits);

/*
 * Aplica la referencia y el tiempo máximo del perfil efectivo sobre los límites
 * de la máquina. Devuelve false sin modificar nada cuando el perfil no cabe
 * dentro de ellos: un perfil no puede ampliar un límite de seguridad.
 */
bool process_limits_apply_profile(process_limits_t *limits,
                                  float temperature_setpoint_c,
                                  uint32_t maximum_time_ms);

/*
 * Baja el límite duro de temperatura para el ensayo de corte de la puesta en
 * servicio (5.10): comprobar sobre la caldera real que la máquina pasa sola a
 * alarma al superarlo, sin llevarla a los 150 °C del límite de banco.
 *
 * **Solo puede bajar.** Un `limit_c` por encima del vigente no cambia nada, y
 * la referencia de café baja con él lo necesario para que los límites sigan
 * siendo válidos: `process_limits_are_valid` exige que el límite quede por
 * encima de la referencia, y esa comprobación se conserva en vez de saltarla.
 * Devuelve false, sin tocar nada, si el resultado no fuera válido.
 */
#define PROCESS_TRIP_TEST_SETPOINT_MARGIN_C 10.0F

bool process_limits_lower_for_trip_test(process_limits_t *limits, float limit_c);

/*
 * Diagnóstico de la bomba sin calentar (2026-10-10): baja la referencia de café
 * a `PROCESS_PUMP_DIAGNOSTIC_SETPOINT_C`, de modo que la máquina se declara
 * preparada a cualquier temperatura de la caldera y el lazo térmico no pide
 * potencia. No toca el límite duro ni ninguna otra protección, y el resultado
 * sigue siendo válido. Exclusivo del binario de identificación de diagnóstico.
 */
#define PROCESS_PUMP_DIAGNOSTIC_SETPOINT_C 5.0F
bool process_limits_for_pump_diagnostic(process_limits_t *limits);

/*
 * Duración del purgado manual fijada al compilar (2026-10-15). La condición 3
 * del plan de la temperatura del café necesita un purgado siempre igual, y a
 * mano no lo es; el operador pidió además poder alargarlo. El corte sigue
 * siendo del supervisor, por la misma ruta que el de 10 s.
 *
 * El tope es el tiempo máximo de extracción por omisión: es la bomba más larga
 * que el supervisor ya admite, así que alargar el purgado no abre una marcha
 * que no exista. Devuelve false, sin tocar nada, fuera de [1 s, tope].
 */
#define PROCESS_PURGE_MINIMUM_MS 1000U
#define PROCESS_PURGE_MAXIMUM_MS 30000U
bool process_limits_set_purge(process_limits_t *limits, uint32_t maximum_purge_ms);

void process_supervisor_init(process_supervisor_t *supervisor);

/*
 * Avanza los temporizadores y devuelve la máscara de eventos que corresponde
 * publicar. Devuelve como mucho un evento por llamada, el de mayor prioridad,
 * para que la máquina de estados reciba la causa real y no su consecuencia.
 */
uint32_t process_supervisor_evaluate(process_supervisor_t *supervisor,
                                     const process_limits_t *limits,
                                     const process_observation_t *observation,
                                     operational_state_t state,
                                     uint32_t elapsed_ms);

uint32_t process_supervisor_state_elapsed_ms(const process_supervisor_t *supervisor);

/*
 * Si una máquina ya «lista» deja de estarlo porque su referencia acaba de
 * subir (compensación del primer uso, ADR-009, enmienda del 2026-10-19).
 *
 * En «lista» la condición solo se pierde al caer `lost_delta_c` bajo la
 * referencia, que es la histéresis contra el temblor del sensor. Con la
 * referencia recién subida menos que eso, la máquina seguiría «lista» con la
 * caldera todavía por debajo, y el barista podría pulsar antes de que llegara:
 * el café saldría sin la compensación que se pidió. Esta comprobación aplica a
 * la referencia nueva la misma regla con que «calentando» declara «lista»
 * (temperatura apta y no menor que referencia − `ready_tolerance_c`).
 *
 * Solo debe llamarse en el ciclo en que la referencia sube; usarla en todos los
 * ciclos quitaría la histéresis de «lista». Devuelve true si corresponde
 * publicar `OP_EVENT_COFFEE_TEMPERATURE_LOST`. Fuera de «lista», con límites
 * no válidos o sin observación apta, false: el resto de las reglas ya decide.
 */
bool process_supervisor_ready_withdrawn(const process_limits_t *limits,
                                        const process_observation_t *observation,
                                        operational_state_t state);

/*
 * Si una alarma puede despejarse: ninguna condición insegura de las que el
 * supervisor vigila fuera del bombeo sigue presente (temperatura apta y bajo el
 * límite duro, presión bajo el suyo si se mide). Es la misma evaluación que
 * dispara, hecha para el reposo al que volvería la máquina, de modo que lo que
 * se despeja no vuelve a disparar en el ciclo siguiente. Sin límites válidos o
 * sin observación, no: no poder comprobarlo no es lo mismo que estar bien.
 */
bool process_supervisor_clear_allowed(const process_limits_t *limits,
                                      const process_observation_t *observation);
