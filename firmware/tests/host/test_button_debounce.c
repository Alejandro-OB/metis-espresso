/*
 * Pruebas del antirrebote de los pulsadores.
 *
 * La propiedad que justifica que este módulo exista es que **un rebote no
 * produzca una orden**. Sobre una máquina cuyo botón de café inicia una
 * extracción, una ráfaga de pulsaciones fantasma no es una molestia estética.
 *
 * Y hay una segunda, menos evidente y más importante: **un botón que ya está
 * pulsado cuando arranca la máquina no puede producir nada**. Si esa primera
 * lectura se tratara como una pulsación, un contacto encallado o una pata
 * flotante iniciarían una preparación que nadie pidió, en el instante del
 * encendido y sin que nadie tocara la máquina.
 */

#include <assert.h>
#include <stdio.h>

#include "button_debounce.h"

#define REQUIRED 4U
#define PERIOD_MS 5U

static button_debounce_t make(void)
{
    button_debounce_t button;
    assert(button_debounce_configure(&button, REQUIRED, PERIOD_MS));
    return button;
}

/* Deja el botón anclado en «suelto», como tras un arranque normal. */
static void anchor_released(button_debounce_t *button)
{
    assert(button_debounce_update(button, false) == BUTTON_EDGE_NONE);
}

static void test_the_configuration_refuses_no_debounce_at_all(void)
{
    button_debounce_t button;
    assert(button_debounce_configure(&button, REQUIRED, PERIOD_MS));
    /* Aceptar un cambio con una sola muestra es no tener antirrebote. */
    assert(!button_debounce_configure(&button, 1U, PERIOD_MS));
    assert(!button_debounce_configure(&button, 0U, PERIOD_MS));
    assert(!button_debounce_configure(&button, REQUIRED, 0U));
    assert(!button_debounce_configure(NULL, REQUIRED, PERIOD_MS));
}

/*
 * Un botón pulsado en el arranque no produce ninguna orden.
 *
 * Ni entonces ni mientras siga pulsado. Hace falta soltarlo y volver a pulsarlo.
 */
static void test_a_button_held_at_startup_produces_nothing(void)
{
    button_debounce_t button = make();

    /* Arranca pulsado y sigue pulsado veinte muestras. */
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    for (uint32_t index = 0U; index < 20U; index++) {
        assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    }
    assert(button_debounce_pressed(&button));

    /* Se suelta: eso sí es un cambio, y produce el flanco de soltar. */
    button_edge_t edge = BUTTON_EDGE_NONE;
    for (uint32_t index = 0U; index < REQUIRED; index++) {
        edge = button_debounce_update(&button, false);
    }
    assert(edge == BUTTON_EDGE_RELEASED);

    /* Y ahora una pulsación de verdad sí se acepta. */
    for (uint32_t index = 0U; index < REQUIRED; index++) {
        edge = button_debounce_update(&button, true);
    }
    assert(edge == BUTTON_EDGE_PRESSED);
}

/* Una pulsación limpia produce un flanco, y solo uno. */
static void test_a_clean_press_produces_exactly_one_edge(void)
{
    button_debounce_t button = make();
    anchor_released(&button);

    uint32_t pulsaciones = 0U;
    for (uint32_t index = 0U; index < 50U; index++) {
        if (button_debounce_update(&button, true) == BUTTON_EDGE_PRESSED) {
            pulsaciones++;
        }
    }
    assert(pulsaciones == 1U);
    assert(button_debounce_pressed(&button));

    uint32_t sueltas = 0U;
    for (uint32_t index = 0U; index < 50U; index++) {
        if (button_debounce_update(&button, false) == BUTTON_EDGE_RELEASED) {
            sueltas++;
        }
    }
    assert(sueltas == 1U);
    assert(!button_debounce_pressed(&button));
}

/*
 * El rebote no produce ninguna orden.
 *
 * Se simula un contacto que alterna durante varias muestras antes de asentarse,
 * que es lo que hace un pulsador real.
 */
static void test_bouncing_produces_no_order(void)
{
    button_debounce_t button = make();
    anchor_released(&button);

    /* Doce muestras alternando: ninguna secuencia llega a REQUIRED seguidas. */
    uint32_t flancos = 0U;
    for (uint32_t index = 0U; index < 12U; index++) {
        if (button_debounce_update(&button, (index % 2U) == 0U) != BUTTON_EDGE_NONE) {
            flancos++;
        }
    }
    assert(flancos == 0U);
    assert(!button_debounce_pressed(&button));

    /* Cuando por fin se asienta, entonces sí. */
    button_edge_t edge = BUTTON_EDGE_NONE;
    for (uint32_t index = 0U; index < REQUIRED; index++) {
        edge = button_debounce_update(&button, true);
    }
    assert(edge == BUTTON_EDGE_PRESSED);
}

/*
 * Un rebote a mitad de confirmación reinicia la cuenta.
 *
 * Es la diferencia entre un antirrebote y un simple retardo: no basta con
 * esperar, hay que exigir que las muestras estén de acuerdo.
 */
static void test_a_bounce_mid_confirmation_restarts_the_count(void)
{
    button_debounce_t button = make();
    anchor_released(&button);

    /* Tres muestras pulsado, una suelta, y otra vez tres pulsado. */
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    assert(button_debounce_update(&button, false) == BUTTON_EDGE_NONE);
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    /* La cuarta de la segunda tanda es la que lo acepta. */
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_PRESSED);
}

/*
 * El tiempo pulsado se mide, que es lo que hace utilizable el purgado.
 *
 * El purgado es de mantener: se abre al pulsar y se cierra al soltar, y entre
 * medias alguien necesita saber cuánto lleva.
 */
static void test_the_held_time_is_measured(void)
{
    button_debounce_t button = make();
    anchor_released(&button);

    for (uint32_t index = 0U; index < REQUIRED; index++) {
        (void)button_debounce_update(&button, true);
    }
    assert(button_debounce_pressed(&button));
    assert(button_debounce_pressed_ms(&button) == 0U);

    /* Cien muestras más de medio segundo. */
    for (uint32_t index = 0U; index < 100U; index++) {
        (void)button_debounce_update(&button, true);
    }
    assert(button_debounce_pressed_ms(&button) == 100U * PERIOD_MS);

    /* Al soltar vuelve a cero. */
    for (uint32_t index = 0U; index < REQUIRED; index++) {
        (void)button_debounce_update(&button, false);
    }
    assert(!button_debounce_pressed(&button));
    assert(button_debounce_pressed_ms(&button) == 0U);
}

/* Reiniciar olvida también la referencia, y vuelve a exigir el primer anclaje. */
static void test_the_reset_forgets_the_anchor(void)
{
    button_debounce_t button = make();
    anchor_released(&button);
    for (uint32_t index = 0U; index < REQUIRED; index++) {
        (void)button_debounce_update(&button, true);
    }
    assert(button_debounce_pressed(&button));

    button_debounce_reset(&button);
    assert(!button_debounce_pressed(&button));
    assert(button_debounce_pressed_ms(&button) == 0U);

    /* Tras el reinicio, un botón pulsado vuelve a no producir nada. */
    assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    for (uint32_t index = 0U; index < 20U; index++) {
        assert(button_debounce_update(&button, true) == BUTTON_EDGE_NONE);
    }
}

static void test_it_survives_a_null_or_unconfigured_button(void)
{
    assert(button_debounce_update(NULL, true) == BUTTON_EDGE_NONE);
    button_debounce_reset(NULL);
    assert(!button_debounce_pressed(NULL));
    assert(button_debounce_pressed_ms(NULL) == 0U);

    /* Una estructura a ceros no produce flancos: sin antirrebote configurado,
     * aceptar pulsaciones sería peor que no aceptarlas. */
    button_debounce_t sin_configurar = {0};
    for (uint32_t index = 0U; index < 20U; index++) {
        assert(button_debounce_update(&sin_configurar, true) == BUTTON_EDGE_NONE);
    }
    assert(!button_debounce_pressed(&sin_configurar));
}

int main(void)
{
    test_the_configuration_refuses_no_debounce_at_all();
    test_a_button_held_at_startup_produces_nothing();
    test_a_clean_press_produces_exactly_one_edge();
    test_bouncing_produces_no_order();
    test_a_bounce_mid_confirmation_restarts_the_count();
    test_the_held_time_is_measured();
    test_the_reset_forgets_the_anchor();
    test_it_survives_a_null_or_unconfigured_button();
    printf("test_button_debounce: 8 pruebas correctas\n");
    return 0;
}
