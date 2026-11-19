#include "condition_monitor.h"

#include <string.h>

void condition_monitor_init(condition_monitor_t *monitor)
{
    if (monitor == NULL) {
        return;
    }
    memset(monitor, 0, sizeof(*monitor));
    monitor->pump = ACTUATION_UNDECIDED;
    monitor->heater = ACTUATION_UNDECIDED;
}

/* Veredicto responsable de la avería, si alguno lo es. */
static bool find_fault(const actuation_decision_t *decision,
                       actuation_verdict_t *verdict,
                       bool *from_pump)
{
    if (actuation_verdict_is_fault(decision->pump)) {
        *verdict = decision->pump;
        *from_pump = true;
        return true;
    }
    if (actuation_verdict_is_fault(decision->heater)) {
        *verdict = decision->heater;
        *from_pump = false;
        return true;
    }
    return false;
}

condition_event_t condition_monitor_observe(condition_monitor_t *monitor,
                                            const actuation_decision_t *decision)
{
    condition_event_t event = {.kind = CONDITION_EVENT_NONE,
                               .verdict = ACTUATION_UNDECIDED,
                               .from_pump = false};
    if (monitor == NULL || decision == NULL) {
        return event;
    }

    const actuation_decision_t previous = {.pump = monitor->pump, .heater = monitor->heater};
    actuation_verdict_t verdict = ACTUATION_UNDECIDED;
    bool from_pump = false;
    const bool fault_now = find_fault(decision, &verdict, &from_pump);

    actuation_verdict_t previous_verdict = ACTUATION_UNDECIDED;
    bool previous_from_pump = false;
    const bool fault_before =
        monitor->known && find_fault(&previous, &previous_verdict, &previous_from_pump);

    if (!monitor->known) {
        /* La primera observación fija la referencia: no hay cambio que informar
         * porque no había estado anterior con el que comparar. */
        monitor->known = true;
    } else if (fault_now && !fault_before) {
        event.kind = CONDITION_EVENT_FAULT_RAISED;
        event.verdict = verdict;
        event.from_pump = from_pump;
    } else if (!fault_now && fault_before) {
        event.kind = CONDITION_EVENT_FAULT_CLEARED;
        event.verdict = previous_verdict;
        event.from_pump = previous_from_pump;
    }

    monitor->pump = decision->pump;
    monitor->heater = decision->heater;
    return event;
}
