/* Verificación en host de la detección de condiciones inseguras. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "condition_monitor.h"
#include "event_message.h"

static actuation_decision_t decision(actuation_verdict_t pump, actuation_verdict_t heater)
{
    actuation_decision_t value = {.pump = pump, .heater = heater};
    return value;
}

int main(int argc, char **argv)
{
    condition_monitor_t monitor;
    condition_monitor_init(&monitor);

    /* La primera observación fija la referencia y no informa de nada: el valor
     * inicial del guardián no es ni avería ni permiso, y tratarlo como estado
     * anterior produciría un aviso espurio en el primer ciclo. */
    condition_event_t event = condition_monitor_observe(
        &monitor, &(actuation_decision_t){.pump = ACTUATION_UNDECIDED,
                                          .heater = ACTUATION_UNDECIDED});
    assert(event.kind == CONDITION_EVENT_NONE);

    /* Una inhibición normal de servicio no es una condición insegura: sin esta
     * distinción, toda la puesta en marcha generaría alarmas permanentes. */
    actuation_decision_t normal = decision(ACTUATION_INHIBITED_NOT_COMMISSIONED,
                                           ACTUATION_INHIBITED_NOT_COMMISSIONED);
    event = condition_monitor_observe(&monitor, &normal);
    assert(event.kind == CONDITION_EVENT_NONE);

    /* Una avería sí informa, y dice qué salida la declaró. */
    actuation_decision_t fault = decision(ACTUATION_INHIBITED_NOT_COMMISSIONED,
                                          ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);
    event = condition_monitor_observe(&monitor, &fault);
    assert(event.kind == CONDITION_EVENT_FAULT_RAISED);
    assert(event.verdict == ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);
    assert(event.from_pump == false);

    /* Mientras la avería dura no se repite: se observan flancos, no niveles, y
     * publicar cada ciclo inundaría la cola que guarda la evidencia. */
    event = condition_monitor_observe(&monitor, &fault);
    assert(event.kind == CONDITION_EVENT_NONE);

    /* Al despejarse se informa, conservando el veredicto que la había provocado:
     * sin el aviso de despeje el historial dejaría la avería abierta para
     * siempre. */
    event = condition_monitor_observe(&monitor, &normal);
    assert(event.kind == CONDITION_EVENT_FAULT_CLEARED);
    assert(event.verdict == ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);

    /* El aviso se serializa según el contrato, y entrar en la condición es
     * alarma mientras que salir de ella es diagnóstico. */
    char buffer[EVENT_MESSAGE_JSON_BYTES];
    char message_id[EVENT_MESSAGE_ID_LENGTH + 1U];
    condition_event_context_t context = {
        .device_id = "espresso-lab-01",
        .sequence = 5U,
        .epoch_ms = UINT64_C(1789999200000),
        .preparation_id = NULL,
        .state = OP_STATE_READY,
    };
    context.condition.kind = CONDITION_EVENT_FAULT_RAISED;
    context.condition.verdict = ACTUATION_INHIBITED_SIGNAL_NOT_USABLE;
    context.condition.from_pump = false;
    assert(event_message_build_condition(&context, buffer, sizeof(buffer), message_id,
                                         sizeof(message_id)) == EVENT_MESSAGE_OK);
    assert(strstr(buffer, "\"kind\":\"alarm\"") != NULL);
    assert(strstr(buffer, "\"severity\":\"critical\"") != NULL);
    assert(strstr(buffer, "\"previousState\":null") != NULL);

    context.condition.kind = CONDITION_EVENT_FAULT_CLEARED;
    assert(event_message_build_condition(&context, buffer, sizeof(buffer), message_id,
                                         sizeof(message_id)) == EVENT_MESSAGE_OK);
    assert(strstr(buffer, "\"kind\":\"diagnostic\"") != NULL);
    assert(strstr(buffer, "\"severity\":\"information\"") != NULL);

    if (argc > 1) {
        context.condition.kind = CONDITION_EVENT_FAULT_RAISED;
        assert(event_message_build_condition(&context, buffer, sizeof(buffer), message_id,
                                             sizeof(message_id)) == EVENT_MESSAGE_OK);
        FILE *file = fopen(argv[1], "w");
        assert(file != NULL);
        fputs(buffer, file);
        fclose(file);
    }

    printf("condition_monitor: 7 casos correctos\n");
    return 0;
}
