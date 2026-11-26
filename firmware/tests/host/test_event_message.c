/* Verificación en host de la serialización de un evento operativo. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "event_message.h"

#define BUFFER_BYTES EVENT_MESSAGE_JSON_BYTES

static event_message_context_t transition_context(operational_state_t from,
                                                  operational_state_t to,
                                                  operational_event_t event)
{
    event_message_context_t context = {
        .device_id = "espresso-lab-01",
        .sequence = 3U,
        .epoch_ms = UINT64_C(1789999200000),
        .preparation_id = NULL,
    };
    context.transition.from = from;
    context.transition.to = to;
    context.transition.event = event;
    context.transition.accepted = true;
    return context;
}

int main(int argc, char **argv)
{
    char buffer[BUFFER_BYTES];
    char message_id[EVENT_MESSAGE_ID_LENGTH + 1U];

    /* Un cambio de estado aceptado se publica como tal. */
    event_message_context_t context =
        transition_context(OP_STATE_READY, OP_STATE_EXTRACTION, OP_EVENT_BREW_BUTTON_PRESSED);
    assert(event_message_build(&context, buffer, sizeof(buffer), message_id,
                               sizeof(message_id)) == EVENT_MESSAGE_OK);
    assert(strstr(buffer, "\"messageType\":\"operational.event\"") != NULL);
    assert(strstr(buffer, "\"kind\":\"state-transition\"") != NULL);
    assert(strstr(buffer, "\"severity\":\"information\"") != NULL);
    assert(strstr(buffer, "\"state\":\"extraction\"") != NULL);
    assert(strstr(buffer, "\"previousState\":\"ready\"") != NULL);

    /* Una transición rechazada no ocurrió: no se publica. */
    context = transition_context(OP_STATE_READY, OP_STATE_EXTRACTION, OP_EVENT_BREW_BUTTON_PRESSED);
    context.transition.accepted = false;
    assert(event_message_build(&context, buffer, sizeof(buffer), message_id,
                               sizeof(message_id)) == EVENT_MESSAGE_NOT_REPORTABLE);

    /* Una transición que no mueve el estado tampoco es un hecho que comunicar. */
    context = transition_context(OP_STATE_READY, OP_STATE_READY, OP_EVENT_BREW_BUTTON_PRESSED);
    assert(event_message_build(&context, buffer, sizeof(buffer), message_id,
                               sizeof(message_id)) == EVENT_MESSAGE_NOT_REPORTABLE);

    /* La alarma es la única condición crítica. */
    context = transition_context(OP_STATE_EXTRACTION, OP_STATE_ALARM, OP_EVENT_SAFETY_TRIP);
    assert(event_message_build(&context, buffer, sizeof(buffer), message_id,
                               sizeof(message_id)) == EVENT_MESSAGE_OK);
    assert(strstr(buffer, "\"kind\":\"alarm\"") != NULL);
    assert(strstr(buffer, "\"severity\":\"critical\"") != NULL);

    /* Sin hora fiable no se publica un instante inventado. */
    context = transition_context(OP_STATE_READY, OP_STATE_EXTRACTION, OP_EVENT_BREW_BUTTON_PRESSED);
    context.epoch_ms = 1000U;
    assert(event_message_build(&context, buffer, sizeof(buffer), message_id,
                               sizeof(message_id)) == EVENT_MESSAGE_TIME_BASE_UNAVAILABLE);

    if (argc > 1) {
        context = transition_context(OP_STATE_READY, OP_STATE_EXTRACTION, OP_EVENT_BREW_BUTTON_PRESSED);
        context.preparation_id = "sim-00000042";
        assert(event_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == EVENT_MESSAGE_OK);
        FILE *file = fopen(argv[1], "w");
        assert(file != NULL);
        fputs(buffer, file);
        fclose(file);
    }

    printf("event_message: 5 casos correctos\n");
    return 0;
}
