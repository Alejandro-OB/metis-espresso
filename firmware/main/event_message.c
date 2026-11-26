#include "event_message.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "extraction_message.h"

/*
 * Gravedad del evento. La alarma es la única condición que exige atención
 * inmediata; una preparación que termina por protección merece constancia, pero
 * no se declara crítica porque la máquina ya se puso a salvo por sí misma.
 */
static const char *severity_name(const operational_transition_t *transition)
{
    if (transition->to == OP_STATE_ALARM) {
        return "critical";
    }
    if (transition->extraction_finished &&
        transition->extraction_termination != OP_TERMINATION_MANUAL_NORMAL_STOP) {
        return "warning";
    }
    return "information";
}

static const char *kind_name(const operational_transition_t *transition)
{
    return transition->to == OP_STATE_ALARM ? "alarm" : "state-transition";
}

event_message_status_t event_message_build_condition(const condition_event_context_t *context,
                                                     char *buffer,
                                                     size_t buffer_size,
                                                     char *message_id,
                                                     size_t message_id_size)
{
    if (context == NULL || buffer == NULL || buffer_size == 0U || message_id == NULL ||
        message_id_size < EVENT_MESSAGE_ID_LENGTH + 1U || context->device_id == NULL) {
        return EVENT_MESSAGE_INVALID_ARGUMENT;
    }
    if (context->condition.kind == CONDITION_EVENT_NONE) {
        return EVENT_MESSAGE_NOT_REPORTABLE;
    }
    if (context->epoch_ms < EXTRACTION_MESSAGE_MINIMUM_EPOCH_MS) {
        return EVENT_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    char occurred_at[32];
    if (!extraction_message_format_instant(context->epoch_ms, occurred_at, sizeof(occurred_at))) {
        return EVENT_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    const int identifier_written = snprintf(message_id, message_id_size, "cnd-%" PRIu64 "-%08" PRIu32,
                                            context->epoch_ms, context->sequence);
    if (identifier_written <= 0 || (size_t)identifier_written >= message_id_size) {
        return EVENT_MESSAGE_BUFFER_TOO_SMALL;
    }

    char preparation[EVENT_MESSAGE_ID_LENGTH + 3U];
    if (context->preparation_id != NULL && context->preparation_id[0] != '\0') {
        snprintf(preparation, sizeof(preparation), "\"%s\"", context->preparation_id);
    } else {
        snprintf(preparation, sizeof(preparation), "null");
    }

    /* Entrar en la condición es una alarma; salir de ella no lo es, pero debe
     * constar: sin el aviso de despeje, el historial dejaría una avería abierta
     * para siempre. */
    const bool raised = context->condition.kind == CONDITION_EVENT_FAULT_RAISED;
    const char *output = context->condition.from_pump ? "la bomba" : "el calentador";
    char description[240];
    snprintf(description, sizeof(description),
             raised ? "Condición insegura detectada en %s: %s; las salidas quedan inhibidas"
                    : "La condición de %s quedó despejada: %s",
             output, actuation_verdict_name(context->condition.verdict));

    const int written = snprintf(
        buffer, buffer_size,
        "{\"schemaVersion\":\"1.0\""
        ",\"messageId\":\"%s\""
        ",\"messageType\":\"operational.event\""
        ",\"deviceId\":\"%s\""
        ",\"sequence\":%" PRIu32
        ",\"occurredAt\":\"%s\""
        ",\"payload\":{"
        "\"preparationId\":%s"
        ",\"kind\":\"%s\""
        ",\"state\":\"%s\""
        ",\"previousState\":null"
        ",\"severity\":\"%s\""
        ",\"code\":\"%s\""
        ",\"description\":\"%s\""
        "}}",
        message_id, context->device_id, context->sequence, occurred_at, preparation,
        raised ? "alarm" : "diagnostic", operational_state_name(context->state),
        raised ? "critical" : "information",
        actuation_verdict_name(context->condition.verdict), description);

    if (written <= 0 || (size_t)written >= buffer_size) {
        return EVENT_MESSAGE_BUFFER_TOO_SMALL;
    }
    return EVENT_MESSAGE_OK;
}

const char *event_message_status_name(event_message_status_t status)
{
    switch (status) {
    case EVENT_MESSAGE_OK:
        return "ok";
    case EVENT_MESSAGE_INVALID_ARGUMENT:
        return "invalid-argument";
    case EVENT_MESSAGE_BUFFER_TOO_SMALL:
        return "buffer-too-small";
    case EVENT_MESSAGE_TIME_BASE_UNAVAILABLE:
        return "time-base-unavailable";
    case EVENT_MESSAGE_NOT_REPORTABLE:
        return "not-reportable";
    default:
        return "unknown";
    }
}

event_message_status_t event_message_build(const event_message_context_t *context,
                                           char *buffer,
                                           size_t buffer_size,
                                           char *message_id,
                                           size_t message_id_size)
{
    if (context == NULL || buffer == NULL || buffer_size == 0U || message_id == NULL ||
        message_id_size < EVENT_MESSAGE_ID_LENGTH + 1U || context->device_id == NULL) {
        return EVENT_MESSAGE_INVALID_ARGUMENT;
    }

    const operational_transition_t *transition = &context->transition;
    /* Una transición rechazada, o que no movió el estado, no es un hecho que
     * comunicar: publicarla llenaría el historial de sucesos que no ocurrieron. */
    if (!transition->accepted || transition->from == transition->to) {
        return EVENT_MESSAGE_NOT_REPORTABLE;
    }

    if (context->epoch_ms < EXTRACTION_MESSAGE_MINIMUM_EPOCH_MS) {
        return EVENT_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    char occurred_at[32];
    if (!extraction_message_format_instant(context->epoch_ms, occurred_at, sizeof(occurred_at))) {
        return EVENT_MESSAGE_TIME_BASE_UNAVAILABLE;
    }

    const int identifier_written = snprintf(message_id, message_id_size, "evt-%" PRIu64 "-%08" PRIu32,
                                            context->epoch_ms, context->sequence);
    if (identifier_written <= 0 || (size_t)identifier_written >= message_id_size) {
        return EVENT_MESSAGE_BUFFER_TOO_SMALL;
    }

    char preparation[EVENT_MESSAGE_ID_LENGTH + 3U];
    if (context->preparation_id != NULL && context->preparation_id[0] != '\0') {
        snprintf(preparation, sizeof(preparation), "\"%s\"", context->preparation_id);
    } else {
        snprintf(preparation, sizeof(preparation), "null");
    }

    /* La descripción nombra el hecho en los términos del dominio; el código
     * conserva el evento que lo provocó, que es lo que permite agrupar después. */
    char description[240];
    if (transition->extraction_finished) {
        snprintf(description, sizeof(description),
                 "La preparación terminó por %s; la máquina pasó de %s a %s",
                 extraction_termination_name(transition->extraction_termination),
                 operational_state_name(transition->from),
                 operational_state_name(transition->to));
    } else {
        snprintf(description, sizeof(description), "La máquina pasó de %s a %s por %s",
                 operational_state_name(transition->from),
                 operational_state_name(transition->to),
                 operational_event_name(transition->event));
    }

    const int written = snprintf(
        buffer, buffer_size,
        "{\"schemaVersion\":\"1.0\""
        ",\"messageId\":\"%s\""
        ",\"messageType\":\"operational.event\""
        ",\"deviceId\":\"%s\""
        ",\"sequence\":%" PRIu32
        ",\"occurredAt\":\"%s\""
        ",\"payload\":{"
        "\"preparationId\":%s"
        ",\"kind\":\"%s\""
        ",\"state\":\"%s\""
        ",\"previousState\":\"%s\""
        ",\"severity\":\"%s\""
        ",\"code\":\"%s\""
        ",\"description\":\"%s\""
        "}}",
        message_id, context->device_id, context->sequence, occurred_at, preparation,
        kind_name(transition), operational_state_name(transition->to),
        operational_state_name(transition->from), severity_name(transition),
        operational_event_name(transition->event), description);

    if (written <= 0 || (size_t)written >= buffer_size) {
        return EVENT_MESSAGE_BUFFER_TOO_SMALL;
    }
    return EVENT_MESSAGE_OK;
}
