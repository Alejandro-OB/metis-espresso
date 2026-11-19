#include "button_debounce.h"

#include <stddef.h>
#include <string.h>

bool button_debounce_configure(button_debounce_t *button,
                               uint32_t required_samples,
                               uint32_t sample_period_ms)
{
    if (button == NULL || required_samples < 2U || sample_period_ms == 0U) {
        return false;
    }
    memset(button, 0, sizeof(*button));
    button->required_samples = required_samples;
    button->sample_period_ms = sample_period_ms;
    return true;
}

void button_debounce_reset(button_debounce_t *button)
{
    if (button == NULL) {
        return;
    }
    const uint32_t required = button->required_samples;
    const uint32_t period = button->sample_period_ms;
    memset(button, 0, sizeof(*button));
    button->required_samples = required;
    button->sample_period_ms = period;
}

button_edge_t button_debounce_update(button_debounce_t *button, bool pressed_now)
{
    if (button == NULL || button->required_samples < 2U) {
        return BUTTON_EDGE_NONE;
    }

    if (!button->anchored) {
        /*
         * La primera muestra fija la referencia y **no produce flanco**. Un botón
         * pulsado, encallado o con la pata flotante en el arranque no debe
         * iniciar nada: hará falta soltarlo y volver a pulsarlo, que es una
         * acción física inequívoca.
         */
        button->anchored = true;
        button->pressed = pressed_now;
        button->candidate = pressed_now;
        button->agreeing = button->required_samples;
        button->pressed_ms = 0U;
        return BUTTON_EDGE_NONE;
    }

    if (button->pressed) {
        /* El tiempo pulsado avanza con cada muestra, no con el flanco: es lo
         * que hace utilizable el purgado, que es de mantener. */
        button->pressed_ms += button->sample_period_ms;
    }

    if (pressed_now != button->candidate) {
        /* Cambió respecto a lo que se estaba confirmando: vuelve a empezar. Es
         * lo que descarta el rebote, que por definición no se sostiene. */
        button->candidate = pressed_now;
        button->agreeing = 1U;
        return BUTTON_EDGE_NONE;
    }

    if (button->agreeing < button->required_samples) {
        button->agreeing++;
    }
    if (button->agreeing < button->required_samples || button->candidate == button->pressed) {
        return BUTTON_EDGE_NONE;
    }

    button->pressed = button->candidate;
    if (button->pressed) {
        button->pressed_ms = 0U;
        return BUTTON_EDGE_PRESSED;
    }
    button->pressed_ms = 0U;
    return BUTTON_EDGE_RELEASED;
}

bool button_debounce_pressed(const button_debounce_t *button)
{
    return button == NULL ? false : button->pressed;
}

uint32_t button_debounce_pressed_ms(const button_debounce_t *button)
{
    return button == NULL ? 0U : button->pressed_ms;
}
