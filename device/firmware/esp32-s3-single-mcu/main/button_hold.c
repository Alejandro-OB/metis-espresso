#include "button_hold.h"

#include <stddef.h>
#include <string.h>

void button_hold_configure(button_hold_t *hold, uint32_t limit_ms)
{
    if (hold == NULL) {
        return;
    }
    memset(hold, 0, sizeof(*hold));
    hold->limit_ms = limit_ms;
}

bool button_hold_update(button_hold_t *hold, bool pressed, uint32_t pressed_ms)
{
    if (hold == NULL) {
        return false;
    }

    const bool was_held = hold->held;
    if (!pressed) {
        hold->held = false;
    } else if (!hold->started) {
        /* Pulsado en la primera lectura: nadie aprieta un botón en el instante
         * del encendido, así que no hace falta esperar a ningún plazo. */
        hold->held = true;
    } else if (hold->limit_ms > 0U && pressed_ms >= hold->limit_ms) {
        hold->held = true;
    }
    hold->started = true;

    const bool became_held = hold->held && !was_held;
    if (became_held) {
        hold->occurrences++;
    }
    return became_held;
}

bool button_hold_is_held(const button_hold_t *hold)
{
    return hold == NULL ? false : hold->held;
}

uint32_t button_hold_occurrences(const button_hold_t *hold)
{
    return hold == NULL ? 0U : hold->occurrences;
}
