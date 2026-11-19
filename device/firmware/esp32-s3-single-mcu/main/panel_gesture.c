#include "panel_gesture.h"

#include <stddef.h>

static bool pump_in_use(operational_state_t state)
{
    return state == OP_STATE_EXTRACTION || state == OP_STATE_MANUAL_PURGE;
}

/* Tara y purgado, sin encendido. Cuenta desde que los dos están apretados. */
static panel_gesture_action_t firmware_update_gesture(panel_gesture_t *gesture,
                                                      const panel_gesture_input_t *input)
{
    if (!input->tare_pressed || !input->purge_pressed || input->tare_held ||
        input->purge_held || gesture->update_done ||
        input->state != OP_STATE_SAFE_INACTIVE) {
        return PANEL_GESTURE_NONE;
    }
    const uint32_t both_ms = input->tare_pressed_ms < input->purge_pressed_ms
                                 ? input->tare_pressed_ms
                                 : input->purge_pressed_ms;
    if (both_ms < PANEL_GESTURE_HOLD_MS) {
        return PANEL_GESTURE_NONE;
    }
    gesture->update_done = true;
    return PANEL_GESTURE_FIRMWARE_UPDATE;
}

panel_gesture_action_t panel_gesture_update(panel_gesture_t *gesture,
                                            const panel_gesture_input_t *input)
{
    if (gesture == NULL || input == NULL) {
        return PANEL_GESTURE_NONE;
    }
    if (!input->tare_pressed || !input->purge_pressed) {
        gesture->update_done = false;
    }
    if (!input->power_pressed) {
        gesture->clear_done = false;
        gesture->restart_done = false;
        return firmware_update_gesture(gesture, input);
    }
    if (!input->tare_pressed) {
        gesture->restart_done = false;
    }
    if (input->power_held || (input->tare_pressed && input->tare_held)) {
        return PANEL_GESTURE_NONE;
    }

    if (input->tare_pressed) {
        /* El reinicio cuenta desde que los dos están apretados: lo que lleve
         * cada uno por separado no cuenta. */
        const uint32_t both_ms = input->power_pressed_ms < input->tare_pressed_ms
                                     ? input->power_pressed_ms
                                     : input->tare_pressed_ms;
        if (!gesture->restart_done && both_ms >= PANEL_GESTURE_HOLD_MS &&
            !pump_in_use(input->state)) {
            gesture->restart_done = true;
            return PANEL_GESTURE_RESTART;
        }
        /* Con la tara apretada no hay despeje: el dedo va camino del reinicio. */
        return PANEL_GESTURE_NONE;
    }

    if (!gesture->clear_done && input->state == OP_STATE_ALARM &&
        input->power_pressed_ms >= PANEL_GESTURE_HOLD_MS) {
        gesture->clear_done = true;
        return PANEL_GESTURE_CLEAR_ALARM;
    }
    return PANEL_GESTURE_NONE;
}

const char *panel_gesture_name(panel_gesture_action_t action)
{
    switch (action) {
    case PANEL_GESTURE_CLEAR_ALARM:
        return "despejar alarma";
    case PANEL_GESTURE_RESTART:
        return "reiniciar";
    case PANEL_GESTURE_FIRMWARE_UPDATE:
        return "buscar firmware nuevo";
    case PANEL_GESTURE_NONE:
    default:
        return "ninguno";
    }
}

bool panel_power_on_allowed(operational_state_t state, bool mains_present, bool measuring)
{
    return state != OP_STATE_SAFE_INACTIVE || (mains_present && measuring);
}
