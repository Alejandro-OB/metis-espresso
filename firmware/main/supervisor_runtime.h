#pragma once

/*
 * Tarea que supervisa el proceso y alimenta la máquina de estados.
 *
 * Es el único componente que publica eventos derivados de lo que ocurre en la
 * cafetera. El mando físico y el canal de banco publican los suyos, pero
 * ninguno de ellos observa el proceso.
 */

#include <stdbool.h>
#include <stdint.h>

#include "actuation_guard.h"
#include "esp_err.h"
#include "identification_mode.h"
#include "pump_ladder.h"
#include "process_supervisor.h"
#include "process_variables.h"

/*
 * Estado del modo restringido. En el binario de operación queda en
 * `no-sequence` y `disabled` durante toda la sesión.
 */
typedef struct {
    identification_setup_t setup;
    identification_phase_t phase;
    stimulus_sequence_id_t sequence;
    identification_target_t target;
    float maximum_percent;
    /* Demanda que el guardián admitió en el último ciclo, y solo esa: el lazo
     * aplica lo que se juzgó, no lo que la secuencia pida después. Vale cero
     * cuando el veredicto no es el de identificación. */
    float demand_percent;
    uint32_t running_ms;
    actuation_verdict_t stop_verdict;
} identification_status_t;

typedef struct {
    process_source_t source;      /* origen de la última observación */
    bool observing;               /* hubo observación utilizable en el último ciclo */
    uint32_t published_events;    /* eventos publicados desde el arranque */
    uint32_t dropped_events;      /* eventos que la cola no admitió */
    operational_state_t state;
    process_limits_t limits;
    actuation_decision_t actuation;   /* veredicto vigente sobre las salidas */
    uint32_t inhibition_cycles;       /* ciclos en los que se llevó algo a reposo */
    /* Servicio continuo de la bomba. Se publica porque ADR-007 § 3.6 obliga a
     * registrar el tiempo de habilitación y el reposo acumulado, y el guardián
     * que los lleva vive dentro de esta tarea. */
    pump_service_t pump_service;
    identification_status_t identification;
    /* Desplazamiento del primer uso que lleva ya `limits.coffee_setpoint_c`
     * (ADR-009, enmienda del 2026-10-19). Cero sin petición o una vez
     * consumido; el resumen de la preparación lo toma de aquí. */
    float first_use_offset_c;
    /* Refuerzo de la prealimentación para esa misma preparación (enmienda del
     * 2026-10-27); cero es «la ganancia del acta». El control la toma de aquí. */
    float first_use_feedforward_gain;
} supervisor_status_t;

esp_err_t supervisor_runtime_start(void);
bool supervisor_runtime_get_status(supervisor_status_t *status);

/*
 * Veredicto vigente para cada salida. Todo controlador debe consultarlo antes
 * de energizar, y no puede sustituirlo por su propio criterio: el guardián
 * lleva además las salidas a reposo por su cuenta cada ciclo.
 */
/*
 * Pide despejar la alarma (gesto del mando, `panel_gesture`). El supervisor lo
 * atiende en su siguiente ciclo: solo publica `OP_EVENT_SAFETY_CLEARED` si la
 * máquina sigue en alarma y `process_supervisor_clear_allowed` lo admite con la
 * observación de ese ciclo. Si no, lo dice en el registro y la alarma sigue.
 */
void supervisor_runtime_request_alarm_clear(void);

/*
 * Marca que hay una actualización de firmware en marcha (ADR-021). Mientras
 * dure, el guardián inhibe las dos salidas por `ACTUATION_INHIBITED_STARTING_UP`
 * desde su siguiente ciclo, sea cual sea el estado. Quien la pide debe esperar a
 * ver ese veredicto publicado antes de escribir la memoria de programa.
 */
void supervisor_runtime_set_updating(bool updating);

bool supervisor_runtime_pump_allowed(void);
bool supervisor_runtime_heater_allowed(void);

/*
 * Atiende una orden `B,` del puerto de servicio (escalera de la bomba). Solo
 * hace algo en el binario con `CAFE_IDENTIFICATION_PUMP_LADDER_V1`; en
 * cualquier otro responde «sin configurar». Devuelve en `current_percent` el
 * escalón actual de la escalera.
 */
pump_ladder_result_t supervisor_runtime_ladder_command(const pump_ladder_command_t *command,
                                                       uint8_t *current_percent);
