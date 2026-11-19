/*
 * Pruebas del arranque rápido del calentador (ADR-009, enmienda del
 * 2026-10-11).
 *
 * Comprueban propiedades, no una secuencia concreta: que sin parámetros no
 * actúe, que la energía al 100 % quede acotada aunque el sensor no llegue, que
 * el corte siga a la referencia y que, con un margen mayor que el arrastre, la
 * temperatura no pase de la referencia.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "heater_fast_start.h"

static const heater_fast_start_parameters_t PARAMS = {
    .margin_c = 10.0F,
    .activation_band_c = 5.0F,
    .max_boost_s = 600.0F,
    .max_setpoint_c = 93.0F,
    .release_drop_c = 0.2F,
};

static heater_fast_start_inputs_t heating_at(float temperature, float setpoint)
{
    return (heater_fast_start_inputs_t){.heating = true,
                                        .measurement_valid = true,
                                        .pump_allowed = false,
                                        .setpoint_c = setpoint,
                                        .temperature_c = temperature,
                                        .elapsed_s = 1.0F};
}

static heater_fast_start_t configured(void)
{
    heater_fast_start_t fs;
    assert(heater_fast_start_configure(&fs, &PARAMS));
    return fs;
}

/* Sin configurar —una estructura en cero— no gobierna nunca la salida. */
static void test_unconfigured_never_overrides(void)
{
    heater_fast_start_t zeroed = {0};
    for (int k = 0; k < 100; ++k) {
        const heater_fast_start_inputs_t in = heating_at(25.0F, 92.0F);
        const heater_fast_start_decision_t d = heater_fast_start_step(&zeroed, &in);
        assert(!d.overrides && !d.hand_over);
    }
    const heater_fast_start_inputs_t in = heating_at(25.0F, 92.0F);
    assert(!heater_fast_start_step(NULL, &in).overrides);
}

/* Un parámetro que ninguna medida puede dar deja el módulo sin actuar, también
 * si antes había uno válido. */
static void test_impossible_parameters_switch_it_off(void)
{
    const float bad[] = {0.0F, -1.0F, NAN, INFINITY};
    for (size_t i = 0U; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        heater_fast_start_parameters_t fields[5];
        for (int f = 0; f < 5; ++f) {
            fields[f] = PARAMS;
        }
        fields[0].margin_c = bad[i];
        fields[1].max_boost_s = bad[i];
        fields[2].max_setpoint_c = bad[i];
        fields[3].activation_band_c = bad[i] == 0.0F ? -1.0F : bad[i];
        fields[4].release_drop_c = bad[i] == 0.0F ? -1.0F : bad[i];
        for (int f = 0; f < 5; ++f) {
            heater_fast_start_t fs = configured();
            assert(!heater_fast_start_configure(&fs, &fields[f]));
            const heater_fast_start_inputs_t in = heating_at(25.0F, 92.0F);
            assert(!heater_fast_start_step(&fs, &in).overrides);
        }
    }
}

/* Arma solo con distancia, temperatura apta, bomba parada y referencia por
 * debajo del máximo; y solo en el primer ciclo del episodio. */
static void test_arms_only_when_every_condition_holds(void)
{
    heater_fast_start_t fs = configured();
    heater_fast_start_inputs_t in = heating_at(25.0F, 92.0F);
    assert(heater_fast_start_step(&fs, &in).overrides);

    struct {
        float temperature, setpoint;
        bool valid, pump;
    } cases[] = {
        {77.5F, 92.0F, true, false},  /* dentro de la banda: basta el PI */
        {25.0F, 93.5F, true, false},  /* referencia por encima del máximo */
        {25.0F, 92.0F, false, false}, /* temperatura no apta */
        {25.0F, 92.0F, true, true},   /* bomba autorizada */
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        heater_fast_start_t c = configured();
        heater_fast_start_inputs_t ci = heating_at(cases[i].temperature, cases[i].setpoint);
        ci.measurement_valid = cases[i].valid;
        ci.pump_allowed = cases[i].pump;
        assert(!heater_fast_start_step(&c, &ci).overrides);
        /* Si no armó al entrar, no arma después aunque las condiciones vuelvan. */
        const heater_fast_start_inputs_t later = heating_at(25.0F, 92.0F);
        for (int k = 0; k < 10; ++k) {
            assert(!heater_fast_start_step(&c, &later).overrides);
        }
    }
}

/* El corte es r − δ para cualquier referencia: el número absoluto no existe. */
static void test_the_cut_follows_the_setpoint(void)
{
    for (float r = 80.0F; r <= 93.0F; r += 1.0F) {
        heater_fast_start_t fs = configured();
        float t = 25.0F;
        heater_fast_start_inputs_t in = heating_at(t, r);
        while (heater_fast_start_step(&fs, &in).output == 100.0F) {
            assert(t < r - PARAMS.margin_c);
            t += 0.5F;
            in = heating_at(t, r);
        }
        assert(heater_fast_start_phase(&fs) == HEATER_FAST_START_COAST);
        assert(heater_fast_start_end(&fs) == HEATER_FAST_START_END_CUT);
        assert(t >= r - PARAMS.margin_c && t < r - PARAMS.margin_c + 0.5F);
    }
}

/* Aunque el sensor no suba nunca, el tiempo al 100 % no pasa de t_máx: la
 * energía entregada queda acotada por construcción. */
static void test_full_power_time_is_bounded(void)
{
    heater_fast_start_t fs = configured();
    const heater_fast_start_inputs_t stuck = heating_at(25.0F, 92.0F);
    float at_full = 0.0F;
    for (int k = 0; k < 5000; ++k) {
        const heater_fast_start_decision_t d = heater_fast_start_step(&fs, &stuck);
        if (d.overrides && d.output == 100.0F) {
            at_full += stuck.elapsed_s;
        }
    }
    assert(at_full <= PARAMS.max_boost_s + stuck.elapsed_s);
    assert(heater_fast_start_end(&fs) == HEATER_FAST_START_END_TIMEOUT);
    /* Y la espera tampoco deja la caldera sin calentar para siempre. */
    assert(heater_fast_start_phase(&fs) == HEATER_FAST_START_HANDED_OVER);
}

/* Tras el corte, 0 % hasta que deje de subir; el PI recibe el mando una sola
 * vez y no vuelve a perderlo en el episodio. */
static void test_coast_then_a_single_hand_over(void)
{
    heater_fast_start_t fs = configured();
    heater_fast_start_inputs_t in = heating_at(25.0F, 92.0F);
    (void)heater_fast_start_step(&fs, &in);
    in = heating_at(82.0F, 92.0F);
    (void)heater_fast_start_step(&fs, &in);
    assert(heater_fast_start_phase(&fs) == HEATER_FAST_START_COAST);

    const float rising_then_falling[] = {83.0F, 84.0F, 84.5F, 84.6F, 84.5F, 84.3F, 84.0F};
    int hand_overs = 0;
    for (size_t i = 0U; i < sizeof(rising_then_falling) / sizeof(rising_then_falling[0]); ++i) {
        in = heating_at(rising_then_falling[i], 92.0F);
        const heater_fast_start_decision_t d = heater_fast_start_step(&fs, &in);
        if (d.hand_over) {
            ++hand_overs;
            assert(!d.overrides);
            /* No antes de caer `release_drop_c` desde el máximo. */
            assert(rising_then_falling[i] <= 84.6F - PARAMS.release_drop_c);
        } else if (hand_overs == 0) {
            assert(d.overrides && d.output == 0.0F);
        } else {
            assert(!d.overrides);
        }
    }
    assert(hand_overs == 1);
}

/* La bomba o una medida no apta durante el 100 % lo abandonan sin volver. */
static void test_abandons_and_stays_with_the_pi(void)
{
    for (int cause = 0; cause < 2; ++cause) {
        heater_fast_start_t fs = configured();
        heater_fast_start_inputs_t in = heating_at(25.0F, 92.0F);
        (void)heater_fast_start_step(&fs, &in);
        in = heating_at(40.0F, 92.0F);
        in.pump_allowed = cause == 0;
        in.measurement_valid = cause == 0;
        const heater_fast_start_decision_t d = heater_fast_start_step(&fs, &in);
        assert(d.hand_over && !d.overrides);
        assert(heater_fast_start_end(&fs) == HEATER_FAST_START_END_ABANDONED);
        in = heating_at(40.0F, 92.0F);
        for (int k = 0; k < 100; ++k) {
            assert(!heater_fast_start_step(&fs, &in).overrides);
        }
    }
}

/* Salir de «calentando» cierra el episodio: el siguiente vuelve a decidir. */
static void test_a_new_episode_decides_again(void)
{
    heater_fast_start_t fs = configured();
    heater_fast_start_inputs_t in = heating_at(25.0F, 92.0F);
    assert(heater_fast_start_step(&fs, &in).overrides);
    in.heating = false;
    assert(!heater_fast_start_step(&fs, &in).overrides);
    assert(heater_fast_start_phase(&fs) == HEATER_FAST_START_IDLE);
    in.heating = true;
    assert(heater_fast_start_step(&fs, &in).overrides);
}

/* Caldera de juguete que sigue subiendo tras cortar: un elemento que guarda
 * calor y lo cede al cuerpo, donde está el sensor. Devuelve el máximo del
 * cuerpo y deja en `cut_c` la temperatura a la que cortó. */
static float run_toy_boiler(float margin, float setpoint, float *cut_c)
{
    heater_fast_start_parameters_t p = PARAMS;
    p.margin_c = margin;
    p.max_setpoint_c = setpoint;
    heater_fast_start_t fs;
    assert(heater_fast_start_configure(&fs, &p));
    float element = 25.0F, body = 25.0F, peak = 0.0F;
    *cut_c = NAN;
    for (int k = 0; k < 4000; ++k) {
        const heater_fast_start_inputs_t in = heating_at(body, setpoint);
        const heater_fast_start_decision_t d = heater_fast_start_step(&fs, &in);
        if (isnan(*cut_c) && heater_fast_start_phase(&fs) == HEATER_FAST_START_COAST) {
            *cut_c = body;
        }
        const float u = d.overrides ? d.output : 0.0F; /* el PI no se modela */
        element += 0.05F * u - 0.05F * (element - body);
        body += 0.01F * (element - body) - 0.0005F * (body - 25.0F);
        peak = body > peak ? body : peak;
        if (heater_fast_start_phase(&fs) == HEATER_FAST_START_HANDED_OVER) {
            break;
        }
    }
    assert(heater_fast_start_end(&fs) == HEATER_FAST_START_END_CUT);
    return peak;
}

/*
 * El procedimiento de ADR-009 en miniatura: un primer arranque con un margen
 * grande mide el arrastre; con δ = arrastre más un margen, la temperatura no
 * pasa de la referencia. Vale porque el elemento llega a su gradiente mucho
 * antes del corte, y el acta tiene que comprobar lo mismo en la máquina.
 */
static void test_a_margin_from_the_measured_drag_does_not_overshoot(void)
{
    const float setpoint = 92.0F;
    float cut;
    const float drag = run_toy_boiler(40.0F, setpoint, &cut) - cut;
    assert(drag > 1.0F); /* la caldera de juguete arrastra de verdad */
    assert(run_toy_boiler(drag + 0.5F, setpoint, &cut) <= setpoint);
    /* Y con un margen menor que el arrastre se pasa: la prueba no es vacía. */
    assert(run_toy_boiler(drag * 0.5F, setpoint, &cut) > setpoint);
}

int main(void)
{
    test_unconfigured_never_overrides();
    test_impossible_parameters_switch_it_off();
    test_arms_only_when_every_condition_holds();
    test_the_cut_follows_the_setpoint();
    test_full_power_time_is_bounded();
    test_coast_then_a_single_hand_over();
    test_abandons_and_stays_with_the_pi();
    test_a_new_episode_decides_again();
    test_a_margin_from_the_measured_drag_does_not_overshoot();
    puts("heater_fast_start: todas las pruebas pasan");
    return 0;
}
