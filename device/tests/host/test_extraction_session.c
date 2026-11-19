/*
 * Verificación en host de los indicadores de estabilidad de una preparación.
 *
 * Repite los casos de `packages/espresso_stability/tests/test_indicators.py`:
 * la regla de la ventana vive en los dos lados y estas pruebas son lo que
 * impide que diverjan en silencio. Si cambia un caso aquí, cambia allá.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "extraction_session.h"

/* Marca de muestra no apta, como el `None` del lado de la plataforma. */
#define UNFIT NAN

static extraction_session_t session;

static extraction_profile_t profile(void)
{
    extraction_profile_t result = {0};
    snprintf(result.id, sizeof(result.id), "perfil-prueba");
    result.version = 1U;
    result.temperature_setpoint_c = 93.0F;
    result.temperature_tolerance_c = 1.0F;
    result.pressure_setpoint_bar = 9.0F;
    result.pressure_tolerance_bar = 0.5F;
    result.maximum_time_ms = 40000U;
    return result;
}

static void run(const float points[][2], size_t count)
{
    const extraction_profile_t requested = profile();
    assert(extraction_session_start(&session, "sesion-prueba", &requested, 0U));
    for (size_t index = 0U; index < count; ++index) {
        const extraction_sample_t sample = {
            .elapsed_ms = (uint32_t)(index * 1000U),
            .observed_temperature_c = points[index][0],
            .observed_pressure_bar = points[index][1],
            .target_temperature_c = requested.temperature_setpoint_c,
            .target_pressure_bar = requested.pressure_setpoint_bar,
            .temperature_valid = !isnan(points[index][0]),
            .pressure_valid = !isnan(points[index][1]),
        };
        assert(extraction_session_record(&session, &sample));
    }
    assert(extraction_session_finish(&session, OP_TERMINATION_MANUAL_NORMAL_STOP,
                                     (uint64_t)count * 1000U));
}

static bool near(float value, float expected)
{
    return fabsf(value - expected) < 1e-3F;
}

static void test_the_initial_pressure_rise_does_not_count(void)
{
    const float points[][2] = {{90.0F, 0.0F}, {90.0F, 3.0F}, {90.0F, 6.0F},
                               {93.0F, 9.0F}, {93.0F, 9.0F}};
    run(points, 5U);
    const extraction_summary_t *summary = &session.summary;
    assert(summary->window_status == EXTRACTION_WINDOW_STABILIZED);
    assert(summary->window_start_ms == 3000U);
    /* La serie se conserva entera; solo la estabilidad mira la ventana. */
    assert(summary->sample_count == 5U);
    assert(summary->temperature_samples_in_window == 2U);
    assert(near(summary->mean_absolute_temperature_error_c, 0.0F));
    assert(near(summary->mean_absolute_pressure_error_bar, 0.0F));
    assert(near(summary->temperature_within_tolerance_percent, 100.0F));
    assert(near(summary->pressure_within_tolerance_percent, 100.0F));
}

static void test_once_open_the_window_keeps_later_departures(void)
{
    const float points[][2] = {{93.0F, 0.0F}, {93.0F, 9.0F}, {93.0F, 7.0F}, {93.0F, 9.0F}};
    run(points, 4U);
    const extraction_summary_t *summary = &session.summary;
    assert(summary->window_start_ms == 1000U);
    assert(near(summary->mean_absolute_pressure_error_bar, 2.0F / 3.0F));
    assert(near(summary->pressure_within_tolerance_percent, 200.0F / 3.0F));
}

static void test_a_preparation_that_never_reaches_its_pressure_gets_no_score(void)
{
    const float points[][2] = {{93.0F, 2.0F}, {93.0F, 4.0F}, {93.0F, 6.0F}};
    run(points, 3U);
    const extraction_summary_t *summary = &session.summary;
    assert(summary->window_status == EXTRACTION_WINDOW_NEVER_STABILIZED);
    assert(summary->temperature_samples_in_window == 0U);
    assert(summary->pressure_samples_in_window == 0U);
}

static void test_unfit_measurements_do_not_count_and_do_not_open_the_window(void)
{
    const float points[][2] = {{93.0F, UNFIT}, {UNFIT, 9.0F}, {95.0F, 9.0F}};
    run(points, 3U);
    const extraction_summary_t *summary = &session.summary;
    assert(summary->window_start_ms == 1000U);
    assert(summary->temperature_samples_in_window == 1U);
    assert(near(summary->mean_absolute_temperature_error_c, 2.0F));
    assert(near(summary->temperature_within_tolerance_percent, 0.0F));
}

static void test_the_zero_of_the_status_is_the_conservative_one(void)
{
    const extraction_summary_t zeroed = {0};
    assert(zeroed.window_status == EXTRACTION_WINDOW_UNDETERMINED);
}

/*
 * El desplazamiento de caldera del primer uso se anota al abrir si consta. Una
 * sesión recién abierta no lo declara: el cero de la estructura es «no
 * consta», no «no se compensó». Un valor imposible no se anota.
 */
static void test_the_first_use_offset_is_noted_only_when_known(void)
{
    const extraction_profile_t requested = profile();
    extraction_session_t closed = {0};
    assert(!extraction_session_note_boiler_offset(&closed, 1.0F));
    assert(!closed.summary.boiler_offset_known);
    assert(extraction_session_start(&session, "sesion-prueba", &requested, 0U));
    assert(!session.summary.boiler_offset_known);
    assert(!extraction_session_note_boiler_offset(&session, -0.5F));
    assert(!extraction_session_note_boiler_offset(&session, NAN));
    assert(!session.summary.boiler_offset_known);
    assert(extraction_session_note_boiler_offset(&session, 3.25F));
    assert(session.summary.boiler_offset_known);
    assert(session.summary.boiler_offset_c == 3.25F);
    assert(!extraction_session_note_boiler_offset(NULL, 1.0F));

    /* La ganancia de la prealimentación, con la misma regla (enmienda del
     * 2026-10-27). */
    assert(!session.summary.feedforward_gain_known);
    assert(!extraction_session_note_feedforward_gain(&session, -0.1F));
    assert(!extraction_session_note_feedforward_gain(&session, INFINITY));
    assert(!session.summary.feedforward_gain_known);
    assert(extraction_session_note_feedforward_gain(&session, 1.25F));
    assert(session.summary.feedforward_gain_known);
    assert(session.summary.feedforward_gain == 1.25F);
    assert(!extraction_session_note_feedforward_gain(&closed, 1.25F));
    assert(!extraction_session_note_feedforward_gain(NULL, 1.25F));
}

int main(void)
{
    test_the_initial_pressure_rise_does_not_count();
    test_once_open_the_window_keeps_later_departures();
    test_a_preparation_that_never_reaches_its_pressure_gets_no_score();
    test_unfit_measurements_do_not_count_and_do_not_open_the_window();
    test_the_zero_of_the_status_is_the_conservative_one();
    test_the_first_use_offset_is_noted_only_when_known();
    printf("PASS: indicadores de estabilidad sobre la ventana estabilizada\n");
    return 0;
}
