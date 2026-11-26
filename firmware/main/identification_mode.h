#pragma once

/*
 * Modo restringido de puesta en servicio (ADR-007 § 5, enmienda del
 * 2026-09-21; puesta en servicio, pieza F1).
 *
 * Es una autorización de actuación en **lazo abierto**, acotada y temporal, que
 * sustituye al controlador en lugar de habilitarlo. Existe porque sin ella el
 * procedimiento de puesta en marcha se pedía a sí mismo: identificar la planta
 * exige actuar, el guardián corta mientras `commissioned` sea falso, y esa marca
 * solo cambia con los registros de la identificación.
 *
 * ## Las tres condiciones, y quién sostiene cada una
 *
 * 1. **Un binario distinto del de operación.** No vive aquí sino en Kconfig:
 *    sin `CONFIG_CAFE_IDENTIFICATION` no se compila ninguna secuencia y este
 *    módulo se configura sin ella, que lo deja en `IDENTIFICATION_DISABLED`
 *    para siempre. No hay mensaje ni marca en ejecución que lo cambie.
 * 2. **Una confirmación física en cada arranque**, sobre el mando: café y tara
 *    apretados a la vez durante `IDENTIFICATION_CONFIRMATION_HOLD_MS`, dentro
 *    de la ventana que abre el arranque y con la máquina todavía en reposo
 *    seguro. Sin ella el binario de identificación se comporta como el de
 *    operación hasta el siguiente arranque.
 * 3. **Una secuencia declarada y versionada**, que la configuración elige al
 *    compilar (`stimulus_sequence`).
 *
 * ## Lo que decide, y lo que no
 *
 * Decide **qué pedir** al guardián —la salida y la demanda del instante— y
 * lleva la cuenta de la secuencia. No decide si se actúa: eso lo decide el
 * guardián con su propio tope, y este módulo solo observa su veredicto. Si el
 * guardián deja de admitir el estímulo por cualquier causa, la secuencia se da
 * por **interrumpida** y no se reanuda: un ensayo retomado a medias produce un
 * registro que no se puede repetir, y uno detenido y anotado vale más.
 *
 * Tampoco toca `commissioned` ni el PI. Mientras el veredicto sea el de
 * identificación, `control_runtime` mantiene el PI reiniciado y aplica la
 * demanda de la secuencia; las ganancias no intervienen.
 *
 * ## Por qué café y tara, y por qué hace falta soltarlos primero
 *
 * Es una combinación que la operación no usa —en reposo seguro la máquina de
 * estados ignora el botón de café, y la tara no cambia de estado— y que exige
 * las dos manos, de modo que no se produce por accidente. El pulsador de tara
 * fue el de vapor hasta el 2026-10-06; se mantuvo en la combinación porque
 * sigue siendo el mismo pulsador, y confirmar con él también toma una tara de
 * la báscula, que no actúa sobre nada. Se exige además ver los dos botones **sueltos** antes
 * de contar: un pulsador en cortocircuito, o apretado desde antes de encender,
 * no puede confirmar nada. La confirmación tiene que ser un acto posterior al
 * arranque.
 *
 * Los dos tiempos —5 s de pulsación, 30 s de ventana— no son magnitudes de la
 * planta sino de la interacción, y no exigen medición: son lo bastante largos
 * para que nadie confirme sin querer y lo bastante cortos para que la ventana no
 * quede abierta mientras la máquina ya está en uso.
 *
 * Este módulo es puro: no reserva memoria, no conoce el reloj ni los GPIO.
 */

#include <stdbool.h>
#include <stdint.h>

#include "actuation_guard.h"
#include "operational_fsm.h"
#include "stimulus_sequence.h"

/* 60 s desde el 2026-10-11: con 30 s el operador, que recibe las indicaciones
 * por mensajes con unos 20 s de retraso, perdió cinco ventanas seguidas. La
 * ventana sigue exigiendo la máquina en reposo seguro y los dos botones vistos
 * sueltos antes de contar; más tiempo no relaja ninguna de las dos. */
#define IDENTIFICATION_CONFIRMATION_WINDOW_MS UINT32_C(60000)
#define IDENTIFICATION_CONFIRMATION_HOLD_MS UINT32_C(5000)

/*
 * El cero es «inhabilitado»: una estructura puesta a cero no pide nada al
 * guardián.
 */
typedef enum {
    IDENTIFICATION_DISABLED = 0,          /* binario de operación, o configuración rechazada */
    IDENTIFICATION_AWAITING_CONFIRMATION, /* ventana abierta tras el arranque */
    IDENTIFICATION_DECLINED,              /* la ventana se cerró sin confirmar */
    IDENTIFICATION_ARMED,                 /* confirmado; espera a que el estado autorice */
    IDENTIFICATION_RUNNING,
    IDENTIFICATION_FINISHED,              /* la secuencia se agotó entera */
    IDENTIFICATION_ABORTED,               /* el guardián dejó de admitirla */
} identification_phase_t;

/* Por qué se aceptó o se rechazó la configuración. El cero, «sin secuencia». */
typedef enum {
    IDENTIFICATION_SETUP_NO_SEQUENCE = 0,   /* binario de operación */
    IDENTIFICATION_SETUP_ACCEPTED,
    IDENTIFICATION_SETUP_INVALID_SEQUENCE,
    IDENTIFICATION_SETUP_INVALID_LIMIT,
    IDENTIFICATION_SETUP_EXCEEDS_LIMIT,     /* la secuencia pide más que el tope */
    IDENTIFICATION_SETUP_ALREADY_COMMISSIONED,
} identification_setup_t;

/* Lo que pasó en una actualización y debe quedar en el registro del lazo. */
typedef enum {
    IDENTIFICATION_EVENT_NONE = 0,
    IDENTIFICATION_EVENT_STARTED,
    IDENTIFICATION_EVENT_FINISHED,
    IDENTIFICATION_EVENT_ABORTED,
} identification_event_t;

typedef struct {
    identification_phase_t phase;
    const stimulus_sequence_t *sequence;
    float maximum_percent;
    uint32_t window_ms;          /* tiempo transcurrido de la ventana de confirmación */
    uint32_t hold_ms;            /* pulsación simultánea continua */
    bool released_seen;          /* se vieron los dos botones sueltos */
    uint32_t running_ms;         /* tiempo de secuencia ya aplicado */
    actuation_verdict_t stop_verdict;  /* el que la interrumpió, si se interrumpió */
} identification_mode_t;

/*
 * Prepara el modo para este arranque.
 *
 * Sin secuencia, con una mal formada, con un tope que no se entiende, con una
 * secuencia que pide más que el tope, o con el lazo ya puesto en servicio, el
 * modo queda inhabilitado y no pedirá nada en toda la sesión. Comprobar el tope
 * aquí no sustituye al guardián, que lo vuelve a comprobar en cada ciclo: solo
 * evita armar un ensayo que se cortaría en el primer escalón.
 */
identification_setup_t identification_mode_configure(identification_mode_t *mode,
                                                     const stimulus_sequence_t *sequence,
                                                     float maximum_percent,
                                                     bool target_commissioned);

/*
 * Avanza la confirmación con lo que el mando muestra.
 *
 * Solo actúa mientras la ventana esté abierta. Si la máquina sale del reposo
 * seguro antes de confirmar, el operador eligió operar y la ventana se cierra.
 * Devuelve true en el ciclo en que la confirmación se completa.
 */
bool identification_mode_observe_panel(identification_mode_t *mode,
                                       operational_state_t state,
                                       bool brew_pressed,
                                       bool tare_pressed,
                                       uint32_t elapsed_ms);

/*
 * Lo que hay que pedir al guardián en este ciclo. Fuera de `ARMED` y `RUNNING`
 * es la petición nula, que no relaja nada.
 */
identification_request_t identification_mode_request(const identification_mode_t *mode);

/*
 * Avanza la secuencia con el veredicto que el guardián dio a la petición.
 *
 * `verdict` es el de la salida que la secuencia gobierna. La secuencia empieza
 * con el primer veredicto de identificación y se interrumpe con el primero que
 * no lo sea.
 */
identification_event_t identification_mode_update(identification_mode_t *mode,
                                                  actuation_verdict_t verdict,
                                                  uint32_t elapsed_ms);

/*
 * Arma otra secuencia en la misma sesión (escalera de la bomba, enmienda a
 * ADR-007 § 5 del 2026-10-10). Solo con la anterior terminada entera —no
 * abortada—, sobre la misma salida y sin pasar del tope. La confirmación física
 * de este arranque sigue valiendo; un aborto sigue cerrando la sesión.
 */
bool identification_mode_rearm(identification_mode_t *mode,
                               const stimulus_sequence_t *sequence);

const char *identification_phase_name(identification_phase_t phase);
const char *identification_setup_name(identification_setup_t setup);
