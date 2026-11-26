/*
 * Pruebas de los gestos del mando (despejar alarma, reiniciar y pedir firmware).
 *
 * Lo que importa es lo que no debe pasar: que un contacto en cortocircuito
 * despeje alarmas o reinicie en bucle, que el reinicio corte un café, que un
 * gesto se repita sin soltar o que el despeje se cuele camino del reinicio.
 */

#include <assert.h>
#include <stdio.h>

#include "panel_gesture.h"

#define STEP_MS 5U

/* Avanza el gesto `ms` con los dos botones como se indica, contando desde que
 * cada uno se apretó, y devuelve la primera acción que salga. */
static panel_gesture_action_t hold(panel_gesture_t *gesture, operational_state_t state,
                                   bool power, bool tare, uint32_t ms)
{
    panel_gesture_action_t first = PANEL_GESTURE_NONE;
    for (uint32_t t = 0U; t <= ms; t += STEP_MS) {
        const panel_gesture_input_t input = {.state = state,
                                             .power_pressed = power,
                                             .power_pressed_ms = power ? t : 0U,
                                             .tare_pressed = tare,
                                             .tare_pressed_ms = tare ? t : 0U};
        const panel_gesture_action_t action = panel_gesture_update(gesture, &input);
        if (action != PANEL_GESTURE_NONE) {
            assert(first == PANEL_GESTURE_NONE); /* una sola vez por pulsación */
            first = action;
        }
    }
    return first;
}

static void release(panel_gesture_t *gesture, operational_state_t state)
{
    const panel_gesture_input_t input = {.state = state};
    assert(panel_gesture_update(gesture, &input) == PANEL_GESTURE_NONE);
}

/* Encendido solo, 3 s, en alarma: pide despejar una vez; antes de 3 s, nada. */
static void test_power_held_in_alarm_asks_to_clear_once(void)
{
    panel_gesture_t g = {0};
    assert(hold(&g, OP_STATE_ALARM, true, false, PANEL_GESTURE_HOLD_MS - STEP_MS) ==
           PANEL_GESTURE_NONE);
    release(&g, OP_STATE_ALARM);
    assert(hold(&g, OP_STATE_ALARM, true, false, 9000U) == PANEL_GESTURE_CLEAR_ALARM);
    release(&g, OP_STATE_ALARM);
    assert(hold(&g, OP_STATE_ALARM, true, false, PANEL_GESTURE_HOLD_MS) ==
           PANEL_GESTURE_CLEAR_ALARM);
}

/* Fuera de alarma, encendido sostenido no pide nada. */
static void test_power_held_outside_alarm_does_nothing(void)
{
    for (int state = 0; state < (int)OP_STATE_ALARM; state++) {
        panel_gesture_t g = {0};
        assert(hold(&g, (operational_state_t)state, true, false, 9000U) == PANEL_GESTURE_NONE);
    }
}

/* Encendido y tara, 3 s: reinicia en reposo, calentando, lista y alarma; nunca
 * con la bomba en uso. Y en alarma no despeja por el camino. */
static void test_power_and_tare_restart_except_with_the_pump(void)
{
    const operational_state_t allowed[] = {OP_STATE_SAFE_INACTIVE, OP_STATE_COFFEE_HEATING,
                                           OP_STATE_READY, OP_STATE_ALARM};
    for (size_t i = 0U; i < sizeof(allowed) / sizeof(allowed[0]); ++i) {
        panel_gesture_t g = {0};
        assert(hold(&g, allowed[i], true, true, 4000U) == PANEL_GESTURE_RESTART);
    }
    const operational_state_t pumping[] = {OP_STATE_EXTRACTION, OP_STATE_MANUAL_PURGE};
    for (size_t i = 0U; i < sizeof(pumping) / sizeof(pumping[0]); ++i) {
        panel_gesture_t g = {0};
        assert(hold(&g, pumping[i], true, true, 9000U) == PANEL_GESTURE_NONE);
    }
}

/* El reinicio cuenta desde que los dos están apretados. */
static void test_restart_counts_from_both_pressed(void)
{
    panel_gesture_t g = {0};
    const panel_gesture_input_t late_tare = {.state = OP_STATE_READY,
                                             .power_pressed = true,
                                             .power_pressed_ms = 8000U,
                                             .tare_pressed = true,
                                             .tare_pressed_ms = 2000U};
    assert(panel_gesture_update(&g, &late_tare) == PANEL_GESTURE_NONE);
}

/* Un pulsador retenido no hace gestos: ni despeja ni reinicia en bucle. */
static void test_a_held_contact_makes_no_gesture(void)
{
    panel_gesture_t g = {0};
    panel_gesture_input_t input = {.state = OP_STATE_ALARM,
                                   .power_pressed = true,
                                   .power_pressed_ms = 5000U,
                                   .power_held = true};
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_NONE);
    input.tare_pressed = true;
    input.tare_pressed_ms = 5000U;
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_NONE);
    input.power_held = false;
    input.tare_held = true;
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_NONE);
}

/* Sin estructura o sin entrada, nada. A cero, rearmado. */
static void test_null_and_zeroed(void)
{
    const panel_gesture_input_t input = {.state = OP_STATE_ALARM,
                                         .power_pressed = true,
                                         .power_pressed_ms = 5000U};
    assert(panel_gesture_update(NULL, &input) == PANEL_GESTURE_NONE);
    panel_gesture_t g = {0};
    assert(panel_gesture_update(&g, NULL) == PANEL_GESTURE_NONE);
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_CLEAR_ALARM);
}


/* Sin red, o con red pero todavía sin medidas del proceso, encendido no saca la
 * máquina del reposo; apagar sí se puede siempre. */
static void test_power_on_needs_mains(void)
{
    assert(!panel_power_on_allowed(OP_STATE_SAFE_INACTIVE, false, true));
    assert(!panel_power_on_allowed(OP_STATE_SAFE_INACTIVE, true, false));
    assert(!panel_power_on_allowed(OP_STATE_SAFE_INACTIVE, false, false));
    assert(panel_power_on_allowed(OP_STATE_SAFE_INACTIVE, true, true));
    for (int state = 1; state <= (int)OP_STATE_ALARM; state++) {
        assert(panel_power_on_allowed((operational_state_t)state, false, false));
    }
}

/* Tara y purgado durante `ms`, contando desde que los dos están apretados. */
static panel_gesture_action_t hold_update(panel_gesture_t *gesture, operational_state_t state,
                                          bool power, uint32_t ms)
{
    panel_gesture_action_t first = PANEL_GESTURE_NONE;
    for (uint32_t t = 0U; t <= ms; t += STEP_MS) {
        const panel_gesture_input_t input = {.state = state,
                                             .power_pressed = power,
                                             .power_pressed_ms = power ? t : 0U,
                                             .tare_pressed = true,
                                             .tare_pressed_ms = t,
                                             .purge_pressed = true,
                                             .purge_pressed_ms = t};
        const panel_gesture_action_t action = panel_gesture_update(gesture, &input);
        if (action != PANEL_GESTURE_NONE) {
            assert(first == PANEL_GESTURE_NONE);
            first = action;
        }
    }
    return first;
}

/* Con la máquina apagada, tara y purgado 3 s piden firmware una sola vez. */
static void test_tare_and_purge_ask_for_firmware_when_off(void)
{
    panel_gesture_t g = {0};
    assert(hold_update(&g, OP_STATE_SAFE_INACTIVE, false, PANEL_GESTURE_HOLD_MS - STEP_MS) ==
           PANEL_GESTURE_NONE);
    release(&g, OP_STATE_SAFE_INACTIVE);
    assert(hold_update(&g, OP_STATE_SAFE_INACTIVE, false, 9000U) ==
           PANEL_GESTURE_FIRMWARE_UPDATE);
    release(&g, OP_STATE_SAFE_INACTIVE);
    assert(hold_update(&g, OP_STATE_SAFE_INACTIVE, false, PANEL_GESTURE_HOLD_MS) ==
           PANEL_GESTURE_FIRMWARE_UPDATE);
}

/* Encendida, en cualquier estado, el gesto no pide nada: en READY el purgado
 * ya abrió el agua al primer dedo. */
static void test_firmware_gesture_only_when_off(void)
{
    for (int state = 1; state <= (int)OP_STATE_ALARM; state++) {
        panel_gesture_t g = {0};
        assert(hold_update(&g, (operational_state_t)state, false, 9000U) ==
               PANEL_GESTURE_NONE);
    }
}

/* Con el encendido también apretado no hay petición de firmware. */
static void test_power_cancels_the_firmware_gesture(void)
{
    panel_gesture_t g = {0};
    assert(hold_update(&g, OP_STATE_SAFE_INACTIVE, true, 9000U) != PANEL_GESTURE_FIRMWARE_UPDATE);
}

/* Cuenta desde que los dos están apretados, y un contacto retenido no cuenta. */
static void test_firmware_gesture_counts_from_both_and_ignores_held(void)
{
    panel_gesture_t g = {0};
    panel_gesture_input_t input = {.state = OP_STATE_SAFE_INACTIVE,
                                   .tare_pressed = true,
                                   .tare_pressed_ms = 8000U,
                                   .purge_pressed = true,
                                   .purge_pressed_ms = PANEL_GESTURE_HOLD_MS - STEP_MS};
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_NONE);
    input.purge_pressed_ms = PANEL_GESTURE_HOLD_MS;
    input.purge_held = true;
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_NONE);
    input.purge_held = false;
    input.tare_held = true;
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_NONE);
    input.tare_held = false;
    assert(panel_gesture_update(&g, &input) == PANEL_GESTURE_FIRMWARE_UPDATE);
}

int main(void)
{
    test_power_held_in_alarm_asks_to_clear_once();
    test_power_held_outside_alarm_does_nothing();
    test_power_and_tare_restart_except_with_the_pump();
    test_restart_counts_from_both_pressed();
    test_a_held_contact_makes_no_gesture();
    test_null_and_zeroed();
    test_power_on_needs_mains();
    test_tare_and_purge_ask_for_firmware_when_off();
    test_firmware_gesture_only_when_off();
    test_power_cancels_the_firmware_gesture();
    test_firmware_gesture_counts_from_both_and_ignores_held();
    printf("PASS: gestos del mando\n");
    return 0;
}
