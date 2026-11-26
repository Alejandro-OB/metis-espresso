/*
 * Devolución de la integral térmica tras el café (ADR-009, segunda enmienda
 * del 2026-10-16). Se comprueban las reglas: cuándo guarda I0, cuándo
 * devuelve, cuánto dura y qué pasa con un café encima de otro.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "heater_recovery_return.h"

static heater_recovery_return_t configured(float tau)
{
    heater_recovery_return_t r;
    memset(&r, 0, sizeof(r));
    assert(heater_recovery_return_configure(&r, tau));
    return r;
}

/* Sin τ (estructura a cero o τ = 0) no actúa nunca. */
static void test_without_tau_it_never_acts(void)
{
    heater_recovery_return_t r;
    memset(&r, 0, sizeof(r));
    assert(!heater_recovery_return_step(&r, true, 2.7F, 0.2F).apply);
    assert(!heater_recovery_return_step(&r, false, 4.0F, 0.2F).apply);
    r = configured(0.0F);
    assert(!heater_recovery_return_step(&r, true, 2.7F, 0.2F).apply);
    assert(!heater_recovery_return_step(&r, false, 4.0F, 0.2F).apply);
    heater_recovery_return_t bad;
    assert(!heater_recovery_return_configure(&bad, -1.0F));
    assert(!heater_recovery_return_configure(&bad, NAN));
    assert(!heater_recovery_return_step(&bad, false, 4.0F, 0.2F).apply);
}

/* Con la bomba no actúa; al pararla devuelve hacia el valor de su arranque. */
static void test_it_returns_to_the_integral_at_pump_start(void)
{
    heater_recovery_return_t r = configured(500.0F);
    assert(!heater_recovery_return_step(&r, false, 2.7F, 0.2F).apply);
    assert(!heater_recovery_return_step(&r, true, 2.7F, 0.2F).apply);
    assert(!heater_recovery_return_step(&r, true, 3.0F, 0.2F).apply);
    const heater_recovery_return_decision_t d = heater_recovery_return_step(&r, false, 4.2F, 0.2F);
    assert(d.apply);
    assert(d.target == 2.7F);
    assert(fabsf(d.fraction - 0.2F / 500.0F) < 1e-7F);
}

/* La ventana dura tres τ desde la parada y después no actúa. */
static void test_the_window_lasts_three_taus(void)
{
    heater_recovery_return_t r = configured(10.0F);
    (void)heater_recovery_return_step(&r, true, 2.7F, 1.0F);
    int applied = 0;
    for (int i = 0; i < 40; i++) {
        if (heater_recovery_return_step(&r, false, 3.0F, 1.0F).apply) {
            applied++;
        }
    }
    assert(applied == 30);
    assert(!heater_recovery_return_step(&r, false, 3.0F, 1.0F).apply);
}

/* Un café con la devolución en curso no renueva I0; uno después sí. */
static void test_a_second_coffee_keeps_the_first_holding_value(void)
{
    heater_recovery_return_t r = configured(10.0F);
    (void)heater_recovery_return_step(&r, true, 2.7F, 1.0F);
    (void)heater_recovery_return_step(&r, false, 4.0F, 1.0F);
    (void)heater_recovery_return_step(&r, true, 3.6F, 1.0F);
    heater_recovery_return_decision_t d = heater_recovery_return_step(&r, false, 4.4F, 1.0F);
    assert(d.apply && d.target == 2.7F);
    for (int i = 0; i < 40; i++) {
        (void)heater_recovery_return_step(&r, false, 2.8F, 1.0F);
    }
    (void)heater_recovery_return_step(&r, true, 2.9F, 1.0F);
    d = heater_recovery_return_step(&r, false, 4.0F, 1.0F);
    assert(d.apply && d.target == 2.9F);
}

/* Al reiniciar se olvida todo. */
static void test_reset_forgets_the_cycle(void)
{
    heater_recovery_return_t r = configured(10.0F);
    (void)heater_recovery_return_step(&r, true, 2.7F, 1.0F);
    heater_recovery_return_reset(&r);
    assert(!heater_recovery_return_step(&r, false, 4.0F, 1.0F).apply);
}

int main(void)
{
    test_without_tau_it_never_acts();
    test_it_returns_to_the_integral_at_pump_start();
    test_the_window_lasts_three_taus();
    test_a_second_coffee_keeps_the_first_holding_value();
    test_reset_forgets_the_cycle();
    printf("heater_recovery_return: 5 casos correctos\n");
    return 0;
}
