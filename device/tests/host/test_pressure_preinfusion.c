/*
 * Preinfusión a presión de extracción (ADR-007, enmienda del 2026-10-12).
 *
 * Además de las reglas del módulo, una cafetera de juguete comprueba la
 * propiedad que lo justifica: con una molienda fina, el PI que empieza a
 * regular desde el principio carga la integral durante el mojado y se pasa de
 * 11 bar al cerrar la pastilla, como se midió; con la preinfusión, no.
 *
 * La cafetera es la de la simulación de esa tarde, ajustada a las cinco
 * extracciones del 2026-10-12: bomba de vibración con su curva, válvula de la
 * canastilla presurizada que abre en 6,6 bar, pastilla que se moja sin frenar y
 * cierra al absorber su volumen, y retardo. Unidades relativas: sirve para
 * comparar estrategias, no para predecir presiones.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "pi_controller.h"
#include "pressure_preinfusion.h"

static const pressure_preinfusion_parameters_t k_parameters = {
    .handover_margin_bar = 0.5F,
    .integral_start_percent = 15.0F,
};

static pressure_preinfusion_inputs_t inputs(float ceiling, float setpoint, float pressure)
{
    return (pressure_preinfusion_inputs_t){
        .ceiling_percent = ceiling, .setpoint_bar = setpoint, .pressure_bar = pressure};
}

static void test_zero_never_regulates(void)
{
    pressure_preinfusion_t preinfusion = {0};
    pressure_preinfusion_inputs_t in = inputs(75.0F, 8.5F, 11.0F);
    for (int i = 0; i < 10; ++i) {
        const pressure_preinfusion_decision_t d = pressure_preinfusion_step(&preinfusion, &in);
        assert(!d.regulate && !d.hand_over);
    }
    /* Una configuración rechazada deja el mismo estado. */
    const pressure_preinfusion_parameters_t bad = {.handover_margin_bar = 0.0F,
                                                   .integral_start_percent = 15.0F};
    assert(!pressure_preinfusion_configure(&preinfusion, &bad));
    const pressure_preinfusion_parameters_t bad_integral = {.handover_margin_bar = 0.5F,
                                                            .integral_start_percent = 120.0F};
    assert(!pressure_preinfusion_configure(&preinfusion, &bad_integral));
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
}

static void test_wets_until_the_pressure_reaches_the_margin_then_hands_over_once(void)
{
    pressure_preinfusion_t preinfusion;
    assert(pressure_preinfusion_configure(&preinfusion, &k_parameters));
    pressure_preinfusion_inputs_t in = inputs(0.0F, 8.5F, 3.0F);
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
    assert(pressure_preinfusion_phase(&preinfusion) == PRESSURE_PREINFUSION_IDLE);

    /* La meseta de la canastilla, con el rizado de la bomba, no hace pasar. */
    in = inputs(75.0F, 8.5F, 7.4F);
    pressure_preinfusion_decision_t d = pressure_preinfusion_step(&preinfusion, &in);
    assert(!d.regulate && !d.hand_over);
    assert(pressure_preinfusion_phase(&preinfusion) == PRESSURE_PREINFUSION_WETTING);

    in.pressure_bar = 8.0F;
    d = pressure_preinfusion_step(&preinfusion, &in);
    assert(d.regulate && d.hand_over && d.integral_start_percent == 15.0F);
    /* Una vez regulando no vuelve atrás aunque la presión baje. */
    in.pressure_bar = 6.0F;
    d = pressure_preinfusion_step(&preinfusion, &in);
    assert(d.regulate && !d.hand_over);

    /* El segundo de cero del final lo devuelve al principio, y la extracción
     * siguiente vuelve a mojar. */
    in = inputs(0.0F, 8.5F, 6.0F);
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
    in = inputs(75.0F, 8.5F, 6.0F);
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
    assert(pressure_preinfusion_phase(&preinfusion) == PRESSURE_PREINFUSION_WETTING);
}

static void test_no_handover_without_a_usable_setpoint_or_measurement(void)
{
    pressure_preinfusion_t preinfusion;
    assert(pressure_preinfusion_configure(&preinfusion, &k_parameters));
    pressure_preinfusion_inputs_t in = inputs(75.0F, 0.0F, 9.0F);
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
    in = inputs(75.0F, NAN, 9.0F);
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
    in = inputs(75.0F, 8.5F, NAN);
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
    in = inputs(NAN, 8.5F, 9.0F);
    assert(!pressure_preinfusion_step(&preinfusion, &in).regulate);
}

static void test_no_integral_start_when_the_pressure_already_reached_the_setpoint(void)
{
    /* Presión atrapada por encima de la referencia al empezar: pasa al PI en
     * el primer ciclo, pero sin precarga, para que la bomba no empuje. */
    pressure_preinfusion_t preinfusion;
    assert(pressure_preinfusion_configure(&preinfusion, &k_parameters));
    pressure_preinfusion_inputs_t in = inputs(75.0F, 3.0F, 3.3F);
    pressure_preinfusion_decision_t d = pressure_preinfusion_step(&preinfusion, &in);
    assert(d.regulate && d.hand_over && d.integral_start_percent == 0.0F);

    /* Justo en la referencia, tampoco: no le falta presión. */
    assert(pressure_preinfusion_configure(&preinfusion, &k_parameters));
    in = inputs(75.0F, 8.5F, 8.5F);
    d = pressure_preinfusion_step(&preinfusion, &in);
    assert(d.hand_over && d.integral_start_percent == 0.0F);

    /* Dentro del margen y por debajo, la precarga de siempre. */
    assert(pressure_preinfusion_configure(&preinfusion, &k_parameters));
    in = inputs(75.0F, 8.5F, 8.2F);
    d = pressure_preinfusion_step(&preinfusion, &in);
    assert(d.hand_over && d.integral_start_percent == 15.0F);
}

/* ---- cafetera de juguete ---- */

#define PLANT_DT_S 0.002F
#define LOOP_PERIOD_S 0.020F
#define DELAY_SAMPLES 125 /* 0,25 s de retardo entre la orden y la bomba */

typedef struct {
    float pressure;
    float absorbed;
    float puck_resistance;   /* la de la pastilla ya cerrada */
    float wet_volume;
    float delay[DELAY_SAMPLES];
    int head;
} toy_t;

static float toy_step(toy_t *toy, float demand)
{
    const float u = toy->delay[toy->head];
    toy->delay[toy->head] = demand;
    toy->head = (toy->head + 1) % DELAY_SAMPLES;
    float closing = (toy->absorbed - toy->wet_volume) / 0.2F + 0.5F;
    closing = closing < 0.0F ? 0.0F : (closing > 1.0F ? 1.0F : closing);
    const float resistance = 0.2F + (toy->puck_resistance - 0.2F) * closing;
    const float stall = 1.0F - toy->pressure / 20.0F;
    const float in = u / 100.0F * (stall > 0.0F ? stall : 0.0F);
    const float over = toy->pressure - 6.6F;
    const float out = over > 0.0F ? over / (0.3F + resistance) : 0.0F;
    toy->absorbed += out * PLANT_DT_S;
    toy->pressure += (in - out) / 0.08F * PLANT_DT_S;
    if (toy->pressure < 0.0F) {
        toy->pressure = 0.0F;
    }
    return toy->pressure;
}

/* Resistencia de la pastilla cerrada que pide `need` % para sostener 8,5 bar. */
static float puck_for(float need)
{
    return (8.5F - 6.6F) / (need / 100.0F * (1.0F - 8.5F / 20.0F)) - 0.3F;
}

typedef struct {
    float peak;
    float settled_mean;
} shot_t;

static shot_t extraction(float need, float wet_volume, bool with_preinfusion)
{
    toy_t toy = {.pressure = 3.0F, .puck_resistance = puck_for(need), .wet_volume = wet_volume};
    /* El PI de la sintonía de presión: K = 0,06, τ = θ = 0,1 s, λ = 1 s. */
    const pi_parameters_t parameters = {
        .proportional_gain = 1.5152F,
        .integral_gain = 15.152F,
        .sample_period_s = LOOP_PERIOD_S,
        .output_minimum = 0.0F,
        .output_maximum = 75.0F,
        .setpoint_rate = 3.0F,
    };
    pi_controller_t pi;
    assert(pi_controller_configure(&pi, &parameters));
    pressure_preinfusion_t preinfusion;
    assert(pressure_preinfusion_configure(&preinfusion, &k_parameters));

    shot_t shot = {0};
    float filtered = toy.pressure;
    float demand = 0.0F;
    float settled_sum = 0.0F;
    int settled_count = 0;
    bool primed = false;
    const int substeps = (int)lroundf(LOOP_PERIOD_S / PLANT_DT_S);
    for (int k = 0; k < (int)(23.0F / LOOP_PERIOD_S); ++k) {
        const float t = (float)k * LOOP_PERIOD_S;
        filtered += (toy.pressure - filtered) * 0.5F; /* 20 ms a 20 ms */
        const float ceiling = t >= 1.0F ? 75.0F : 0.0F;
        if (with_preinfusion) {
            const pressure_preinfusion_inputs_t in = inputs(ceiling, 8.5F, filtered);
            const pressure_preinfusion_decision_t d = pressure_preinfusion_step(&preinfusion, &in);
            if (d.hand_over) {
                pi_controller_reset_with_integral(&pi, filtered, d.integral_start_percent);
            }
            demand = d.regulate ? pi_controller_update(&pi, 8.5F, filtered, 0.0F).output
                                : ceiling;
        } else if (ceiling > 0.0F) {
            if (!primed) {
                pi_controller_reset(&pi, filtered);
                primed = true;
            }
            demand = pi_controller_update(&pi, 8.5F, filtered, 0.0F).output;
        }
        for (int s = 0; s < substeps; ++s) {
            const float p = toy_step(&toy, demand);
            shot.peak = p > shot.peak ? p : shot.peak;
        }
        if (t >= 19.0F) {
            settled_sum += toy.pressure;
            settled_count++;
        }
    }
    shot.settled_mean = settled_sum / (float)settled_count;
    return shot;
}

static void test_the_toy_reproduces_the_fine_grind_failure(void)
{
    /* Si la cafetera de juguete no reprodujera el fallo, la prueba siguiente
     * no demostraría nada. */
    const shot_t fine = extraction(10.0F, 2.6F, false);
    assert(fine.peak > 11.0F);
}

static void test_preinfusion_keeps_every_grind_and_wetting_below_ten_bar(void)
{
    const float needs[] = {70.0F, 54.0F, 30.0F, 10.0F, 5.0F};
    const float wettings[] = {1.3F, 2.6F, 5.2F};
    for (size_t i = 0; i < sizeof(needs) / sizeof(needs[0]); ++i) {
        for (size_t j = 0; j < sizeof(wettings) / sizeof(wettings[0]); ++j) {
            const shot_t shot = extraction(needs[i], wettings[j], true);
            assert(shot.peak < 10.0F);
            /* Y regula: al final está en la referencia. */
            assert(fabsf(shot.settled_mean - 8.5F) < 0.3F);
        }
    }
}

int main(void)
{
    test_zero_never_regulates();
    test_wets_until_the_pressure_reaches_the_margin_then_hands_over_once();
    test_no_handover_without_a_usable_setpoint_or_measurement();
    test_no_integral_start_when_the_pressure_already_reached_the_setpoint();
    test_the_toy_reproduces_the_fine_grind_failure();
    test_preinfusion_keeps_every_grind_and_wetting_below_ten_bar();
    printf("PASS: preinfusión a presión de extracción\n");
    return 0;
}
