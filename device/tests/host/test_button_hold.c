/*
 * Pruebas de la detección de un pulsador retenido (ADR-006 § 7.9).
 *
 * Lo que se comprueba son las propiedades que justifican el módulo: que un
 * botón apretado desde el arranque se declare retenido sin esperar, que una
 * pulsación normal no lo sea nunca, que el aviso salga una sola vez por
 * episodio y que soltar el botón lo devuelva a utilizable. Y que el purgado,
 * que se usa manteniéndolo, no se convierta en avería por durar.
 */

#include <assert.h>
#include <stdio.h>

#include "button_hold.h"

#define LIMIT_MS 10000U
#define PERIOD_MS 5U

/* Simula `ms` de pulsación continua a partir de `start_ms`, y cuenta cuántas
 * veces el módulo anunció el paso a retenido. */
static uint32_t hold_for(button_hold_t *hold, uint32_t start_ms, uint32_t ms)
{
    uint32_t announcements = 0U;
    for (uint32_t elapsed = start_ms; elapsed <= start_ms + ms; elapsed += PERIOD_MS) {
        if (button_hold_update(hold, true, elapsed)) {
            announcements++;
        }
    }
    return announcements;
}

static void test_pressed_at_start_is_held_at_once(void)
{
    button_hold_t hold;
    button_hold_configure(&hold, LIMIT_MS);
    /* Primera lectura, pulsado y sin tiempo acumulado: retenido ya. */
    assert(button_hold_update(&hold, true, 0U));
    assert(button_hold_is_held(&hold));
}

static void test_the_start_rule_applies_without_a_time_limit(void)
{
    /* El purgado no tiene límite de tiempo, pero apretado al arrancar sigue
     * siendo un contacto encallado. */
    button_hold_t hold;
    button_hold_configure(&hold, 0U);
    assert(button_hold_update(&hold, true, 0U));
    assert(button_hold_is_held(&hold));
}

static void test_a_normal_press_never_is_held(void)
{
    button_hold_t hold;
    button_hold_configure(&hold, LIMIT_MS);
    assert(!button_hold_update(&hold, false, 0U));
    assert(hold_for(&hold, 0U, 300U) == 0U);
    assert(!button_hold_is_held(&hold));
    assert(!button_hold_update(&hold, false, 0U));
    assert(button_hold_occurrences(&hold) == 0U);
}

static void test_beyond_the_limit_it_is_held_and_announced_once(void)
{
    button_hold_t hold;
    button_hold_configure(&hold, LIMIT_MS);
    assert(!button_hold_update(&hold, false, 0U));
    /* Mucho más allá del límite: un solo aviso, no uno por ciclo. */
    assert(hold_for(&hold, 0U, 3U * LIMIT_MS) == 1U);
    assert(button_hold_is_held(&hold));
    assert(button_hold_occurrences(&hold) == 1U);
}

static void test_just_under_the_limit_is_not_held(void)
{
    button_hold_t hold;
    button_hold_configure(&hold, LIMIT_MS);
    assert(!button_hold_update(&hold, false, 0U));
    assert(hold_for(&hold, 0U, LIMIT_MS - PERIOD_MS) == 0U);
    assert(!button_hold_is_held(&hold));
}

static void test_releasing_makes_it_usable_again(void)
{
    button_hold_t hold;
    button_hold_configure(&hold, LIMIT_MS);
    assert(button_hold_update(&hold, true, 0U));
    assert(!button_hold_update(&hold, false, 0U));
    assert(!button_hold_is_held(&hold));
    /* Una pulsación corta después ya no es retenida: el arranque quedó atrás. */
    assert(hold_for(&hold, 0U, 300U) == 0U);
    assert(!button_hold_is_held(&hold));
    /* Y un segundo episodio se cuenta aparte. */
    assert(!button_hold_update(&hold, false, 0U));
    assert(hold_for(&hold, 0U, 2U * LIMIT_MS) == 1U);
    assert(button_hold_occurrences(&hold) == 2U);
}

static void test_a_held_purge_is_not_a_fault(void)
{
    button_hold_t hold;
    button_hold_configure(&hold, 0U);
    assert(!button_hold_update(&hold, false, 0U));
    assert(hold_for(&hold, 0U, 10U * LIMIT_MS) == 0U);
    assert(!button_hold_is_held(&hold));
}

static void test_a_zeroed_monitor_claims_nothing(void)
{
    /* El cero no afirma un botón retenido. */
    const button_hold_t hold = {0};
    assert(!button_hold_is_held(&hold));
    assert(button_hold_occurrences(&hold) == 0U);
    assert(!button_hold_is_held(NULL));
    assert(!button_hold_update(NULL, true, LIMIT_MS));
}

int main(void)
{
    test_pressed_at_start_is_held_at_once();
    test_the_start_rule_applies_without_a_time_limit();
    test_a_normal_press_never_is_held();
    test_beyond_the_limit_it_is_held_and_announced_once();
    test_just_under_the_limit_is_not_held();
    test_releasing_makes_it_usable_again();
    test_a_held_purge_is_not_a_fault();
    test_a_zeroed_monitor_claims_nothing();
    puts("button_hold: todas las pruebas pasaron");
    return 0;
}
