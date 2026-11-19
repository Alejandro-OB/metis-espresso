/*
 * Retención de la integral térmica tras el café (ADR-009, enmienda del
 * 2026-10-16). Se comprueba cuándo retiene y cuándo suelta, no con qué valores.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "heater_recovery_hold.h"

/* Una estructura puesta a cero no retiene: es el comportamiento de antes. */
static void test_a_zeroed_hold_does_not_hold(void)
{
    heater_recovery_hold_t hold;
    memset(&hold, 0, sizeof(hold));
    assert(!heater_recovery_hold_step(&hold, false, 92.0F, 80.0F));
}

/* Retiene desde que la bomba queda autorizada y mientras siga. */
static void test_it_holds_while_the_pump_is_allowed(void)
{
    heater_recovery_hold_t hold;
    heater_recovery_hold_reset(&hold);
    assert(heater_recovery_hold_step(&hold, true, 92.0F, 92.2F));
    assert(heater_recovery_hold_step(&hold, true, 92.0F, 85.0F));
    /* Aunque la medida pase la referencia con la bomba en marcha, sigue: la
     * perturbación no ha terminado. */
    assert(heater_recovery_hold_step(&hold, true, 92.0F, 93.0F));
}

/* Tras parar la bomba sigue reteniendo hasta que la medida vuelve, y suelta
 * al cruzar la referencia, sin margen. */
static void test_it_releases_when_the_measurement_reaches_the_setpoint(void)
{
    heater_recovery_hold_t hold;
    heater_recovery_hold_reset(&hold);
    (void)heater_recovery_hold_step(&hold, true, 92.0F, 84.0F);
    assert(heater_recovery_hold_step(&hold, false, 92.0F, 84.0F));
    assert(heater_recovery_hold_step(&hold, false, 92.0F, 91.99F));
    assert(!heater_recovery_hold_step(&hold, false, 92.0F, 92.0F));
    /* Una vez suelta, una caída sin bomba no vuelve a retener: no es café. */
    assert(!heater_recovery_hold_step(&hold, false, 92.0F, 90.0F));
}

/* Un dato imposible no suelta la retención. */
static void test_a_non_finite_input_does_not_release(void)
{
    heater_recovery_hold_t hold;
    heater_recovery_hold_reset(&hold);
    (void)heater_recovery_hold_step(&hold, true, 92.0F, 84.0F);
    assert(heater_recovery_hold_step(&hold, false, 92.0F, NAN));
    assert(heater_recovery_hold_step(&hold, false, NAN, 95.0F));
    heater_recovery_hold_reset(&hold);
    assert(!heater_recovery_hold_step(&hold, false, 92.0F, 80.0F));
    assert(!heater_recovery_hold_step(NULL, true, 92.0F, 80.0F));
}

int main(void)
{
    test_a_zeroed_hold_does_not_hold();
    test_it_holds_while_the_pump_is_allowed();
    test_it_releases_when_the_measurement_reaches_the_setpoint();
    test_a_non_finite_input_does_not_release();
    printf("heater_recovery_hold: 4 casos correctos\n");
    return 0;
}
