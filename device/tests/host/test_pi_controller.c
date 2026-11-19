/*
 * Pruebas del PI unilateral compartido por los dos lazos (H-08).
 *
 * Lo que se comprueba son las propiedades de las que depende que el lazo sea
 * seguro y sintonizable, no una implementación concreta: que el actuador nunca
 * reciba una orden negativa, que la integral no se cargue contra un tope, que la
 * prealimentación no desplace la referencia y que una entrada imposible no deje
 * el controlador envenenado.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "pi_controller.h"

static bool close_enough(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static pi_parameters_t base_parameters(void)
{
    /* Periodo del lazo de presión, ADR-007 § 3.3. */
    return (pi_parameters_t){
        .proportional_gain = 2.0F,
        .integral_gain = 5.0F,
        .sample_period_s = 0.020F,
        .output_minimum = 0.0F,
        .output_maximum = 100.0F,
        .setpoint_rate = 0.0F,  /* sin límite salvo en la prueba que lo mide */
    };
}

static void test_an_unusable_configuration_is_refused(void)
{
    pi_controller_t control = {0};
    pi_parameters_t parametros = base_parameters();

    parametros.sample_period_s = 0.0F;
    assert(!pi_controller_configure(&control, &parametros));

    parametros = base_parameters();
    parametros.output_maximum = parametros.output_minimum;
    assert(!pi_controller_configure(&control, &parametros));

    parametros = base_parameters();
    parametros.proportional_gain = NAN;
    assert(!pi_controller_configure(&control, &parametros));

    assert(!pi_controller_configure(NULL, &parametros));
    assert(!pi_controller_configure(&control, NULL));
}

/*
 * Con `Ki = 0` no hay acción integral.
 *
 * No es un caso de borde: es el estado con el que arranca el firmware mientras
 * el control no esté puesto en servicio, según ADR-007 § 3.5 y ADR-009 § 3.7.
 * La salida debe ser proporcional pura y la integral quedarse en cero por
 * muchas ejecuciones que pasen.
 */
static void test_a_zero_integral_gain_yields_pure_proportional(void)
{
    pi_controller_t control = {0};
    pi_parameters_t parametros = base_parameters();
    parametros.integral_gain = 0.0F;
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    for (int i = 0; i < 100; ++i) {
        const pi_result_t r = pi_controller_update(&control, 10.0F, 5.0F, 0.0F);
        assert(close_enough(r.error, 5.0F, 1e-6F));
        assert(close_enough(r.output, 10.0F, 1e-5F));  /* Kp · e = 2 · 5 */
        assert(close_enough(r.integral, 0.0F, 1e-6F));
    }
}

/*
 * El actuador nunca recibe una orden negativa.
 *
 * Es lo que hace al controlador unilateral. Una presión por encima de la
 * referencia significa que la bomba debe parar, no que pueda aspirar; una
 * caldera caliente de más significa dejar de calentar, no enfriar. La salida
 * baja hasta el mínimo y ahí se queda.
 */
static void test_the_output_never_goes_below_the_minimum(void)
{
    pi_controller_t control = {0};
    const pi_parameters_t parametros = base_parameters();
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    for (int i = 0; i < 200; ++i) {
        const pi_result_t r = pi_controller_update(&control, 9.0F, 12.0F, 0.0F);
        assert(r.output >= parametros.output_minimum);
        assert(r.error < 0.0F);
    }
    const pi_result_t final = pi_controller_update(&control, 9.0F, 12.0F, 0.0F);
    assert(close_enough(final.output, 0.0F, 1e-6F));
    assert(final.saturated_low);
}

/*
 * La integral elimina el error estacionario que el proporcional deja.
 *
 * Se contrasta contra una planta de primer orden de ganancia conocida, donde el
 * valor de régimen del proporcional puro puede calcularse a mano:
 * `y∞ = Kp·g·r / (1 + Kp·g)`, que con `Kp = 2`, `g = 0,5` y referencia 10 da
 * una medida de 5. El hueco de 5 unidades es el error permanente que la integral
 * debe cerrar.
 *
 * La planta lleva dinámica y no es una ganancia instantánea a propósito: con
 * realimentación algebraica directa y ganancia de lazo unitaria el sistema
 * oscilaría entre dos valores sin converger, que es un artefacto del modelo de
 * prueba y no una propiedad del controlador.
 */
static float plant_step(float state, float output, float gain)
{
    const float inercia = 0.05F;
    return state + inercia * (gain * output - state);
}

static void test_the_integral_removes_the_steady_state_error(void)
{
    const float ganancia_planta = 0.5F;
    const float referencia = 10.0F;

    pi_controller_t proporcional = {0};
    pi_parameters_t sin_integral = base_parameters();
    sin_integral.integral_gain = 0.0F;
    assert(pi_controller_configure(&proporcional, &sin_integral));
    pi_controller_reset(&proporcional, 0.0F);
    float medida = 0.0F;
    for (int i = 0; i < 5000; ++i) {
        const pi_result_t r = pi_controller_update(&proporcional, referencia, medida, 0.0F);
        medida = plant_step(medida, r.output, ganancia_planta);
    }
    assert(close_enough(medida, 5.0F, 0.01F));           /* se estanca */
    assert(fabsf(referencia - medida) > 1.0F);           /* con error permanente */

    pi_controller_t completo = {0};
    const pi_parameters_t con_integral = base_parameters();
    assert(pi_controller_configure(&completo, &con_integral));
    pi_controller_reset(&completo, 0.0F);
    medida = 0.0F;
    for (int i = 0; i < 20000; ++i) {
        const pi_result_t r = pi_controller_update(&completo, referencia, medida, 0.0F);
        medida = plant_step(medida, r.output, ganancia_planta);
    }
    assert(close_enough(medida, referencia, 0.01F));     /* error cerrado */
}

/*
 * Anti-*windup*: la integral no se carga contra un tope que no puede superar.
 *
 * Es la propiedad que distingue un PI utilizable de uno que se queda pegado. Sin
 * ella, una saturación prolongada acumula un término enorme y, cuando el error
 * cambia de signo, la salida tarda cientos de ejecuciones en abandonar el tope
 * mientras el proceso se pasa de largo. Se comprueba de las dos maneras: la
 * integral permanece acotada durante la saturación, y la salida sale del tope en
 * la **primera** ejecución tras invertirse el error.
 */
static void test_anti_windup_keeps_the_integral_from_charging_against_the_limit(void)
{
    pi_controller_t control = {0};
    const pi_parameters_t parametros = base_parameters();
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    /* Error grande y sostenido: la salida satura arriba durante mucho tiempo. */
    float integral_maxima = 0.0F;
    for (int i = 0; i < 2000; ++i) {
        const pi_result_t r = pi_controller_update(&control, 100.0F, 0.0F, 0.0F);
        assert(close_enough(r.output, 100.0F, 1e-6F));
        assert(r.saturated_high);
        if (fabsf(r.integral) > integral_maxima) {
            integral_maxima = fabsf(r.integral);
        }
    }
    /* Sin anti-windup la integral habría crecido sin freno: 5 · 100 · 0,02 por
     * ejecución, es decir 20 000 tras 2000 pasos. */
    assert(integral_maxima <= parametros.output_maximum + 1.0F);

    /* Al invertirse el error, la salida abandona el tope de inmediato. */
    const pi_result_t primera = pi_controller_update(&control, 100.0F, 140.0F, 0.0F);
    assert(primera.output < parametros.output_maximum);
    assert(close_enough(primera.output, 0.0F, 1e-6F));
}

/* La misma propiedad contra el tope inferior. */
static void test_anti_windup_also_protects_the_lower_limit(void)
{
    pi_controller_t control = {0};
    const pi_parameters_t parametros = base_parameters();
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    for (int i = 0; i < 2000; ++i) {
        const pi_result_t r = pi_controller_update(&control, 0.0F, 50.0F, 0.0F);
        assert(close_enough(r.output, 0.0F, 1e-6F));
        assert(fabsf(r.integral) <= parametros.output_maximum + 1.0F);
    }
    const pi_result_t primera = pi_controller_update(&control, 0.0F, -50.0F, 0.0F);
    assert(primera.output > 0.0F);
}

/*
 * La prealimentación se suma a la salida y no desplaza la referencia.
 *
 * Es la decisión explícita de ADR-009 § 3.4 frente a la forma de Gaggiuino. La
 * comprobación es directa: con y sin prealimentación, el error debe ser
 * idéntico —porque se mide contra la misma referencia— y la salida debe diferir
 * exactamente en el término añadido.
 */
static void test_feedforward_adds_to_the_output_not_to_the_setpoint(void)
{
    pi_controller_t sin_ff = {0};
    pi_controller_t con_ff = {0};
    const pi_parameters_t parametros = base_parameters();
    assert(pi_controller_configure(&sin_ff, &parametros));
    assert(pi_controller_configure(&con_ff, &parametros));
    pi_controller_reset(&sin_ff, 90.0F);
    pi_controller_reset(&con_ff, 90.0F);

    const float prealimentacion = 12.0F;
    const pi_result_t a = pi_controller_update(&sin_ff, 93.0F, 90.0F, 0.0F);
    const pi_result_t b = pi_controller_update(&con_ff, 93.0F, 90.0F, prealimentacion);

    assert(close_enough(a.error, b.error, 1e-6F));          /* la referencia no se movió */
    assert(close_enough(a.setpoint, b.setpoint, 1e-6F));
    assert(close_enough(b.output - a.output, prealimentacion, 1e-5F));
    assert(close_enough(b.feedforward, prealimentacion, 1e-6F));
}

/*
 * La integral no se carga compensando una prealimentación que ya satura.
 *
 * ADR-009 § 3.4 lo exige explícitamente: la saturación y el anti-*windup* operan
 * sobre la suma, no sobre el término realimentado por separado. Con una
 * prealimentación que por sí sola alcanza el tope y un error todavía positivo,
 * la integral debe quedarse quieta.
 */
static void test_the_integral_does_not_charge_behind_a_saturating_feedforward(void)
{
    pi_controller_t control = {0};
    const pi_parameters_t parametros = base_parameters();
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    for (int i = 0; i < 500; ++i) {
        const pi_result_t r = pi_controller_update(&control, 10.0F, 9.0F, 100.0F);
        assert(close_enough(r.output, 100.0F, 1e-6F));
        assert(r.integral_held);
        assert(close_enough(r.integral, 0.0F, 1e-6F));
    }
}

/*
 * La referencia interna persigue a la autorizada con velocidad acotada al subir.
 *
 * Sin este limitador, autorizar 9 bar de golpe produciría una aplicación
 * instantánea del 100 %. Se comprueba que cada paso avanza como mucho
 * `rate · Ts` y que termina alcanzando la referencia.
 */
static void test_the_internal_setpoint_ramps_at_the_configured_rate(void)
{
    pi_controller_t control = {0};
    pi_parameters_t parametros = base_parameters();
    parametros.setpoint_rate = 2.0F;  /* unidades por segundo */
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    const float paso_maximo = parametros.setpoint_rate * parametros.sample_period_s;
    float anterior = 0.0F;
    for (int i = 0; i < 300; ++i) {
        const pi_result_t r = pi_controller_update(&control, 9.0F, 0.0F, 0.0F);
        assert(r.setpoint - anterior <= paso_maximo + 1e-5F);
        assert(r.setpoint <= 9.0F + 1e-5F);
        anterior = r.setpoint;
    }
    assert(close_enough(anterior, 9.0F, 1e-4F));

    /*
     * Bajar, en cambio, es inmediato. El limitador existe para que la salida no
     * salte al 100 %, y una referencia que se reduce no puede hacer eso.
     * Frenarla alargaría una actuación que ya nadie autoriza: cuando se cerró el
     * lazo contra la planta de banco, bajar de 20 a 5 bar con el limitador
     * simétrico dejaba la bomba al 100 % durante cinco segundos.
     */
    const pi_result_t bajada = pi_controller_update(&control, 1.0F, 0.0F, 0.0F);
    assert(close_enough(bajada.setpoint, 1.0F, 1e-6F));
}

/* Una velocidad no positiva significa «sin límite», que es como se deja el
 * parámetro mientras no se haya medido el valor adecuado. */
static void test_a_non_positive_rate_applies_the_setpoint_at_once(void)
{
    pi_controller_t control = {0};
    pi_parameters_t parametros = base_parameters();
    parametros.setpoint_rate = 0.0F;
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    const pi_result_t r = pi_controller_update(&control, 9.0F, 0.0F, 0.0F);
    assert(close_enough(r.setpoint, 9.0F, 1e-6F));
}

/*
 * Una entrada no finita no gobierna ni envenena.
 *
 * `sensor_scaling` puede producir un no-número ante una lectura imposible. Si
 * entrara en la integral, el controlador quedaría inutilizable de forma
 * permanente. Se responde con la salida más conservadora y el estado intacto.
 */
static void test_a_non_finite_input_is_refused_without_poisoning_the_state(void)
{
    pi_controller_t control = {0};
    const pi_parameters_t parametros = base_parameters();
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);

    for (int i = 0; i < 20; ++i) {
        pi_controller_update(&control, 10.0F, 9.0F, 0.0F);
    }
    const float integral_antes = pi_controller_integral(&control);

    const pi_result_t con_nan = pi_controller_update(&control, 10.0F, NAN, 0.0F);
    assert(close_enough(con_nan.output, parametros.output_minimum, 1e-6F));
    assert(con_nan.integral_held);

    const pi_result_t con_inf = pi_controller_update(&control, INFINITY, 9.0F, 0.0F);
    assert(close_enough(con_inf.output, parametros.output_minimum, 1e-6F));

    assert(close_enough(pi_controller_integral(&control), integral_antes, 1e-6F));
    assert(isfinite(pi_controller_integral(&control)));

    /* Y sigue funcionando después. */
    const pi_result_t despues = pi_controller_update(&control, 10.0F, 9.0F, 0.0F);
    assert(isfinite(despues.output));
    assert(close_enough(despues.error, 1.0F, 1e-5F));
}

/*
 * Reset borra la integral y ceba la referencia interna con la medida.
 *
 * Se invoca al inhibir el lazo. Sin él, al reanudar se aplicaría una integral
 * acumulada durante un tiempo en que la salida estaba cortada, que es una orden
 * calculada sobre un pasado que no ocurrió.
 */
static void test_reset_clears_the_integral_and_primes_from_the_measurement(void)
{
    pi_controller_t control = {0};
    const pi_parameters_t parametros = base_parameters();
    assert(pi_controller_configure(&control, &parametros));
    pi_controller_reset(&control, 0.0F);
    for (int i = 0; i < 100; ++i) {
        pi_controller_update(&control, 10.0F, 0.0F, 0.0F);
    }
    assert(fabsf(pi_controller_integral(&control)) > 0.0F);

    pi_controller_reset(&control, 88.0F);
    assert(close_enough(pi_controller_integral(&control), 0.0F, 1e-6F));
    assert(close_enough(pi_controller_internal_setpoint(&control), 88.0F, 1e-6F));
}

static void test_reset_with_integral_starts_from_the_given_power(void)
{
    pi_parameters_t parameters = base_parameters();
    parameters.output_maximum = 75.0F;
    pi_controller_t controller;
    assert(pi_controller_configure(&controller, &parameters));
    pi_controller_reset_with_integral(&controller, 6.0F, 15.0F);
    assert(pi_controller_integral(&controller) == 15.0F);
    assert(pi_controller_internal_setpoint(&controller) == 6.0F);
    /* Con error nulo la salida es la integral de arranque, no cero ni el techo. */
    const pi_result_t result = pi_controller_update(&controller, 6.0F, 6.0F, 0.0F);
    assert(fabsf(result.output - 15.0F) < 1e-4F);
    /* Fuera del rango se limita, y un valor no finito arranca en cero. */
    pi_controller_reset_with_integral(&controller, 6.0F, 300.0F);
    assert(pi_controller_integral(&controller) == 75.0F);
    pi_controller_reset_with_integral(&controller, 6.0F, NAN);
    assert(pi_controller_integral(&controller) == 0.0F);
}

/*
 * Retener la integral la deja donde estaba, sin quitarle autoridad al
 * proporcional, y al soltarla el controlador sigue desde ese valor.
 */
static void test_holding_keeps_the_integral_and_leaves_the_proportional_acting(void)
{
    pi_controller_t controller;
    pi_parameters_t parameters = base_parameters();
    assert(pi_controller_configure(&controller, &parameters));
    pi_controller_reset_with_integral(&controller, 10.0F, 20.0F);

    for (int i = 0; i < 100; i++) {
        const pi_result_t held = pi_controller_update_holding(&controller, 10.0F, 5.0F, 0.0F, true);
        assert(held.integral == 20.0F);
        assert(held.integral_held);
        assert(close_enough(held.output, 20.0F + 2.0F * 5.0F, 1e-4F));
    }
    const pi_result_t released = pi_controller_update_holding(&controller, 10.0F, 5.0F, 0.0F, false);
    assert(!released.integral_held);
    assert(released.integral > 20.0F);
}

/* Relajar la integral la acerca al objetivo en la fracción dada, dentro del
 * rango de salida, y no hace nada con datos imposibles. */
static void test_relaxing_moves_the_integral_toward_the_target(void)
{
    pi_controller_t controller;
    pi_parameters_t parameters = base_parameters();
    assert(pi_controller_configure(&controller, &parameters));
    pi_controller_reset_with_integral(&controller, 10.0F, 40.0F);
    pi_controller_relax_integral(&controller, 20.0F, 0.25F);
    assert(close_enough(pi_controller_integral(&controller), 35.0F, 1e-4F));
    pi_controller_relax_integral(&controller, 20.0F, 5.0F);
    assert(close_enough(pi_controller_integral(&controller), 20.0F, 1e-4F));
    pi_controller_relax_integral(&controller, 500.0F, 1.0F);
    assert(pi_controller_integral(&controller) == 100.0F);
    pi_controller_relax_integral(&controller, NAN, 0.5F);
    pi_controller_relax_integral(&controller, 0.0F, NAN);
    pi_controller_relax_integral(&controller, 0.0F, -1.0F);
    assert(pi_controller_integral(&controller) == 100.0F);
}

int main(void)
{
    test_an_unusable_configuration_is_refused();
    test_a_zero_integral_gain_yields_pure_proportional();
    test_the_output_never_goes_below_the_minimum();
    test_the_integral_removes_the_steady_state_error();
    test_anti_windup_keeps_the_integral_from_charging_against_the_limit();
    test_anti_windup_also_protects_the_lower_limit();
    test_feedforward_adds_to_the_output_not_to_the_setpoint();
    test_the_integral_does_not_charge_behind_a_saturating_feedforward();
    test_the_internal_setpoint_ramps_at_the_configured_rate();
    test_a_non_positive_rate_applies_the_setpoint_at_once();
    test_a_non_finite_input_is_refused_without_poisoning_the_state();
    test_reset_clears_the_integral_and_primes_from_the_measurement();
    test_reset_with_integral_starts_from_the_given_power();
    test_holding_keeps_the_integral_and_leaves_the_proportional_acting();
    test_relaxing_moves_the_integral_toward_the_target();
    printf("test_pi_controller: todas las comprobaciones pasaron\n");
    return 0;
}
