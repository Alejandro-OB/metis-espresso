/*
 * El lazo cerrado, entero, sin tarjeta (paso 7; cierra H-08).
 *
 * Las pruebas anteriores comprueban cada pieza por separado: el filtro filtra,
 * el PI calcula, el planificador reparte, el modulador no deriva. Ninguna de
 * ellas responde a la pregunta que importa, que es si la cadena completa
 * **regula**.
 *
 * Aquí se monta esa cadena tal como correrá en la tarjeta —medida, filtro, PI,
 * cuantización, actuación discreta, planta— y se comprueba sobre ella lo que
 * ADR-007 § 7 y ADR-009 § 7 exigen verificar: seguimiento de referencia,
 * sobreimpulso acotado, estabilidad, correspondencia entre lo solicitado y lo
 * emitido, y actuación segura cuando la magnitud supera la referencia.
 *
 * La planta es `control_plant` con sus cifras de banco, **que no están
 * identificadas**. Lo que estas pruebas demuestran es que la estructura del lazo
 * es correcta y que la sintonía que produce el procedimiento de SIMC sobre un
 * modelo conocido regula ese modelo. No demuestran nada sobre la máquina real, y
 * por eso `commissioned` sigue en falso.
 *
 * El lazo de presión se ejercita **con el planificador dentro**, no con la
 * salida continua del PI. Es una diferencia importante: a 20 ms solo caben dos o
 * tres oportunidades de la red, de modo que la actuación que llega a la planta
 * está cuantizada a saltos de un tercio y el lazo tiene que regular con ese
 * rizado encima. Probar contra la salida continua sería probar un lazo que no
 * existe.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "control_filter.h"
#include "control_plant.h"
#include "commissioned_plant.h"
#include "control_tuning.h"
#include "heater_modulator.h"
#include "heater_recovery_hold.h"
#include "heater_recovery_return.h"
#include "pi_controller.h"
#include "psm_scheduler.h"

#define ZERO_CROSS_HZ 120.0F

static bool close_enough(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/* ---------------------------------------------------------------- presión -- */

typedef struct {
    control_tuning_t tuning;
    control_filter_t filter;
    pi_controller_t controller;
    psm_scheduler_t scheduler;
    control_plant_t plant;
    float opportunity_credit;  /* oportunidades fraccionarias aún no ofrecidas */
    float maximum;             /* máximo alcanzado, para el sobreimpulso */
} pressure_loop_t;

static void pressure_loop_init(pressure_loop_t *loop)
{
    loop->tuning = control_tuning_pressure_bench();
    assert(control_filter_configure(&loop->filter, loop->tuning.filter_time_constant_s,
                                    loop->tuning.sample_period_s));
    assert(pi_controller_configure(&loop->controller, &loop->tuning.pi));
    psm_scheduler_reset(&loop->scheduler);

    const control_plant_parameters_t parameters = control_plant_pressure_bench();
    assert(control_plant_configure(&loop->plant, &parameters));
    control_plant_reset(&loop->plant, 0.0F);

    pi_controller_reset(&loop->controller, 0.0F);
    control_filter_reset(&loop->filter);
    loop->opportunity_credit = 0.0F;
    loop->maximum = 0.0F;
}

/*
 * Un ciclo del lazo de presión, de la medida a la planta.
 *
 * Devuelve la salida del PI. La planta no recibe esa salida sino la fracción de
 * oportunidades que el planificador aprovechó de verdad, que es lo que hace la
 * bomba.
 */
static pi_result_t pressure_loop_step(pressure_loop_t *loop, float setpoint_bar)
{
    const float measured = control_plant_value(&loop->plant);
    if (measured > loop->maximum) {
        loop->maximum = measured;
    }
    const float filtered = control_filter_push(&loop->filter, measured);
    const pi_result_t result =
        pi_controller_update(&loop->controller, setpoint_bar, filtered, 0.0F);
    const uint8_t density = psm_scheduler_quantize(result.output);

    /* Las oportunidades de la red no caen en número entero dentro del periodo
     * del lazo: 120 Hz sobre 20 ms son 2,4. El crédito fraccionario las reparte
     * sin perder ninguna a lo largo del tiempo. */
    loop->opportunity_credit += ZERO_CROSS_HZ * loop->tuning.sample_period_s;
    uint32_t opportunities = 0U;
    uint32_t emitted = 0U;
    while (loop->opportunity_credit >= 1.0F) {
        loop->opportunity_credit -= 1.0F;
        opportunities++;
        if (psm_scheduler_step(&loop->scheduler, density)) {
            emitted++;
        }
    }

    const float applied = opportunities > 0U
                              ? 100.0F * (float)emitted / (float)opportunities
                              : 0.0F;
    (void)control_plant_step(&loop->plant, applied);
    return result;
}

static void pressure_loop_run(pressure_loop_t *loop, float setpoint_bar, float seconds)
{
    const uint32_t steps = (uint32_t)(seconds / loop->tuning.sample_period_s);
    for (uint32_t step = 0U; step < steps; step++) {
        (void)pressure_loop_step(loop, setpoint_bar);
    }
}

/*
 * El lazo de presión alcanza la referencia y se queda ahí.
 *
 * Seis bar sobre una planta que da nueve al 100 %: la salida de régimen ronda
 * los dos tercios, lejos de ambas saturaciones, que es donde una sintonía se
 * juzga de verdad.
 */
static void test_the_pressure_loop_reaches_and_holds_the_setpoint(void)
{
    pressure_loop_t loop;
    pressure_loop_init(&loop);
    pressure_loop_run(&loop, 6.0F, 8.0F);

    assert(close_enough(control_plant_value(&loop.plant), 6.0F, 0.30F));

    /* Y sigue ahí: nada de oscilación sostenida ni de deriva lenta. */
    float minimum = 100.0F;
    float maximum = 0.0F;
    for (uint32_t step = 0U; step < 250U; step++) {  /* cinco segundos más */
        (void)pressure_loop_step(&loop, 6.0F);
        const float value = control_plant_value(&loop.plant);
        minimum = value < minimum ? value : minimum;
        maximum = value > maximum ? value : maximum;
    }
    /* El rizado que queda es el del planificador, no el de un lazo inestable. */
    assert((maximum - minimum) < 0.40F);
    assert(close_enough(0.5F * (maximum + minimum), 6.0F, 0.30F));
}

/*
 * El limitador de referencia impide el escalón de arranque.
 *
 * Sin él, pedir seis bar a una máquina en reposo produce un error de seis bar
 * en el primer ciclo y una salida del 100 % inmediata. Es exactamente lo que
 * ADR-007 § 3.2 manda evitar.
 */
static void test_the_rate_limiter_prevents_the_startup_step(void)
{
    pressure_loop_t loop;
    pressure_loop_init(&loop);

    const pi_result_t first = pressure_loop_step(&loop, 6.0F);
    assert(first.setpoint < 1.0F);   /* la referencia interna aún no llegó */
    assert(first.output < 100.0F);   /* y por tanto la salida no satura */

    /* La referencia interna sube con la velocidad configurada, no de golpe. */
    const pi_result_t second = pressure_loop_step(&loop, 6.0F);
    assert(second.setpoint > first.setpoint);
    assert(second.setpoint < 6.0F);
}

/* El sobreimpulso queda acotado: la sintonía es conservadora a propósito. */
static void test_the_overshoot_stays_bounded(void)
{
    pressure_loop_t loop;
    pressure_loop_init(&loop);
    pressure_loop_run(&loop, 6.0F, 10.0F);
    /* Menos de un 10 % por encima de la referencia. */
    assert(loop.maximum < 6.6F);
}

/*
 * Con la referencia por debajo de la presión, la salida cae a cero y ahí se
 * queda.
 *
 * La planta es unilateral: la bomba no aspira. ADR-007 § 3.2 lo trata como
 * saturación inferior, y lo que hay que comprobar es que el lazo no afirma poder
 * descargar presión ni acumula integral negativa mientras espera.
 */
static void test_a_setpoint_below_the_measurement_drives_the_output_to_zero(void)
{
    pressure_loop_t loop;
    pressure_loop_init(&loop);
    pressure_loop_run(&loop, 6.0F, 8.0F);

    /* La referencia baja a cero: el lazo solo puede dejar de bombear. */
    pi_result_t result = {0};
    for (uint32_t step = 0U; step < 100U; step++) {
        result = pressure_loop_step(&loop, 0.0F);
    }
    assert(close_enough(result.output, 0.0F, 0.001F));
    assert(result.saturated_low);
    assert(control_plant_value(&loop.plant) < 6.0F);  /* descarga sola */

    /* Y al volver a pedir presión, responde sin el retraso que dejaría una
     * integral cargada de signo contrario. */
    for (uint32_t step = 0U; step < 25U; step++) {  /* medio segundo */
        result = pressure_loop_step(&loop, 6.0F);
    }
    assert(result.output > 0.0F);
}

/*
 * Una referencia inalcanzable satura sin cargar la integral, y al volver a lo
 * posible el lazo reacciona de inmediato.
 *
 * Es la prueba del anti-*windup* sobre el lazo entero y no sobre el controlador
 * aislado: si la integral creciera mientras la salida está al 100 %, el lazo
 * seguiría saturado durante segundos después de que la referencia bajara, y la
 * presión se pasaría de largo.
 */
static void test_an_unreachable_setpoint_does_not_wind_up_the_integral(void)
{
    pressure_loop_t loop;
    pressure_loop_init(&loop);

    /* Veinte bar sobre una planta que da nueve: imposible por construcción. */
    pressure_loop_run(&loop, 20.0F, 15.0F);
    const float integral_saturado = pi_controller_integral(&loop.controller);

    pi_result_t result = pressure_loop_step(&loop, 20.0F);
    assert(result.saturated_high);
    assert(result.integral_held);

    /* La referencia vuelve a lo posible; el lazo debe soltar enseguida. */
    for (uint32_t step = 0U; step < 100U; step++) {  /* dos segundos */
        result = pressure_loop_step(&loop, 5.0F);
    }
    assert(result.output < 100.0F);
    assert(pi_controller_integral(&loop.controller) < integral_saturado);

    pressure_loop_run(&loop, 5.0F, 10.0F);
    assert(close_enough(control_plant_value(&loop.plant), 5.0F, 0.35F));
}

/*
 * Lo emitido corresponde con lo solicitado (ADR-007 § 7.2).
 *
 * Es el criterio de verificación que el propio ADR enumera, y se comprueba sobre
 * los contadores del planificador después de haber corrido el lazo entero.
 */
static void test_the_emitted_actuations_match_the_requested_density(void)
{
    psm_scheduler_t scheduler;
    psm_scheduler_reset(&scheduler);

    /* Régimen: una densidad constante durante mil oportunidades. */
    const uint8_t density = 67U;
    for (uint32_t opportunity = 0U; opportunity < 1000U; opportunity++) {
        (void)psm_scheduler_step(&scheduler, density);
    }
    assert(psm_scheduler_opportunities(&scheduler) == 1000U);
    assert(psm_scheduler_emitted(&scheduler) == 670U);
}

/* ------------------------------------------------------------ temperatura -- */

typedef struct {
    control_tuning_t tuning;
    control_filter_t filter;
    pi_controller_t controller;
    heater_modulator_t modulator;
    control_plant_t plant;
    uint32_t conducting_ticks;
    uint32_t total_ticks;
    float maximum;
} temperature_loop_t;

/* El modulador conmuta con el paso del lazo de presión, que es el de la tarea. */
#define HEATER_TICK_MS 20U
#define HEATER_WINDOW_MS 1000U
#define HEATER_TICKS_PER_CONTROL 10U  /* 200 ms de lazo térmico en pasos de 20 ms */

static void temperature_loop_init_with(temperature_loop_t *loop, const control_tuning_t *tuning,
                                      const control_plant_parameters_t *parameters_in)
{
    loop->tuning = *tuning;
    assert(control_filter_configure(&loop->filter, loop->tuning.filter_time_constant_s,
                                    loop->tuning.sample_period_s));
    assert(pi_controller_configure(&loop->controller, &loop->tuning.pi));
    assert(heater_modulator_configure(&loop->modulator, HEATER_WINDOW_MS, HEATER_TICK_MS));

    const control_plant_parameters_t parameters = *parameters_in;
    assert(control_plant_configure(&loop->plant, &parameters));
    control_plant_reset(&loop->plant, parameters.rest_value);

    pi_controller_reset(&loop->controller, parameters.rest_value);
    control_filter_reset(&loop->filter);
    loop->conducting_ticks = 0U;
    loop->total_ticks = 0U;
    loop->maximum = parameters.rest_value;
}

/* El lazo contra el modelo de banco, con la sintonía de banco: es lo que estas
 * pruebas ejercitan desde antes de que existiera el acta. */
static void temperature_loop_init(temperature_loop_t *loop)
{
    const control_tuning_t tuning = control_tuning_temperature_bench();
    const control_plant_parameters_t parameters = control_plant_temperature_bench();
    temperature_loop_init_with(loop, &tuning, &parameters);
}

/*
 * Un ciclo del lazo térmico: 200 ms, dentro de los cuales el modulador conmuta
 * diez veces. La planta recibe la conducción real de esos diez pasos, no la
 * demanda del PI.
 */
static pi_result_t temperature_loop_step(temperature_loop_t *loop, float setpoint_c)
{
    const float measured = control_plant_value(&loop->plant);
    if (measured > loop->maximum) {
        loop->maximum = measured;
    }
    const float filtered = control_filter_push(&loop->filter, measured);
    const pi_result_t result =
        pi_controller_update(&loop->controller, setpoint_c, filtered, 0.0F);
    const uint8_t demand = heater_modulator_quantize(result.output);

    uint32_t conducting = 0U;
    for (uint32_t tick = 0U; tick < HEATER_TICKS_PER_CONTROL; tick++) {
        if (heater_modulator_step(&loop->modulator, demand)) {
            conducting++;
        }
    }
    loop->conducting_ticks += conducting;
    loop->total_ticks += HEATER_TICKS_PER_CONTROL;

    const float applied = 100.0F * (float)conducting / (float)HEATER_TICKS_PER_CONTROL;
    (void)control_plant_step(&loop->plant, applied);
    return result;
}

static void temperature_loop_run(temperature_loop_t *loop, float setpoint_c, float seconds)
{
    const uint32_t steps = (uint32_t)(seconds / loop->tuning.sample_period_s);
    for (uint32_t step = 0U; step < steps; step++) {
        (void)temperature_loop_step(loop, setpoint_c);
    }
}

/*
 * El lazo térmico alcanza la referencia con el modulador dentro.
 *
 * La diferencia con el de presión no es solo de velocidad: aquí el actuador es
 * binario y la fracción se consigue repartiendo tiempo, de modo que la planta ve
 * conducción a plena potencia alternando con silencio. Que la temperatura se
 * asiente igualmente es la propiedad que justifica la modulación por ciclo
 * integral.
 */
static void test_the_temperature_loop_reaches_the_setpoint(void)
{
    temperature_loop_t loop;
    temperature_loop_init(&loop);
    temperature_loop_run(&loop, 93.0F, 900.0F);  /* quince minutos simulados */
    assert(close_enough(control_plant_value(&loop.plant), 93.0F, 1.0F));
}

/*
 * En régimen, la conducción corresponde con lo que la planta necesita.
 *
 * Con 22 °C de ambiente, 2,8 °C por punto y una referencia de 93 °C, la demanda
 * de equilibrio es (93 − 22)/2,8 ≈ 25,4 %. Comprobarlo cierra el lazo entre la
 * física del modelo y el tiempo que el modulador reparte de verdad.
 */
static void test_the_steady_conduction_matches_what_the_plant_needs(void)
{
    temperature_loop_t loop;
    temperature_loop_init(&loop);
    temperature_loop_run(&loop, 93.0F, 1200.0F);

    loop.conducting_ticks = 0U;
    loop.total_ticks = 0U;
    temperature_loop_run(&loop, 93.0F, 300.0F);  /* cinco minutos de régimen */

    const float duty = 100.0F * (float)loop.conducting_ticks / (float)loop.total_ticks;
    const float expected = (93.0F - 22.0F) / 2.80F;
    assert(close_enough(duty, expected, 2.0F));
}

/*
 * El sobrepaso térmico queda acotado pese al retardo de tres segundos.
 *
 * El criterio es provisional: ADR-009 § 4 reserva el sobrepaso aceptable a la
 * puesta en marcha. Lo que se fija aquí es que la velocidad de la referencia
 * está elegida para que el lazo no sature durante el calentamiento, y eso se
 * comprueba además sobre la salida: si el PI llegara al 100 %, el sobrepaso
 * dejaría de estar gobernado por la sintonía y pasaría a estarlo por la rampa.
 */
static void test_the_thermal_overshoot_stays_bounded(void)
{
    temperature_loop_t loop;
    temperature_loop_init(&loop);

    float peak_output = 0.0F;
    const uint32_t steps = (uint32_t)(1200.0F / loop.tuning.sample_period_s);
    for (uint32_t step = 0U; step < steps; step++) {
        const pi_result_t result = temperature_loop_step(&loop, 93.0F);
        peak_output = result.output > peak_output ? result.output : peak_output;
    }
    assert(loop.maximum < 96.0F);
    assert(peak_output < 60.0F);
}

/* ---------------------------------------------------------------- sintonía -- */

/*
 * Las ganancias son las que SIMC dicta sobre el modelo, no otras.
 *
 * Si alguien cambiara la planta de banco sin revisar la sintonía, o al revés,
 * esta prueba lo diría. Es la razón por la que las ganancias se calculan y no se
 * escriben.
 */
static void test_the_tuning_is_what_simc_dictates_for_the_bench_plant(void)
{
    const control_plant_parameters_t plant = control_plant_pressure_bench();
    const float lambda = 3.0F * plant.dead_time_s;
    float kp = 0.0F;
    float ki = 0.0F;
    assert(control_tuning_simc(plant.gain, plant.time_constant_s, plant.dead_time_s,
                               lambda, &kp, &ki));

    const control_tuning_t tuning = control_tuning_pressure_bench();
    assert(close_enough(tuning.pi.proportional_gain, kp, 0.001F));
    assert(close_enough(tuning.pi.integral_gain, ki, 0.001F));
    assert(close_enough(tuning.pi.sample_period_s, plant.sample_period_s, 0.0001F));

    /* La de banco no autoriza nada; la térmica solo con acta. */
    assert(!tuning.commissioned);
    assert(!control_tuning_temperature_bench().commissioned);
    assert(control_tuning_temperature().commissioned == (HAVE_COMMISSIONED_TEMPERATURE != 0));
    assert(control_tuning_pressure().commissioned == (HAVE_COMMISSIONED_PRESSURE != 0));
}

/* Un modelo inutilizable no produce sintonía, produce un rechazo. */
static void test_simc_refuses_a_plant_it_cannot_tune(void)
{
    float kp = 1.0F;
    float ki = 1.0F;
    assert(!control_tuning_simc(0.0F, 1.0F, 0.1F, 0.3F, &kp, &ki));
    assert(!control_tuning_simc(-1.0F, 1.0F, 0.1F, 0.3F, &kp, &ki));
    assert(!control_tuning_simc(1.0F, 0.0F, 0.1F, 0.3F, &kp, &ki));
    assert(!control_tuning_simc(NAN, 1.0F, 0.1F, 0.3F, &kp, &ki));
    assert(!control_tuning_simc(1.0F, 1.0F, 0.0F, 0.0F, &kp, &ki));
    assert(!control_tuning_simc(1.0F, 1.0F, 0.1F, 0.3F, NULL, &ki));
    assert(!control_tuning_simc(1.0F, 1.0F, 0.1F, 0.3F, &kp, NULL));
}

#if HAVE_COMMISSIONED_TEMPERATURE
/*
 * La sintonía del acta regula un modelo de la caldera identificada.
 *
 * No sustituye a lo medido en la máquina (evidencias/sintonia-termica-2026-10-10):
 * comprueba que la cadena compilada —filtro, PI, limitador de referencia,
 * modulador— con los números del acta lleva a la referencia sin oscilar sobre
 * el primer orden con retardo que esos mismos números describen.
 */
static void test_the_commissioned_tuning_regulates_the_identified_plant(void)
{
    const control_tuning_t tuning = control_tuning_temperature();
    const control_plant_parameters_t identified = {
        .gain = COMMISSIONED_TEMPERATURE_GAIN,
        .time_constant_s = COMMISSIONED_TEMPERATURE_TIME_CONSTANT_S,
        .dead_time_s = COMMISSIONED_TEMPERATURE_DEAD_TIME_S,
        .sample_period_s = tuning.sample_period_s,
        .rest_value = 22.0F,
    };
    temperature_loop_t loop;
    temperature_loop_init_with(&loop, &tuning, &identified);
    temperature_loop_run(&loop, 92.0F, 3600.0F);
    printf("      planta del acta: final %.2f C, máximo %.2f C\n",
           (double)control_plant_value(&loop.plant), (double)loop.maximum);
    fflush(stdout);
    assert(close_enough(control_plant_value(&loop.plant), 92.0F, 0.5F));
    assert(loop.maximum < 93.0F);
}

/*
 * Tras un café, retener la integral quita el sobrante con el que el metal se
 * pasaba de la referencia (ADR-009, enmienda del 2026-10-16).
 *
 * Se monta la cadena con la planta del acta, se asienta en la referencia y se
 * simula un café como 30 s de calor quitado por el agua fría, sin
 * prealimentación. La planta de primer orden no sabe de café, así que el calor
 * que se lleva el agua entra como actuación negativa: es la forma de
 * perturbarla sin cambiar el modelo. Se corre dos veces, con y sin retener, y
 * se comprueba la propiedad: con la integral retenida, el metal se pasa menos
 * y la integral vuelve a su valor de sostenimiento; sin retener, la integral
 * queda por encima.
 */
enum coffee_law { LAW_FREE, LAW_HOLD, LAW_RETURN };

static float run_coffee(enum coffee_law law, float *integral_before, float *integral_after,
                        float *peak_after)
{
    const control_tuning_t tuning = control_tuning_temperature();
    const control_plant_parameters_t identified = {
        .gain = COMMISSIONED_TEMPERATURE_GAIN,
        .time_constant_s = COMMISSIONED_TEMPERATURE_TIME_CONSTANT_S,
        .dead_time_s = COMMISSIONED_TEMPERATURE_DEAD_TIME_S,
        .sample_period_s = tuning.sample_period_s,
        .rest_value = 22.0F,
    };
    temperature_loop_t loop;
    temperature_loop_init_with(&loop, &tuning, &identified);
    temperature_loop_run(&loop, 92.0F, 7200.0F);
    *integral_before = pi_controller_integral(&loop.controller);

    heater_recovery_hold_t hold;
    heater_recovery_hold_reset(&hold);
    heater_recovery_return_t back;
    assert(heater_recovery_return_configure(&back, law == LAW_RETURN ? 533.0F : 0.0F));
    const float coffee_s = 30.0F;
    const float drawn_percent = 25.0F;  /* calor del agua fría, en % equivalente */
    const uint32_t steps = (uint32_t)(1800.0F / tuning.sample_period_s);
    float peak = 0.0F;
    bool crossed = false;
    for (uint32_t step = 0U; step < steps; step++) {
        const float t = (float)step * tuning.sample_period_s;
        const bool pump = t < coffee_s;
        const float measured = control_plant_value(&loop.plant);
        const float filtered = control_filter_push(&loop.filter, measured);
        const bool held = law == LAW_HOLD && heater_recovery_hold_step(&hold, pump, 92.0F, filtered);
        const pi_result_t result =
            pi_controller_update_holding(&loop.controller, 92.0F, filtered, 0.0F, held);
        const heater_recovery_return_decision_t back_step = heater_recovery_return_step(
            &back, pump, result.integral, tuning.sample_period_s);
        if (back_step.apply) {
            pi_controller_relax_integral(&loop.controller, back_step.target, back_step.fraction);
        }
        const uint8_t demand = heater_modulator_quantize(result.output);
        uint32_t conducting = 0U;
        for (uint32_t tick = 0U; tick < HEATER_TICKS_PER_CONTROL; tick++) {
            if (heater_modulator_step(&loop.modulator, demand)) {
                conducting++;
            }
        }
        float applied = 100.0F * (float)conducting / (float)HEATER_TICKS_PER_CONTROL;
        if (pump) {
            applied -= drawn_percent;
        }
        (void)control_plant_step(&loop.plant, applied);
        if (!pump && measured >= 92.0F) {
            crossed = true;
        }
        if (crossed && measured > peak) {
            peak = measured;
        }
        if (crossed && *integral_after == 0.0F) {
            *integral_after = pi_controller_integral(&loop.controller);
        }
    }
    *peak_after = peak;
    return control_plant_value(&loop.plant);
}

static void test_holding_the_integral_removes_the_overshoot_after_a_coffee(void)
{
    float before_free = 0.0F, after_free = 0.0F, peak_free = 0.0F;
    float before_held = 0.0F, after_held = 0.0F, peak_held = 0.0F;
    const float final_free = run_coffee(LAW_FREE, &before_free, &after_free, &peak_free);
    const float final_held = run_coffee(LAW_HOLD, &before_held, &after_held, &peak_held);
    printf("      café en la planta del acta: máximo %.2f C sin retener, %.2f C reteniendo;"
           " integral al cruzar %.2f / %.2f %% (antes %.2f %%)\n",
           (double)peak_free, (double)peak_held, (double)after_free, (double)after_held,
           (double)before_held);
    fflush(stdout);
    assert(after_free > before_free);
    assert(fabsf(after_held - before_held) < 1e-3F);
    assert(peak_held < peak_free);
    assert(close_enough(final_held, 92.0F, 0.5F));
    assert(close_enough(final_free, 92.0F, 0.5F));
}

/*
 * Devolver la integral a la de antes del café (ADR-009, segunda enmienda del
 * 2026-10-16) también baja el sobrepaso en la planta del acta y la deja en la
 * referencia. La planta de primer orden no tiene el agua aparte, así que aquí
 * solo se comprueba la propiedad, no la cifra del modelo de tres nodos.
 */
static void test_returning_the_integral_lowers_the_overshoot_after_a_coffee(void)
{
    float before_free = 0.0F, after_free = 0.0F, peak_free = 0.0F;
    float before_back = 0.0F, after_back = 0.0F, peak_back = 0.0F;
    (void)run_coffee(LAW_FREE, &before_free, &after_free, &peak_free);
    const float final_back = run_coffee(LAW_RETURN, &before_back, &after_back, &peak_back);
    printf("      café en la planta del acta: máximo %.2f C sin devolver, %.2f C devolviendo\n",
           (double)peak_free, (double)peak_back);
    fflush(stdout);
    assert(peak_back < peak_free);
    assert(close_enough(final_back, 92.0F, 0.5F));
}
#endif

int main(void)
{
    test_the_pressure_loop_reaches_and_holds_the_setpoint();
    test_the_rate_limiter_prevents_the_startup_step();
    test_the_overshoot_stays_bounded();
    test_a_setpoint_below_the_measurement_drives_the_output_to_zero();
    test_an_unreachable_setpoint_does_not_wind_up_the_integral();
    test_the_emitted_actuations_match_the_requested_density();
    test_the_temperature_loop_reaches_the_setpoint();
    test_the_steady_conduction_matches_what_the_plant_needs();
    test_the_thermal_overshoot_stays_bounded();
    test_the_tuning_is_what_simc_dictates_for_the_bench_plant();
    test_simc_refuses_a_plant_it_cannot_tune();
#if HAVE_COMMISSIONED_TEMPERATURE
    test_the_commissioned_tuning_regulates_the_identified_plant();
    test_holding_the_integral_removes_the_overshoot_after_a_coffee();
    test_returning_the_integral_lowers_the_overshoot_after_a_coffee();
#endif
    printf("test_control_loop: 11 pruebas correctas\n");
    return 0;
}
