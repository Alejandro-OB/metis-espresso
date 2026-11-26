#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "operational_fsm.h"

static operational_transition_t dispatch_expect(operational_fsm_t *fsm,
                                                 operational_event_t event,
                                                 bool accepted,
                                                 operational_state_t state)
{
    const operational_transition_t transition = operational_fsm_dispatch(fsm, event);
    assert(transition.accepted == accepted);
    assert(fsm->state == state);
    assert(transition.to == state);
    return transition;
}

static void reach_ready(operational_fsm_t *fsm)
{
    operational_fsm_init(fsm);
    dispatch_expect(fsm, OP_EVENT_LOGICAL_POWER_TOGGLE, true,
                    OP_STATE_COFFEE_HEATING);
    dispatch_expect(fsm, OP_EVENT_COFFEE_TEMPERATURE_READY, true,
                    OP_STATE_READY);
}

static void test_safe_start_and_rejections(void)
{
    operational_fsm_t fsm;
    operational_fsm_init(&fsm);
    assert(fsm.state == OP_STATE_SAFE_INACTIVE);
    assert(!operational_fsm_authorization(&fsm).heater_regulation_allowed);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, false,
                    OP_STATE_SAFE_INACTIVE);
    dispatch_expect(&fsm, OP_EVENT_PURGE_BUTTON_PRESSED, false,
                    OP_STATE_SAFE_INACTIVE);
}

static void test_manual_extraction(void)
{
    operational_fsm_t fsm;
    reach_ready(&fsm);
    operational_transition_t transition = dispatch_expect(
        &fsm, OP_EVENT_BREW_BUTTON_PRESSED, true, OP_STATE_EXTRACTION);
    assert(transition.extraction_started && fsm.extraction_open);
    const operational_authorization_t authorization =
        operational_fsm_authorization(&fsm);
    assert(authorization.heater_regulation_allowed);
    assert(authorization.pressure_regulation_allowed);
    assert(authorization.extraction_pump_allowed);
    assert(!authorization.purge_pump_allowed);

    transition = dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true,
                                 OP_STATE_READY);
    assert(transition.extraction_finished && !fsm.extraction_open);
    assert(transition.extraction_termination ==
           OP_TERMINATION_MANUAL_NORMAL_STOP);
}

static void test_local_stop_and_time_limit(void)
{
    operational_fsm_t fsm;
    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true,
                    OP_STATE_EXTRACTION);
    operational_transition_t transition = dispatch_expect(
        &fsm, OP_EVENT_LOCAL_NORMAL_STOP, true, OP_STATE_READY);
    assert(transition.extraction_termination ==
           OP_TERMINATION_MANUAL_NORMAL_STOP);

    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true,
                    OP_STATE_EXTRACTION);
    transition = dispatch_expect(&fsm, OP_EVENT_EXTRACTION_TIME_LIMIT, true,
                                 OP_STATE_READY);
    assert(transition.extraction_termination ==
           OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED);
}

static void test_purge_is_hold_to_run(void)
{
    operational_fsm_t fsm;
    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_PURGE_BUTTON_PRESSED, true,
                    OP_STATE_MANUAL_PURGE);
    const operational_authorization_t authorization =
        operational_fsm_authorization(&fsm);
    assert(authorization.purge_pump_allowed);
    assert(!authorization.extraction_pump_allowed);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, false,
                    OP_STATE_MANUAL_PURGE);
    dispatch_expect(&fsm, OP_EVENT_PURGE_BUTTON_RELEASED, true,
                    OP_STATE_READY);
}

/*
 * La tara se rechaza exactamente donde cae agua en la taza.
 *
 * Se prueba la propiedad y no una lista: una tara es admisible si y solo si el
 * estado no autoriza la bomba. Así, un estado nuevo que bombee queda cubierto
 * sin acordarse de esta función, y uno que no bombee no la bloquea por olvido.
 */
static void test_tare_only_while_no_water_falls(void)
{
    for (int state = 0; state <= (int)OP_STATE_ALARM; state++) {
        operational_fsm_t fsm;
        operational_fsm_init(&fsm);
        fsm.state = (operational_state_t)state;
        const operational_authorization_t authorization =
            operational_fsm_authorization(&fsm);
        const bool water_falls = authorization.extraction_pump_allowed ||
                                 authorization.purge_pump_allowed;
        assert(operational_fsm_tare_allowed(fsm.state) == !water_falls);
    }

    /* Un estado fuera de la enumeración no autoriza nada, tampoco una tara. */
    assert(!operational_fsm_tare_allowed((operational_state_t)((int)OP_STATE_ALARM + 1)));
}

static void test_safety_has_priority_and_no_restart(void)
{
    operational_fsm_t fsm;
    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true,
                    OP_STATE_EXTRACTION);
    operational_transition_t transition = dispatch_expect(
        &fsm, OP_EVENT_SAFETY_TRIP, true, OP_STATE_ALARM);
    assert(transition.extraction_finished);
    assert(transition.extraction_termination ==
           OP_TERMINATION_SAFETY_PROTECTION_TRIP);
    const operational_authorization_t authorization =
        operational_fsm_authorization(&fsm);
    assert(!authorization.heater_regulation_allowed);
    assert(!authorization.extraction_pump_allowed);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, false, OP_STATE_ALARM);
    dispatch_expect(&fsm, OP_EVENT_LOGICAL_POWER_TOGGLE, false, OP_STATE_ALARM);
    dispatch_expect(&fsm, OP_EVENT_SAFETY_CLEARED, true,
                    OP_STATE_SAFE_INACTIVE);
    assert(!fsm.extraction_open);
}

static void test_logical_power_off_is_safe(void)
{
    operational_fsm_t fsm;
    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true,
                    OP_STATE_EXTRACTION);
    const operational_transition_t transition = dispatch_expect(
        &fsm, OP_EVENT_LOGICAL_POWER_TOGGLE, true, OP_STATE_SAFE_INACTIVE);
    assert(transition.extraction_termination ==
           OP_TERMINATION_MANUAL_NORMAL_STOP);
    assert(!operational_fsm_authorization(&fsm).extraction_pump_allowed);
}

static void test_termination_priority(void)
{
    operational_fsm_t fsm;
    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true,
                    OP_STATE_EXTRACTION);

    const uint32_t simultaneous_requests =
        OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP) |
        OP_EVENT_MASK(OP_EVENT_BREW_BUTTON_PRESSED) |
        OP_EVENT_MASK(OP_EVENT_LOCAL_NORMAL_STOP) |
        OP_EVENT_MASK(OP_EVENT_EXTRACTION_TIME_LIMIT);
    const operational_transition_t transition =
        operational_fsm_dispatch_highest_priority(&fsm, simultaneous_requests);
    assert(transition.accepted);
    assert(transition.event == OP_EVENT_SAFETY_TRIP);
    assert(transition.to == OP_STATE_ALARM);
    assert(transition.extraction_termination ==
           OP_TERMINATION_SAFETY_PROTECTION_TRIP);

    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true,
                    OP_STATE_EXTRACTION);
    const operational_transition_t manual =
        operational_fsm_dispatch_highest_priority(
            &fsm, OP_EVENT_MASK(OP_EVENT_LOCAL_NORMAL_STOP) |
                      OP_EVENT_MASK(OP_EVENT_EXTRACTION_TIME_LIMIT));
    assert(manual.event == OP_EVENT_LOCAL_NORMAL_STOP);
    assert(manual.extraction_termination == OP_TERMINATION_MANUAL_NORMAL_STOP);
}


/* Solo las pulsaciones del barista cuentan como órdenes que esperan respuesta:
 * el panel destella rojo si se rechazan. Soltar el purgado y lo que publica el
 * supervisor no. */
static void test_operator_presses_are_the_three_buttons(void)
{
    assert(operational_event_is_operator_press(OP_EVENT_LOGICAL_POWER_TOGGLE));
    assert(operational_event_is_operator_press(OP_EVENT_BREW_BUTTON_PRESSED));
    assert(operational_event_is_operator_press(OP_EVENT_PURGE_BUTTON_PRESSED));
    assert(!operational_event_is_operator_press(OP_EVENT_PURGE_BUTTON_RELEASED));
    assert(!operational_event_is_operator_press(OP_EVENT_SAFETY_TRIP));
    assert(!operational_event_is_operator_press(OP_EVENT_SAFETY_CLEARED));
    assert(!operational_event_is_operator_press(OP_EVENT_COFFEE_TEMPERATURE_READY));
}


/* Sin red, la máquina vuelve a reposo desde cualquier estado encendido, sin
 * alarma; un café en curso se cierra como pérdida de energía. En reposo y en
 * alarma no cambia nada, y la falta de red gana a cualquier orden salvo la
 * protección. */
static void test_mains_loss_returns_to_rest(void)
{
    operational_fsm_t fsm;
    operational_fsm_init(&fsm);
    dispatch_expect(&fsm, OP_EVENT_MAINS_LOST, false, OP_STATE_SAFE_INACTIVE);

    dispatch_expect(&fsm, OP_EVENT_LOGICAL_POWER_TOGGLE, true, OP_STATE_COFFEE_HEATING);
    dispatch_expect(&fsm, OP_EVENT_MAINS_LOST, true, OP_STATE_SAFE_INACTIVE);

    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_MAINS_LOST, true, OP_STATE_SAFE_INACTIVE);

    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_PURGE_BUTTON_PRESSED, true, OP_STATE_MANUAL_PURGE);
    dispatch_expect(&fsm, OP_EVENT_MAINS_LOST, true, OP_STATE_SAFE_INACTIVE);

    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_BREW_BUTTON_PRESSED, true, OP_STATE_EXTRACTION);
    const operational_transition_t lost =
        dispatch_expect(&fsm, OP_EVENT_MAINS_LOST, true, OP_STATE_SAFE_INACTIVE);
    assert(lost.extraction_finished && !fsm.extraction_open);
    assert(lost.extraction_termination == OP_TERMINATION_UNEXPECTED_POWER_LOSS);

    reach_ready(&fsm);
    dispatch_expect(&fsm, OP_EVENT_SAFETY_TRIP, true, OP_STATE_ALARM);
    dispatch_expect(&fsm, OP_EVENT_MAINS_LOST, false, OP_STATE_ALARM);

    reach_ready(&fsm);
    const operational_transition_t first = operational_fsm_dispatch_highest_priority(
        &fsm, OP_EVENT_MASK(OP_EVENT_MAINS_LOST) | OP_EVENT_MASK(OP_EVENT_BREW_BUTTON_PRESSED));
    assert(first.event == OP_EVENT_MAINS_LOST && fsm.state == OP_STATE_SAFE_INACTIVE);
}

int main(void)
{
    test_mains_loss_returns_to_rest();
    test_operator_presses_are_the_three_buttons();
    test_safe_start_and_rejections();
    test_manual_extraction();
    test_local_stop_and_time_limit();
    test_purge_is_hold_to_run();
    test_tare_only_while_no_water_falls();
    test_safety_has_priority_and_no_restart();
    test_logical_power_off_is_safe();
    test_termination_priority();
    puts("PASS: 8 recorridos de la máquina de estados");
    return 0;
}
