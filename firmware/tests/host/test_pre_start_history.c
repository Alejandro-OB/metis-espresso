/*
 * Verificación en host del minuto previo a pulsar (enmienda del 2026-10-19 a
 * ADR-012 § 4.1): el anillo que lo guarda y la copia que toma la sesión.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "extraction_session.h"
#include "pre_start_history.h"

#define TASK_PERIOD_MS 100U

static pre_start_history_t history;
static pre_start_sample_t frozen[PRE_START_HISTORY_CAPACITY];
static extraction_session_t session;

static pre_start_observation_t observation(float temperature, bool measured)
{
    const pre_start_observation_t value = {
        .temperature_c = temperature,
        .pressure_bar = 0.1F,
        .temperature_valid = true,
        .pressure_valid = true,
        .measured = measured,
    };
    return value;
}

/* Observa como la tarea, cada 100 ms, desde `from_ms` hasta antes de `to_ms`. */
static void observe_ready(uint32_t from_ms, uint32_t to_ms, bool measured)
{
    for (uint32_t now = from_ms; now != to_ms; now += TASK_PERIOD_MS) {
        const pre_start_observation_t value = observation(92.0F, measured);
        (void)pre_start_history_observe(&history, now, &value);
    }
}

/* Las propiedades que la plataforma necesita de cualquier minuto previo. */
static void assert_well_formed(uint32_t count)
{
    for (uint32_t index = 0U; index < count; ++index) {
        assert(frozen[index].elapsed_ms < 0);
        assert(frozen[index].elapsed_ms >= -(int32_t)PRE_START_HISTORY_WINDOW_MS);
        if (index > 0U) {
            assert(frozen[index].elapsed_ms > frozen[index - 1U].elapsed_ms);
        }
    }
}

static void test_a_full_minute_on_the_grid(void)
{
    /* Noventa segundos lista y se pulsa: sale el último minuto, una muestra
       cada 500 ms, en orden y con instante negativo. */
    pre_start_history_reset(&history);
    observe_ready(1000U, 91000U, true);
    const uint32_t press = 91000U;
    const uint32_t count = pre_start_history_freeze(&history, press, true, frozen,
                                                    PRE_START_HISTORY_CAPACITY);
    assert_well_formed(count);
    assert(count == PRE_START_HISTORY_WINDOW_MS / PRE_START_HISTORY_INTERVAL_MS);
    for (uint32_t index = 1U; index < count; ++index) {
        assert(frozen[index].elapsed_ms - frozen[index - 1U].elapsed_ms ==
               (int32_t)PRE_START_HISTORY_INTERVAL_MS);
    }
    /* La más reciente está a un intervalo o menos de la pulsación. */
    assert(frozen[count - 1U].elapsed_ms >= -(int32_t)PRE_START_HISTORY_INTERVAL_MS);
}

static void test_the_grid_does_not_drift_with_jitter(void)
{
    /* La tarea no despierta exacta. Con un criterio de «500 ms desde la
       anterior» cada retraso se acumularía y la serie perdería densidad; con
       la rejilla, la k-ésima muestra cae en el k-ésimo intervalo. */
    pre_start_history_reset(&history);
    static const uint32_t jitter[] = {0U, 30U, 70U, 10U, 90U, 50U};
    uint32_t now = 0U;
    for (uint32_t tick = 0U; tick < 600U; ++tick) {
        const pre_start_observation_t value = observation(92.0F, true);
        (void)pre_start_history_observe(&history, now + jitter[tick % 6U], &value);
        now += TASK_PERIOD_MS;
    }
    const uint32_t count =
        pre_start_history_freeze(&history, now, true, frozen, PRE_START_HISTORY_CAPACITY);
    assert_well_formed(count);
    for (uint32_t index = 0U; index < count; ++index) {
        const int32_t since_anchor = (int32_t)now + frozen[index].elapsed_ms;
        const int32_t slot = since_anchor / (int32_t)PRE_START_HISTORY_INTERVAL_MS;
        if (index > 0U) {
            const int32_t previous =
                ((int32_t)now + frozen[index - 1U].elapsed_ms) / (int32_t)PRE_START_HISTORY_INTERVAL_MS;
            assert(slot == previous + 1);
        }
    }
}

static void test_without_history_nothing_is_invented(void)
{
    /* Recién encendida o recién llegada a «lista»: no hay minuto que dar. */
    pre_start_history_reset(&history);
    assert(pre_start_history_freeze(&history, 5000U, true, frozen,
                                    PRE_START_HISTORY_CAPACITY) == 0U);

    /* Diez segundos lista: van esos diez, no un minuto relleno. */
    observe_ready(20000U, 30000U, true);
    const uint32_t count =
        pre_start_history_freeze(&history, 30000U, true, frozen, PRE_START_HISTORY_CAPACITY);
    assert_well_formed(count);
    assert(count == 20U);
    assert(frozen[0].elapsed_ms == -10000);
}

static void test_a_gap_breaks_the_streak(void)
{
    /* Si la máquina dejó de estar lista —o nadie la observó— lo anterior no es
       «minuto previo con la máquina lista» y no se entrega como tal. */
    pre_start_history_reset(&history);
    observe_ready(0U, 30000U, true);
    observe_ready(30000U + 2000U, 42000U, true);
    const uint32_t count =
        pre_start_history_freeze(&history, 42000U, true, frozen, PRE_START_HISTORY_CAPACITY);
    assert_well_formed(count);
    assert(frozen[0].elapsed_ms >= -10000);
}

static void test_measured_and_simulated_do_not_mix(void)
{
    pre_start_history_reset(&history);
    observe_ready(0U, 20000U, false);
    observe_ready(20000U, 30000U, true);
    /* El cambio de origen descartó lo simulado. */
    uint32_t count =
        pre_start_history_freeze(&history, 30000U, true, frozen, PRE_START_HISTORY_CAPACITY);
    assert(count == 20U);
    /* Y una preparación de otro origen no se lleva este minuto. */
    count = pre_start_history_freeze(&history, 30000U, false, frozen, PRE_START_HISTORY_CAPACITY);
    assert(count == 0U);
}

static void test_an_unfit_sample_is_kept_marked(void)
{
    pre_start_history_reset(&history);
    observe_ready(0U, 2000U, true);
    pre_start_observation_t unfit = observation(-1.0F, true);
    unfit.temperature_valid = false;
    assert(pre_start_history_observe(&history, 2000U, &unfit));
    observe_ready(2100U, 4000U, true);
    const uint32_t count =
        pre_start_history_freeze(&history, 4000U, true, frozen, PRE_START_HISTORY_CAPACITY);
    bool seen = false;
    for (uint32_t index = 0U; index < count; ++index) {
        if (frozen[index].elapsed_ms == -2000) {
            /* Ni saltada ni sustituida por la vecina: está, y marcada. */
            assert(!frozen[index].temperature_valid);
            assert(frozen[index].pressure_valid);
            seen = true;
        }
    }
    assert(seen);
}

static void test_only_what_came_before_pressing(void)
{
    /* Lo tomado en el mismo milisegundo tendría instante cero y chocaría con
       la primera muestra de la preparación; lo posterior no es previo. */
    pre_start_history_reset(&history);
    observe_ready(0U, 10100U, true);
    const uint32_t count =
        pre_start_history_freeze(&history, 10000U, true, frozen, PRE_START_HISTORY_CAPACITY);
    assert_well_formed(count);
    assert(frozen[count - 1U].elapsed_ms == -500);
}

static void test_the_clock_wrap_does_not_break_it(void)
{
    /* El reloj monótono truncado a 32 bits da la vuelta cada 49 días. */
    pre_start_history_reset(&history);
    const uint32_t start = UINT32_MAX - 29999U;
    const uint32_t press = start + 60000U;  /* pasa por cero */
    observe_ready(start, press, true);
    const uint32_t count =
        pre_start_history_freeze(&history, press, true, frozen, PRE_START_HISTORY_CAPACITY);
    assert_well_formed(count);
    assert(count == PRE_START_HISTORY_WINDOW_MS / PRE_START_HISTORY_INTERVAL_MS);
}

static void test_the_session_takes_it_at_opening(void)
{
    extraction_profile_t profile = {0};
    snprintf(profile.id, sizeof(profile.id), "perfil-prueba");
    profile.version = 1U;
    profile.pressure_setpoint_bar = 9.0F;
    profile.pressure_tolerance_bar = 0.5F;
    profile.temperature_setpoint_c = 93.0F;
    profile.temperature_tolerance_c = 1.0F;

    pre_start_history_reset(&history);
    observe_ready(0U, 90000U, true);

    /* El instante de pulsar es el de apertura: los dos lados comparten origen. */
    assert(extraction_session_start(&session, "sesion-prueba", &profile, 90000U));
    const uint32_t taken = extraction_session_take_pre_start(&session, &history, true);
    assert(taken == session.pre_start_count);
    assert(taken == PRE_START_HISTORY_WINDOW_MS / PRE_START_HISTORY_INTERVAL_MS);
    assert(session.pre_start[taken - 1U].elapsed_ms == -500);

    /* El minuto previo no es la preparación: ni cuenta muestras ni abre la
       ventana de estabilidad, aunque su presión fuera la pedida. */
    assert(session.summary.sample_count == 0U);
    const extraction_sample_t sample = {
        .elapsed_ms = 0U,
        .observed_temperature_c = 93.0F,
        .observed_pressure_bar = 2.0F,
        .target_temperature_c = 93.0F,
        .target_pressure_bar = 9.0F,
        .temperature_valid = true,
        .pressure_valid = true,
    };
    assert(extraction_session_record(&session, &sample));
    assert(session.summary.sample_count == 1U);
    assert(!session.window_open);

    /* Después de la primera muestra ya no es «al pulsar». */
    assert(extraction_session_take_pre_start(&session, &history, true) == 0U);
    assert(session.pre_start_count == taken);

    /* Una sesión nueva empieza sin minuto previo hasta que lo toma. */
    assert(extraction_session_start(&session, "sesion-otra", &profile, 95000U));
    assert(session.pre_start_count == 0U);
}

int main(void)
{
    test_a_full_minute_on_the_grid();
    test_the_grid_does_not_drift_with_jitter();
    test_without_history_nothing_is_invented();
    test_a_gap_breaks_the_streak();
    test_measured_and_simulated_do_not_mix();
    test_an_unfit_sample_is_kept_marked();
    test_only_what_came_before_pressing();
    test_the_clock_wrap_does_not_break_it();
    test_the_session_takes_it_at_opening();
    printf("PASS: minuto previo a pulsar (anillo %zu B, copia en la sesión %zu B)\n",
           sizeof(pre_start_history_t),
           sizeof(((extraction_session_t *)0)->pre_start) + sizeof(uint32_t));
    return 0;
}
