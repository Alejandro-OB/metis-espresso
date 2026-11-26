/*
 * Pruebas de la conversión del agua pedida a referencia de caldera.
 *
 * Lo que no debe pasar: extrapolar fuera de lo medido, dar al lazo una caldera
 * fuera del intervalo por redondeo, cambiar el significado de un acta sin la
 * sección, o aceptar una relación imposible.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "water_reference.h"

/* La recta del acta: agua ≈ 0,92 · caldera + 0,3, caldera medida 88–100. */
static const water_reference_t MEASURED = {
    .defined = true, .gain = 0.92F, .offset_c = 0.3F,
    .boiler_minimum_c = 88.0F, .boiler_maximum_c = 100.0F};

static bool near(float a, float b) { return fabsf(a - b) < 1e-3F; }

/* Sin sección en el acta, la temperatura del perfil es la de la caldera. */
static void test_without_relation_the_profile_is_the_boiler(void)
{
    const water_reference_t none = {0};
    float boiler = 0.0F;
    assert(water_reference_boiler_setpoint(&none, 92.0F, &boiler));
    assert(boiler == 92.0F);
    assert(water_reference_water_for(&none, 92.0F) == 92.0F);
}

/* La conversión invierte la recta: ida y vuelta da lo mismo. */
static void test_round_trip(void)
{
    for (float water = 81.3F; water <= 92.2F; water += 0.1F) {
        float boiler = 0.0F;
        assert(water_reference_boiler_setpoint(&MEASURED, water, &boiler));
        assert(near(water_reference_water_for(&MEASURED, boiler), water));
    }
    float boiler = 0.0F;
    assert(water_reference_boiler_setpoint(&MEASURED, 92.0F, &boiler));
    assert(boiler > 99.6F && boiler < 99.7F);
}

/* Fuera de lo medido no se extrapola, y la salida no se toca. */
static void test_outside_the_measured_range_is_refused(void)
{
    const float refused[] = {81.0F, 92.5F, 95.0F, 60.0F, NAN, INFINITY};
    for (size_t i = 0U; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        float boiler = -1.0F;
        assert(!water_reference_boiler_setpoint(&MEASURED, refused[i], &boiler));
        assert(boiler == -1.0F);
    }
}

/* Los extremos exactos entran, y el lazo nunca recibe una caldera fuera del
 * intervalo, ni por redondeo. */
static void test_edges_are_clamped_not_exceeded(void)
{
    const float edges[] = {
        water_reference_water_for(&MEASURED, 88.0F),
        water_reference_water_for(&MEASURED, 100.0F)};
    for (size_t i = 0U; i < 2U; ++i) {
        float boiler = 0.0F;
        assert(water_reference_boiler_setpoint(&MEASURED, edges[i], &boiler));
        assert(boiler >= 88.0F && boiler <= 100.0F);
    }
}

/* Una relación imposible no convierte nada, aunque se declare definida. */
static void test_an_impossible_relation_is_refused(void)
{
    const water_reference_t broken[] = {
        {.defined = true, .gain = 0.0F, .offset_c = 0.3F,
         .boiler_minimum_c = 88.0F, .boiler_maximum_c = 100.0F},
        {.defined = true, .gain = -0.92F, .offset_c = 0.3F,
         .boiler_minimum_c = 88.0F, .boiler_maximum_c = 100.0F},
        {.defined = true, .gain = 0.92F, .offset_c = NAN,
         .boiler_minimum_c = 88.0F, .boiler_maximum_c = 100.0F},
        {.defined = true, .gain = 0.92F, .offset_c = 0.3F,
         .boiler_minimum_c = 100.0F, .boiler_maximum_c = 88.0F},
    };
    for (size_t i = 0U; i < sizeof(broken) / sizeof(broken[0]); ++i) {
        float boiler = -1.0F;
        assert(!water_reference_is_valid(&broken[i]));
        assert(!water_reference_boiler_setpoint(&broken[i], 85.0F, &boiler));
        assert(boiler == -1.0F);
    }
    assert(water_reference_is_valid(&MEASURED));
}

int main(void)
{
    test_without_relation_the_profile_is_the_boiler();
    test_round_trip();
    test_outside_the_measured_range_is_refused();
    test_edges_are_clamped_not_exceeded();
    test_an_impossible_relation_is_refused();
    puts("water_reference: todas las pruebas pasan");
    return 0;
}
