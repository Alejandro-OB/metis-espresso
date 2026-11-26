/*
 * Pruebas del aviso de tara corrida.
 *
 * Lo que no debe pasar: avisar sin tara que vigilar, avisar porque cae café,
 * apagar el aviso solo o dar por buena una báscula sin lectura.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "tare_watch.h"

static bool step(tare_watch_t *w, bool tare, operational_state_t state, float grams)
{
    const tare_watch_input_t input = {
        .tare_taken = tare, .state = state, .mass_available = true, .grams = grams};
    return tare_watch_update(w, &input);
}

/* Sin tara no hay nada que vigilar. */
static void test_nothing_to_watch_without_a_tare(void)
{
    tare_watch_t w = {0};
    assert(!step(&w, false, OP_STATE_READY, 25.0F));
}

/* Dentro de ±1 g, quieta; pasado el límite, aviso que se queda aunque la
 * lectura vuelva a cero, hasta la próxima tara. */
static void test_drift_latches_until_the_next_tare(void)
{
    tare_watch_t w = {0};
    assert(!step(&w, true, OP_STATE_READY, 0.0F));
    assert(!step(&w, false, OP_STATE_READY, 0.9F));
    assert(!step(&w, false, OP_STATE_READY, -1.0F));
    assert(step(&w, false, OP_STATE_READY, -1.4F));
    assert(step(&w, false, OP_STATE_READY, 0.0F));
    assert(!step(&w, true, OP_STATE_READY, 0.0F));
}

/* El café que cae no es una tara corrida, y tras el café no se vigila hasta
 * otra tara. */
static void test_coffee_and_purge_disarm(void)
{
    const operational_state_t pumping[] = {OP_STATE_EXTRACTION, OP_STATE_MANUAL_PURGE};
    for (size_t i = 0U; i < sizeof(pumping) / sizeof(pumping[0]); ++i) {
        tare_watch_t w = {0};
        assert(!step(&w, true, OP_STATE_READY, 0.0F));
        assert(!step(&w, false, pumping[i], 30.0F));
        assert(!step(&w, false, OP_STATE_READY, 38.0F));
    }
}

/* Sin lectura no se puede comprobar: se da por corrida. */
static void test_no_reading_counts_as_drifted(void)
{
    tare_watch_t w = {0};
    assert(!step(&w, true, OP_STATE_READY, 0.0F));
    const tare_watch_input_t lost = {.state = OP_STATE_READY, .mass_available = false};
    assert(tare_watch_update(&w, &lost));
    tare_watch_t v = {0};
    assert(!step(&v, true, OP_STATE_READY, 0.0F));
    assert(step(&v, false, OP_STATE_READY, NAN));
    assert(!tare_watch_update(NULL, &lost));
    assert(!tare_watch_update(&v, NULL));
}

int main(void)
{
    test_nothing_to_watch_without_a_tare();
    test_drift_latches_until_the_next_tare();
    test_coffee_and_purge_disarm();
    test_no_reading_counts_as_drifted();
    printf("PASS: aviso de tara corrida\n");
    return 0;
}
