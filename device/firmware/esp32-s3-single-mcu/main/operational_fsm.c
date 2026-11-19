#include "operational_fsm.h"

#include <stddef.h>

static void accept_transition(operational_transition_t *transition,
                              operational_fsm_t *fsm,
                              operational_state_t next_state)
{
    transition->accepted = true;
    transition->to = next_state;
    fsm->state = next_state;
}

static void finish_extraction(operational_transition_t *transition,
                              operational_fsm_t *fsm,
                              extraction_termination_t termination)
{
    transition->extraction_finished = true;
    transition->extraction_termination = termination;
    fsm->last_extraction_termination = termination;
    fsm->extraction_open = false;
}

void operational_fsm_init(operational_fsm_t *fsm)
{
    if (fsm == NULL) {
        return;
    }

    *fsm = (operational_fsm_t) {
        .state = OP_STATE_SAFE_INACTIVE,
        .last_extraction_termination = OP_TERMINATION_NONE,
        .extraction_open = false,
    };
}

operational_transition_t operational_fsm_dispatch(operational_fsm_t *fsm,
                                                   operational_event_t event)
{
    operational_transition_t transition = {
        .from = OP_STATE_SAFE_INACTIVE,
        .to = OP_STATE_SAFE_INACTIVE,
        .event = event,
        .extraction_termination = OP_TERMINATION_NONE,
        .accepted = false,
        .extraction_started = false,
        .extraction_finished = false,
    };

    if (fsm == NULL) {
        return transition;
    }

    transition.from = fsm->state;
    transition.to = fsm->state;

    /* A safety trip has priority over every operational request. */
    if (event == OP_EVENT_SAFETY_TRIP && fsm->state != OP_STATE_ALARM) {
        if (fsm->state == OP_STATE_EXTRACTION && fsm->extraction_open) {
            finish_extraction(&transition, fsm, OP_TERMINATION_SAFETY_PROTECTION_TRIP);
        }
        accept_transition(&transition, fsm, OP_STATE_ALARM);
        return transition;
    }

    if (event == OP_EVENT_MAINS_LOST && fsm->state != OP_STATE_SAFE_INACTIVE &&
        fsm->state != OP_STATE_ALARM) {
        if (fsm->state == OP_STATE_EXTRACTION && fsm->extraction_open) {
            finish_extraction(&transition, fsm, OP_TERMINATION_UNEXPECTED_POWER_LOSS);
        }
        accept_transition(&transition, fsm, OP_STATE_SAFE_INACTIVE);
        return transition;
    }

    switch (fsm->state) {
    case OP_STATE_SAFE_INACTIVE:
        if (event == OP_EVENT_LOGICAL_POWER_TOGGLE) {
            accept_transition(&transition, fsm, OP_STATE_COFFEE_HEATING);
        }
        break;

    case OP_STATE_COFFEE_HEATING:
        if (event == OP_EVENT_LOGICAL_POWER_TOGGLE) {
            accept_transition(&transition, fsm, OP_STATE_SAFE_INACTIVE);
        } else if (event == OP_EVENT_COFFEE_TEMPERATURE_READY) {
            accept_transition(&transition, fsm, OP_STATE_READY);
        }
        break;

    case OP_STATE_READY:
        if (event == OP_EVENT_LOGICAL_POWER_TOGGLE) {
            accept_transition(&transition, fsm, OP_STATE_SAFE_INACTIVE);
        } else if (event == OP_EVENT_COFFEE_TEMPERATURE_LOST) {
            accept_transition(&transition, fsm, OP_STATE_COFFEE_HEATING);
        } else if (event == OP_EVENT_BREW_BUTTON_PRESSED) {
            fsm->extraction_open = true;
            fsm->last_extraction_termination = OP_TERMINATION_NONE;
            transition.extraction_started = true;
            accept_transition(&transition, fsm, OP_STATE_EXTRACTION);
        } else if (event == OP_EVENT_PURGE_BUTTON_PRESSED) {
            accept_transition(&transition, fsm, OP_STATE_MANUAL_PURGE);
        }
        break;

    case OP_STATE_EXTRACTION:
        if (event == OP_EVENT_LOGICAL_POWER_TOGGLE) {
            finish_extraction(&transition, fsm, OP_TERMINATION_MANUAL_NORMAL_STOP);
            accept_transition(&transition, fsm, OP_STATE_SAFE_INACTIVE);
        } else if (event == OP_EVENT_BREW_BUTTON_PRESSED ||
                   event == OP_EVENT_LOCAL_NORMAL_STOP) {
            finish_extraction(&transition, fsm, OP_TERMINATION_MANUAL_NORMAL_STOP);
            accept_transition(&transition, fsm, OP_STATE_READY);
        } else if (event == OP_EVENT_EXTRACTION_TIME_LIMIT) {
            finish_extraction(&transition, fsm,
                              OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED);
            accept_transition(&transition, fsm, OP_STATE_READY);
        }
        break;

    case OP_STATE_MANUAL_PURGE:
        if (event == OP_EVENT_LOGICAL_POWER_TOGGLE) {
            accept_transition(&transition, fsm, OP_STATE_SAFE_INACTIVE);
        } else if (event == OP_EVENT_PURGE_BUTTON_RELEASED ||
                   event == OP_EVENT_PURGE_TIME_LIMIT) {
            accept_transition(&transition, fsm, OP_STATE_READY);
        }
        break;

    case OP_STATE_ALARM:
        /* Recovery never resumes an interrupted operation automatically. */
        if (event == OP_EVENT_SAFETY_CLEARED) {
            accept_transition(&transition, fsm, OP_STATE_SAFE_INACTIVE);
        }
        break;

    default:
        fsm->state = OP_STATE_ALARM;
        transition.to = OP_STATE_ALARM;
        transition.accepted = true;
        break;
    }

    return transition;
}

operational_transition_t operational_fsm_dispatch_highest_priority(
    operational_fsm_t *fsm, uint32_t pending_events)
{
    /*
     * Safety always wins. In an extraction, a physical or local normal stop
     * wins over the maximum-time event, as established by ADR-005.
     * Temperature loss precedes a new operational request so READY cannot
     * start an extraction from a stale thermal condition.
     */
    static const operational_event_t priority[] = {
        OP_EVENT_SAFETY_TRIP,
        OP_EVENT_MAINS_LOST,
        OP_EVENT_LOGICAL_POWER_TOGGLE,
        OP_EVENT_COFFEE_TEMPERATURE_LOST,
        OP_EVENT_BREW_BUTTON_PRESSED,
        OP_EVENT_LOCAL_NORMAL_STOP,
        OP_EVENT_EXTRACTION_TIME_LIMIT,
        OP_EVENT_PURGE_BUTTON_RELEASED,
        OP_EVENT_PURGE_TIME_LIMIT,
        OP_EVENT_COFFEE_TEMPERATURE_READY,
        OP_EVENT_PURGE_BUTTON_PRESSED,
        OP_EVENT_SAFETY_CLEARED,
    };

    for (size_t index = 0; index < sizeof(priority) / sizeof(priority[0]); ++index) {
        if ((pending_events & OP_EVENT_MASK(priority[index])) != 0U) {
            return operational_fsm_dispatch(fsm, priority[index]);
        }
    }

    operational_transition_t transition = {
        .from = fsm == NULL ? OP_STATE_SAFE_INACTIVE : fsm->state,
        .to = fsm == NULL ? OP_STATE_SAFE_INACTIVE : fsm->state,
        .event = OP_EVENT_COUNT,
    };
    return transition;
}

operational_authorization_t operational_fsm_authorization(const operational_fsm_t *fsm)
{
    operational_authorization_t authorization = {0};
    if (fsm == NULL) {
        return authorization;
    }

    switch (fsm->state) {
    case OP_STATE_COFFEE_HEATING:
    case OP_STATE_READY:
        authorization.heater_regulation_allowed = true;
        break;
    case OP_STATE_EXTRACTION:
        authorization.heater_regulation_allowed = true;
        authorization.pressure_regulation_allowed = true;
        authorization.extraction_pump_allowed = true;
        break;
    case OP_STATE_MANUAL_PURGE:
        authorization.heater_regulation_allowed = true;
        authorization.purge_pump_allowed = true;
        break;
    case OP_STATE_SAFE_INACTIVE:
    case OP_STATE_ALARM:
    default:
        break;
    }

    return authorization;
}

bool operational_fsm_tare_allowed(operational_state_t state)
{
    switch (state) {
    case OP_STATE_SAFE_INACTIVE:
    case OP_STATE_COFFEE_HEATING:
    case OP_STATE_READY:
    case OP_STATE_ALARM:
        return true;
    /* Un estado desconocido no autoriza nada, tampoco una tara. */
    case OP_STATE_EXTRACTION:
    case OP_STATE_MANUAL_PURGE:
    default:
        return false;
    }
}

const char *operational_state_name(operational_state_t state)
{
    static const char *const names[] = {
        [OP_STATE_SAFE_INACTIVE] = "safe-inactive",
        [OP_STATE_COFFEE_HEATING] = "coffee-heating",
        [OP_STATE_READY] = "ready",
        [OP_STATE_EXTRACTION] = "extraction",
        [OP_STATE_MANUAL_PURGE] = "manual-purge",
        [OP_STATE_ALARM] = "alarm",
    };
    return state >= 0 && (size_t)state < sizeof(names) / sizeof(names[0])
               ? names[state]
               : "invalid-state";
}

const char *operational_event_name(operational_event_t event)
{
    static const char *const names[] = {
        [OP_EVENT_LOGICAL_POWER_TOGGLE] = "logical-power-toggle",
        [OP_EVENT_COFFEE_TEMPERATURE_READY] = "coffee-temperature-ready",
        [OP_EVENT_COFFEE_TEMPERATURE_LOST] = "coffee-temperature-lost",
        [OP_EVENT_BREW_BUTTON_PRESSED] = "brew-button-pressed",
        [OP_EVENT_LOCAL_NORMAL_STOP] = "local-normal-stop",
        [OP_EVENT_PURGE_BUTTON_PRESSED] = "purge-button-pressed",
        [OP_EVENT_PURGE_BUTTON_RELEASED] = "purge-button-released",
        [OP_EVENT_EXTRACTION_TIME_LIMIT] = "extraction-time-limit",
        [OP_EVENT_PURGE_TIME_LIMIT] = "purge-time-limit",
        [OP_EVENT_SAFETY_TRIP] = "safety-trip",
        [OP_EVENT_SAFETY_CLEARED] = "safety-cleared",
        [OP_EVENT_MAINS_LOST] = "mains-lost",
    };
    return event >= 0 && (size_t)event < sizeof(names) / sizeof(names[0]) &&
                   names[event] != NULL
               ? names[event]
               : "invalid-event";
}

const char *extraction_termination_name(extraction_termination_t termination)
{
    static const char *const names[] = {
        [OP_TERMINATION_NONE] = "none",
        [OP_TERMINATION_MANUAL_NORMAL_STOP] = "manual-normal-stop",
        [OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED] =
            "maximum-extraction-time-reached",
        [OP_TERMINATION_SAFETY_PROTECTION_TRIP] = "safety-protection-trip",
        [OP_TERMINATION_UNEXPECTED_POWER_LOSS] = "unexpected-power-loss",
    };
    return termination >= 0 &&
                   (size_t)termination < sizeof(names) / sizeof(names[0])
               ? names[termination]
               : "invalid-termination";
}

bool operational_event_is_operator_press(operational_event_t event)
{
    return event == OP_EVENT_LOGICAL_POWER_TOGGLE || event == OP_EVENT_BREW_BUTTON_PRESSED ||
           event == OP_EVENT_PURGE_BUTTON_PRESSED;
}
