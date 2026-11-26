#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    OP_STATE_SAFE_INACTIVE = 0,
    OP_STATE_COFFEE_HEATING,
    OP_STATE_READY,
    OP_STATE_EXTRACTION,
    OP_STATE_MANUAL_PURGE,
    OP_STATE_ALARM,
} operational_state_t;

typedef enum {
    OP_EVENT_LOGICAL_POWER_TOGGLE = 0,
    OP_EVENT_COFFEE_TEMPERATURE_READY,
    OP_EVENT_COFFEE_TEMPERATURE_LOST,
    OP_EVENT_BREW_BUTTON_PRESSED,
    OP_EVENT_LOCAL_NORMAL_STOP,
    OP_EVENT_PURGE_BUTTON_PRESSED,
    OP_EVENT_PURGE_BUTTON_RELEASED,
    OP_EVENT_EXTRACTION_TIME_LIMIT,
    OP_EVENT_PURGE_TIME_LIMIT,
    OP_EVENT_SAFETY_TRIP,
    OP_EVENT_SAFETY_CLEARED,
    /* La red faltó de forma sostenida (`process_supervisor`, usuario
     * 2026-10-17): la máquina vuelve a reposo; un café en curso se cierra como
     * pérdida de energía. No es una alarma: no hay falla, falta la red. */
    OP_EVENT_MAINS_LOST,
    OP_EVENT_COUNT,
} operational_event_t;

#define OP_EVENT_MASK(event_) (UINT32_C(1) << (event_))

typedef enum {
    OP_TERMINATION_NONE = 0,
    OP_TERMINATION_MANUAL_NORMAL_STOP,
    OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED,
    OP_TERMINATION_SAFETY_PROTECTION_TRIP,
    /*
     * La máquina de estados nunca la produce: nadie ve el corte desde dentro.
     * La asigna el arranque siguiente a la preparación que encontró abierta
     * (ADR-005 § 3, RNF-006). Ver `open_extraction.h`.
     */
    OP_TERMINATION_UNEXPECTED_POWER_LOSS,
} extraction_termination_t;

typedef struct {
    operational_state_t state;
    extraction_termination_t last_extraction_termination;
    bool extraction_open;
} operational_fsm_t;

typedef struct {
    operational_state_t from;
    operational_state_t to;
    operational_event_t event;
    extraction_termination_t extraction_termination;
    bool accepted;
    bool extraction_started;
    bool extraction_finished;
} operational_transition_t;

typedef struct {
    bool heater_regulation_allowed;
    bool pressure_regulation_allowed;
    bool extraction_pump_allowed;
    bool purge_pump_allowed;
} operational_authorization_t;

void operational_fsm_init(operational_fsm_t *fsm);
operational_transition_t operational_fsm_dispatch(operational_fsm_t *fsm,
                                                   operational_event_t event);
operational_transition_t operational_fsm_dispatch_highest_priority(
    operational_fsm_t *fsm, uint32_t pending_events);
operational_authorization_t operational_fsm_authorization(const operational_fsm_t *fsm);

/*
 * Si la báscula de ensayo puede tomar la tara en este estado.
 *
 * Los pulsadores que fueron de vapor pasaron a ser la tara (ADR-006, revisión
 * del 2026-10-06). La tara no acciona nada ni cambia de estado, así que no es un
 * evento de esta máquina; pero sí depende de ella, porque tomarla mientras cae
 * agua en la taza —extracción o purga— pondría a cero una masa que se está
 * midiendo y falsearía en silencio el caudal de esa serie.
 */
bool operational_fsm_tare_allowed(operational_state_t state);

const char *operational_state_name(operational_state_t state);
const char *operational_event_name(operational_event_t event);

/*
 * Si el evento es una orden del barista desde el mando (encendido, café o
 * purgado al pulsar). Cuando la máquina de estados rechaza una de estas, el
 * panel lo dice con el destello rojo; las que publica el supervisor no tienen a
 * nadie esperando respuesta, y soltar el purgado no es una orden nueva.
 */
bool operational_event_is_operator_press(operational_event_t event);
const char *extraction_termination_name(extraction_termination_t termination);
