#include "indicator_panel.h"

#include <stddef.h>

const char *indicator_pattern_name(indicator_pattern_t pattern)
{
    switch (pattern) {
    case INDICATOR_ON:
        return "fijo";
    case INDICATOR_SLOW:
        return "lento";
    case INDICATOR_FAST:
        return "rapido";
    case INDICATOR_OFF:
    default:
        return "apagado";
    }
}

bool indicator_pattern_is_lit(indicator_pattern_t pattern, uint64_t elapsed_ms)
{
    switch (pattern) {
    case INDICATOR_ON:
        return true;
    case INDICATOR_SLOW:
        return (elapsed_ms % INDICATOR_SLOW_PERIOD_MS) < (INDICATOR_SLOW_PERIOD_MS / 2U);
    case INDICATOR_FAST:
        return (elapsed_ms % INDICATOR_FAST_PERIOD_MS) < (INDICATOR_FAST_PERIOD_MS / 2U);
    case INDICATOR_OFF:
    default:
        return false;
    }
}

/*
 * Lo que dice el estado operativo, antes de superponerle las averías: un solo
 * color por estado (tabla de la cabecera).
 */
static indicator_panel_t from_state(operational_state_t state)
{
    indicator_panel_t panel = {0};
    switch (state) {
    case OP_STATE_SAFE_INACTIVE:
        /* Apagada: el panel no debe parecer encendido con la máquina enchufada
         * y en reposo. */
        break;
    case OP_STATE_COFFEE_HEATING:
        panel.yellow = INDICATOR_SLOW;
        break;
    case OP_STATE_READY:
        panel.green = INDICATOR_ON;
        break;
    case OP_STATE_EXTRACTION:
    case OP_STATE_MANUAL_PURGE:
        panel.blue = INDICATOR_ON;
        break;
    case OP_STATE_ALARM:
        panel.red = INDICATOR_ON;
        break;
    default:
        /*
         * Un estado que no está en la enumeración solo puede venir de memoria
         * corrupta o de un enumerado ampliado sin tocar esta tabla. No se puede
         * afirmar nada sobre la máquina, así que no se enciende ningún LED que
         * signifique «lista» y sí el aviso de que algo no cuadra.
         */
        panel.red = INDICATOR_SLOW;
        break;
    }
    return panel;
}

indicator_panel_t indicator_panel_evaluate(const indicator_request_t *request)
{
    if (request == NULL) {
        return (indicator_panel_t){.red = INDICATOR_SLOW};
    }

    indicator_panel_t panel = from_state(request->state);
    /* Sustituye al estado salvo a una alarma declarada, que dice más. */
    if (request->firmware_updating && panel.red != INDICATOR_ON) {
        panel = (indicator_panel_t){.blue = INDICATOR_SLOW, .yellow = INDICATOR_SLOW};
    }

    /*
     * El aviso de avería se superpone al estado.
     *
     * El sistema debe informar de una condición insegura, y esa condición puede
     * darse sin que la máquina de estados haya disparado: un sensor que se
     * suelta con la cafetera en `READY` corta las salidas por la ruta del
     * guardián y deja el estado intacto. Sin esta superposición, el barista
     * vería una máquina lista que en realidad no puede hacer nada.
     *
     * No pisa una alarma ya declarada: fijo es una parada y parpadeante es un
     * aviso, y degradar lo primero a lo segundo diría menos de lo que se sabe.
     */
    const bool actuation_fault = !request->mains_absent &&
                                 (actuation_verdict_is_fault(request->pump) ||
                                  actuation_verdict_is_fault(request->heater));
    if (panel.red == INDICATOR_OFF && (actuation_fault || request->panel_held)) {
        panel.red = INDICATOR_SLOW;
    }

    if (request->tare_drifted && panel.blue == INDICATOR_OFF && !request->firmware_updating) {
        const uint32_t phase = request->tare_drift_age_ms % INDICATOR_TARE_REMINDER_PERIOD_MS;
        if (phase < INDICATOR_TARE_FLASH_MS &&
            (phase % INDICATOR_TARE_FLASH_PERIOD_MS) < (INDICATOR_TARE_FLASH_PERIOD_MS / 2U)) {
            panel.blue = INDICATOR_ON;
        }
    }

    /*
     * El destello de la tara va al final: es lo más reciente que hizo el barista
     * y lo que espera ver. Se resuelve aquí como encendido o apagado, no como
     * patrón, porque su fase es la de la pulsación y no la del reloj.
     */
    if (request->tare != INDICATOR_TARE_NONE && request->tare_age_ms < INDICATOR_TARE_FLASH_MS) {
        const indicator_pattern_t flash =
            (request->tare_age_ms % INDICATOR_TARE_FLASH_PERIOD_MS) <
                    (INDICATOR_TARE_FLASH_PERIOD_MS / 2U)
                ? INDICATOR_ON
                : INDICATOR_OFF;
        if (request->tare == INDICATOR_TARE_TAKEN) {
            panel.green = flash;
        } else if (panel.red != INDICATOR_ON) {
            panel.red = flash;
        }
    }
    return panel;
}
