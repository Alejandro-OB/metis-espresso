/*
 * Pruebas del modelo de planta de banco.
 *
 * Este módulo existe para que el PI tenga contra qué regular. Lo que se
 * comprueba, por tanto, no son valores bonitos sino las tres propiedades sin
 * las cuales un lazo cerrado contra él no significaría nada: que la magnitud
 * acaba donde la actuación dice, que tarda lo que la constante de tiempo dice, y
 * que **no se mueve durante el retardo**. Esta última es la que hace que la
 * prueba sea exigente: sin retardo, casi cualquier sintonía funciona.
 *
 * Se comprueba además que las cifras de banco reproducen los números que las
 * decisiones ya tienen escritos —1,4 K/s de ascenso con la caldera llena— para
 * que el modelo no se aleje en silencio de la física que dice representar.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "control_plant.h"

static bool close_enough(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static void test_the_configuration_refuses_an_impossible_plant(void)
{
    control_plant_t plant = {0};
    control_plant_parameters_t parameters = control_plant_pressure_bench();
    assert(control_plant_configure(&plant, &parameters));

    control_plant_parameters_t sin_constante = parameters;
    sin_constante.time_constant_s = 0.0F;
    assert(!control_plant_configure(&plant, &sin_constante));

    control_plant_parameters_t sin_periodo = parameters;
    sin_periodo.sample_period_s = 0.0F;
    assert(!control_plant_configure(&plant, &sin_periodo));

    control_plant_parameters_t retardo_negativo = parameters;
    retardo_negativo.dead_time_s = -0.1F;
    assert(!control_plant_configure(&plant, &retardo_negativo));

    control_plant_parameters_t no_finito = parameters;
    no_finito.gain = NAN;
    assert(!control_plant_configure(&plant, &no_finito));

    /*
     * Un retardo que no cabe se rechaza, no se recorta. Una planta con la mitad
     * del retardo pedido es otra planta, y la sintonía que saliera de ella no
     * valdría para la que se quiso modelar.
     */
    control_plant_parameters_t retardo_enorme = parameters;
    retardo_enorme.dead_time_s = 10.0F;  /* 500 periodos de 20 ms */
    assert(!control_plant_configure(&plant, &retardo_enorme));

    assert(!control_plant_configure(NULL, &parameters));
    assert(!control_plant_configure(&plant, NULL));
}

/* Con actuación constante, la magnitud acaba donde la ganancia dice. */
static void test_a_constant_actuation_settles_where_the_gain_says(void)
{
    control_plant_t plant = {0};
    const control_plant_parameters_t parameters = control_plant_pressure_bench();
    assert(control_plant_configure(&plant, &parameters));
    control_plant_reset(&plant, 0.0F);

    /* Veinte constantes de tiempo: de sobra para asentarse. */
    const uint32_t steps = (uint32_t)(20.0F * parameters.time_constant_s /
                                      parameters.sample_period_s);
    for (uint32_t step = 0U; step < steps; step++) {
        (void)control_plant_step(&plant, 100.0F);
    }
    assert(close_enough(control_plant_value(&plant),
                        control_plant_steady_state(&plant, 100.0F), 0.01F));
    assert(close_enough(control_plant_value(&plant), 9.0F, 0.01F));

    /* Y con la mitad de actuación, la mitad de presión: la planta es lineal. */
    for (uint32_t step = 0U; step < steps; step++) {
        (void)control_plant_step(&plant, 50.0F);
    }
    assert(close_enough(control_plant_value(&plant), 4.5F, 0.01F));
}

/* Sin actuación vuelve al reposo: el actuador solo aporta energía. */
static void test_without_actuation_it_returns_to_rest(void)
{
    control_plant_t plant = {0};
    const control_plant_parameters_t parameters = control_plant_temperature_bench();
    assert(control_plant_configure(&plant, &parameters));
    control_plant_reset(&plant, 95.0F);

    const uint32_t steps = (uint32_t)(20.0F * parameters.time_constant_s /
                                      parameters.sample_period_s);
    for (uint32_t step = 0U; step < steps; step++) {
        (void)control_plant_step(&plant, 0.0F);
    }
    assert(close_enough(control_plant_value(&plant), parameters.rest_value, 0.01F));
}

/*
 * Durante el retardo no pasa nada, y eso es lo que hace difícil el problema.
 *
 * Un lazo probado contra una planta sin retardo tolera casi cualquier ganancia.
 * Comprobar que el modelo lo tiene, y que dura exactamente lo pedido, es lo que
 * da valor a las pruebas de sintonía que se hagan encima.
 */
static void test_nothing_moves_during_the_dead_time(void)
{
    control_plant_t plant = {0};
    const control_plant_parameters_t parameters = control_plant_temperature_bench();
    assert(control_plant_configure(&plant, &parameters));
    control_plant_reset(&plant, parameters.rest_value);

    const uint32_t delay_steps =
        (uint32_t)(parameters.dead_time_s / parameters.sample_period_s + 0.5F);
    assert(delay_steps == 15U);  /* 3 s con el lazo térmico a 200 ms */

    for (uint32_t step = 0U; step < delay_steps; step++) {
        const float value = control_plant_step(&plant, 100.0F);
        assert(close_enough(value, parameters.rest_value, 0.0001F));
    }
    /* El paso siguiente ya siente la actuación. */
    assert(control_plant_step(&plant, 100.0F) > parameters.rest_value + 0.1F);
}

/*
 * Tras una constante de tiempo se ha recorrido el 63 % del camino.
 *
 * Es la firma del primer orden. Si alguien cambiara la discretización, esta
 * prueba lo diría antes de que una sintonía saliera mal por una razón que
 * parecería del controlador.
 */
static void test_after_one_time_constant_it_has_covered_sixty_three_percent(void)
{
    control_plant_t plant = {0};
    control_plant_parameters_t parameters = control_plant_pressure_bench();
    parameters.dead_time_s = 0.0F;  /* el retardo desplazaría la medida */
    assert(control_plant_configure(&plant, &parameters));
    control_plant_reset(&plant, 0.0F);

    const uint32_t steps =
        (uint32_t)(parameters.time_constant_s / parameters.sample_period_s);
    for (uint32_t step = 0U; step < steps; step++) {
        (void)control_plant_step(&plant, 100.0F);
    }
    const float expected = 9.0F * (1.0F - expf(-1.0F));
    assert(close_enough(control_plant_value(&plant), expected, 0.1F));
}

/*
 * La cifra de banco térmica reproduce la física que ADR-009 § 3.7 calcula.
 *
 * 1050 W sobre unos 750 J/K dan 1,4 K/s con la caldera llena. Si el modelo se
 * alejara de ese número, los ensayos de banco dejarían de parecerse a la
 * máquina sin que nadie se enterara.
 */
static void test_the_thermal_bench_figures_match_the_documented_physics(void)
{
    control_plant_t plant = {0};
    const control_plant_parameters_t parameters = control_plant_temperature_bench();
    assert(control_plant_configure(&plant, &parameters));
    control_plant_reset(&plant, parameters.rest_value);

    /* Se atraviesa el retardo y se mide un segundo de ascenso. */
    const uint32_t delay_steps =
        (uint32_t)(parameters.dead_time_s / parameters.sample_period_s + 0.5F);
    for (uint32_t step = 0U; step < delay_steps; step++) {
        (void)control_plant_step(&plant, 100.0F);
    }
    const float before = control_plant_value(&plant);
    const uint32_t steps_per_second = (uint32_t)(1.0F / parameters.sample_period_s);
    for (uint32_t step = 0U; step < steps_per_second; step++) {
        (void)control_plant_step(&plant, 100.0F);
    }
    const float rise_per_second = control_plant_value(&plant) - before;
    assert(close_enough(rise_per_second, 1.4F, 0.05F));
}

/* Una actuación imposible se trata como cero, no como una orden enorme. */
static void test_a_non_finite_actuation_is_treated_as_zero(void)
{
    control_plant_t plant = {0};
    const control_plant_parameters_t parameters = control_plant_pressure_bench();
    assert(control_plant_configure(&plant, &parameters));
    control_plant_reset(&plant, 0.0F);

    for (uint32_t step = 0U; step < 200U; step++) {
        const float value = control_plant_step(&plant, NAN);
        assert(isfinite(value));
    }
    assert(close_enough(control_plant_value(&plant), 0.0F, 0.0001F));

    for (uint32_t step = 0U; step < 200U; step++) {
        (void)control_plant_step(&plant, INFINITY);
    }
    assert(isfinite(control_plant_value(&plant)));
    assert(close_enough(control_plant_value(&plant), 0.0F, 0.0001F));
}

static void test_it_survives_a_null_plant(void)
{
    assert(close_enough(control_plant_step(NULL, 50.0F), 0.0F, 0.0F));
    assert(close_enough(control_plant_value(NULL), 0.0F, 0.0F));
    assert(close_enough(control_plant_steady_state(NULL, 50.0F), 0.0F, 0.0F));
    control_plant_reset(NULL, 1.0F);

    /* Sin configurar no evoluciona: la estructura a ceros no modela nada. */
    control_plant_t sin_configurar = {0};
    assert(close_enough(control_plant_step(&sin_configurar, 100.0F), 0.0F, 0.0F));
}

int main(void)
{
    test_the_configuration_refuses_an_impossible_plant();
    test_a_constant_actuation_settles_where_the_gain_says();
    test_without_actuation_it_returns_to_rest();
    test_nothing_moves_during_the_dead_time();
    test_after_one_time_constant_it_has_covered_sixty_three_percent();
    test_the_thermal_bench_figures_match_the_documented_physics();
    test_a_non_finite_actuation_is_treated_as_zero();
    test_it_survives_a_null_plant();
    printf("test_control_plant: 8 pruebas correctas\n");
    return 0;
}
