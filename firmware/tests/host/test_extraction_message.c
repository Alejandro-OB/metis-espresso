/* Verificación en host de la serialización del resumen de una preparación. */

#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "extraction_message.h"
#include "extraction_profile.h"
#include "extraction_session.h"

#define BUFFER_BYTES 1537U  /* límite de una entrada de la cola persistente */

static extraction_summary_t completed_summary(void)
{
    extraction_summary_t summary = {0};
    snprintf(summary.id, sizeof(summary.id), "sim-00000042");
    summary.effective_profile = extraction_simulation_default_profile();
    summary.duration_ms = 27500U;
    summary.termination = OP_TERMINATION_MANUAL_NORMAL_STOP;
    summary.sample_count = 275U;
    summary.dropped_samples = 3U;
    summary.mean_absolute_temperature_error_c = 0.482F;
    summary.mean_absolute_pressure_error_bar = 0.174F;
    summary.temperature_within_tolerance_percent = 91.25F;
    summary.pressure_within_tolerance_percent = 84.5F;
    summary.window_status = EXTRACTION_WINDOW_STABILIZED;
    summary.window_start_ms = 4200U;
    summary.temperature_samples_in_window = 240U;
    summary.pressure_samples_in_window = 240U;
    summary.complete = true;
    return summary;
}

static extraction_summary_t never_stabilized_summary(void)
{
    extraction_summary_t summary = completed_summary();
    summary.mean_absolute_temperature_error_c = 0.0F;
    summary.mean_absolute_pressure_error_bar = 0.0F;
    summary.temperature_within_tolerance_percent = 0.0F;
    summary.pressure_within_tolerance_percent = 0.0F;
    summary.window_status = EXTRACTION_WINDOW_NEVER_STABILIZED;
    summary.window_start_ms = 0U;
    summary.temperature_samples_in_window = 0U;
    summary.pressure_samples_in_window = 0U;
    return summary;
}

static extraction_message_context_t context(void)
{
    return (extraction_message_context_t){
        .device_id = "esp32s3-prototype-01",
        .barista = "barista-lab-01",
        .measured = false,
        .sequence = 7U,
        .epoch_ms_at_finish = UINT64_C(1789398030123),  /* 2026-09-14T15:00:30.123Z */
    };
}

static void test_identifier_is_stable_and_derived_from_the_session(void)
{
    const extraction_summary_t summary = completed_summary();
    char first[EXTRACTION_MESSAGE_ID_LENGTH + 1U];
    char second[EXTRACTION_MESSAGE_ID_LENGTH + 1U];
    assert(extraction_message_identifier(&summary, first, sizeof(first)) ==
           EXTRACTION_MESSAGE_OK);
    assert(extraction_message_identifier(&summary, second, sizeof(second)) ==
           EXTRACTION_MESSAGE_OK);
    assert(strcmp(first, second) == 0);
    assert(strcmp(first, "extraction-sim-00000042") == 0);
}

static void test_builds_an_envelope_that_fits_the_queue_entry(void)
{
    const extraction_summary_t summary = completed_summary();
    const extraction_message_context_t ctx = context();
    char buffer[BUFFER_BYTES];
    size_t written = 0U;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), &written) ==
           EXTRACTION_MESSAGE_OK);
    assert(written > 0U && written < sizeof(buffer));

    assert(strstr(buffer, "\"messageType\":\"extraction.completed\"") != NULL);
    assert(strstr(buffer, "\"messageId\":\"extraction-sim-00000042\"") != NULL);
    assert(strstr(buffer, "\"deviceId\":\"esp32s3-prototype-01\"") != NULL);
    assert(strstr(buffer, "\"sequence\":7") != NULL);
    assert(strstr(buffer, "\"source\":\"simulation\"") != NULL);
    assert(strstr(buffer, "\"termination\":\"manual-normal-stop\"") != NULL);
    assert(strstr(buffer, "\"sampleCount\":275") != NULL);
    assert(strstr(buffer, "\"barista\":\"barista-lab-01\"") != NULL);
    /* el instante de fin y el de inicio se derivan de la duración registrada */
    assert(strstr(buffer, "\"finishedAt\":\"2026-09-14T15:00:30.123Z\"") != NULL);
    assert(strstr(buffer, "\"startedAt\":\"2026-09-14T15:00:02.623Z\"") != NULL);
    printf("      envolvente de %zu bytes sobre un límite de %u\n", written,
           (unsigned)(BUFFER_BYTES - 1U));
}

/*
 * Con qué se midió viaja con la preparación (RF-014). Sin calibración ni versión
 * declaradas se escribe null, no un valor plausible; y con los identificadores
 * más largos que admiten la calibración y una versión de git, el mensaje sigue
 * cabiendo en una entrada de la cola.
 */
static void test_declares_the_calibration_and_firmware_it_was_measured_with(void)
{
    const extraction_summary_t summary = completed_summary();
    char buffer[BUFFER_BYTES];

    extraction_message_context_t ctx = context();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"calibration\":null") != NULL);
    assert(strstr(buffer, "\"firmwareVersion\":null") != NULL);

    ctx.calibration_id = "ntc-2026-10-09";
    ctx.calibration_version = 4U;
    ctx.firmware_version = "861347f";
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"calibration\":{\"id\":\"ntc-2026-10-09\",\"version\":4}") != NULL);
    assert(strstr(buffer, "\"firmwareVersion\":\"861347f\"") != NULL);

    ctx.calibration_id = "abcdefghijklmnopqrstuvwxyz01234";  /* 31: el máximo */
    ctx.calibration_version = UINT32_MAX;
    ctx.firmware_version = "v9.99.999-999-g0123456789-dirty";
    size_t written = 0U;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), &written) ==
           EXTRACTION_MESSAGE_OK);
    assert(written < BUFFER_BYTES - 1U);
}

/*
 * La recta con que se convirtió el agua viaja con la preparación, con su acta;
 * sin acta o sin una recta válida va null, no la de otra acta. Con el acta más
 * larga que admite el contrato y todo lo demás al máximo, sigue cabiendo.
 */
static void test_declares_the_water_reference_it_converted_with(void)
{
    char buffer[BUFFER_BYTES];
    const extraction_summary_t summary = completed_summary();
    extraction_message_context_t ctx = context();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"waterReference\":null") != NULL);

    ctx.water_reference = (water_reference_t){
        .defined = true, .gain = 0.8819F, .offset_c = -0.49F,
        .boiler_minimum_c = 97.3F, .boiler_maximum_c = 105.6F};
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"waterReference\":null") != NULL);  /* sin acta, no consta */

    ctx.temperature_plant = "acta-temperatura-20261028-v11";
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer,
                  "\"waterReference\":{\"plant\":\"acta-temperatura-20261028-v11\""
                  ",\"gain\":0.8819,\"offsetC\":-0.490"
                  ",\"boilerMinimumC\":97.30,\"boilerMaximumC\":105.60}") != NULL);

    ctx.water_reference.boiler_maximum_c = 90.0F;  /* intervalo al revés: no vale */
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"waterReference\":null") != NULL);

    extraction_summary_t full = completed_summary();
    full.boiler_offset_known = true;
    full.boiler_offset_c = 3.25F;
    full.feedforward_gain_known = true;
    full.feedforward_gain = 1.25F;
    ctx.water_reference = (water_reference_t){
        .defined = true, .gain = 0.88191F, .offset_c = -100.123F,
        .boiler_minimum_c = 100.0F, .boiler_maximum_c = 119.99F};
    ctx.temperature_plant = "acta-temperatura-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    ctx.calibration_id = "abcdefghijklmnopqrstuvwxyz01234";
    ctx.calibration_version = UINT32_MAX;
    ctx.firmware_version = "v9.99.999-999-g0123456789-dirty";
    size_t written = 0U;
    assert(strlen(ctx.temperature_plant) == 63U);
    assert(extraction_message_build(&full, &ctx, buffer, sizeof(buffer), &written) ==
           EXTRACTION_MESSAGE_OK);
    assert(written < BUFFER_BYTES - 1U);
    printf("      con la recta y todo al máximo: %zu bytes sobre %u\n", written,
           (unsigned)(BUFFER_BYTES - 1U));
}

/*
 * El desplazamiento de caldera del primer uso (ADR-009, enmienda del
 * 2026-10-19) viaja con la preparación si consta, y se omite si no: un cero
 * afirmaría que no se compensó. Con todo lo demás en su máximo, sigue cabiendo.
 */
static void test_declares_the_first_use_offset_only_when_known(void)
{
    char buffer[BUFFER_BYTES];
    extraction_summary_t summary = completed_summary();
    extraction_message_context_t ctx = context();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "boilerOffsetAppliedC") == NULL);

    summary.boiler_offset_known = true;
    summary.boiler_offset_c = 0.0F;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"boilerOffsetAppliedC\":0.00") != NULL);

    summary.boiler_offset_c = 3.25F;
    ctx.calibration_id = "abcdefghijklmnopqrstuvwxyz01234";
    ctx.calibration_version = UINT32_MAX;
    ctx.firmware_version = "v9.99.999-999-g0123456789-dirty";
    size_t written = 0U;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), &written) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"boilerOffsetAppliedC\":3.25") != NULL);
    assert(written < BUFFER_BYTES - 1U);
    printf("      con desplazamiento y todo al máximo: %zu bytes sobre %u\n", written,
           (unsigned)(BUFFER_BYTES - 1U));

    /* Un valor imposible no se escribe: no consta. */
    summary.boiler_offset_c = -1.0F;
    ctx = context();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "boilerOffsetAppliedC") == NULL);

    /* La ganancia de la prealimentación (enmienda del 2026-10-27): solo si
     * consta, y cabe junto a todo lo demás al máximo. */
    summary = completed_summary();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "firstUseFeedforwardGainApplied") == NULL);
    summary.boiler_offset_known = true;
    summary.boiler_offset_c = 3.25F;
    summary.feedforward_gain_known = true;
    summary.feedforward_gain = 1.25F;
    ctx.calibration_id = "abcdefghijklmnopqrstuvwxyz01234";
    ctx.calibration_version = UINT32_MAX;
    ctx.firmware_version = "v9.99.999-999-g0123456789-dirty";
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), &written) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"firstUseFeedforwardGainApplied\":1.250") != NULL);
    assert(written < BUFFER_BYTES - 1U);
    summary.feedforward_gain = NAN;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "firstUseFeedforwardGainApplied") == NULL);
}

static extraction_summary_t compensated_summary(void)
{
    extraction_summary_t summary = completed_summary();
    summary.boiler_offset_known = true;
    summary.boiler_offset_c = 3.25F;
    return summary;
}

/*
 * Los indicadores declaran su ventana y dónde se midió la temperatura. Una
 * preparación cuya presión nunca entró en su rango no recibe nota: null, no el
 * cero de la estructura, que se leería como una preparación perfecta.
 */
static void test_states_the_stability_window_and_never_scores_without_one(void)
{
    const extraction_message_context_t ctx = context();
    char buffer[BUFFER_BYTES];

    extraction_summary_t summary = completed_summary();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"temperatureMaeC\":0.482") != NULL);
    assert(strstr(buffer, "\"window\":{\"rule\":\"pressure-first-within-tolerance-v1\""
                          ",\"status\":\"stabilized\",\"startMs\":4200}") != NULL);
    assert(strstr(buffer, "\"temperatureSite\":\"boiler\"") != NULL);

    summary = never_stabilized_summary();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"temperatureMaeC\":null,\"pressureMaeBar\":null") != NULL);
    assert(strstr(buffer, "\"temperatureWithinTolerancePercent\":null") != NULL);
    assert(strstr(buffer, "\"pressureWithinTolerancePercent\":null") != NULL);
    assert(strstr(buffer, "\"status\":\"never-stabilized\",\"startMs\":null") != NULL);
}

static void test_refuses_to_invent_a_date_without_a_trustworthy_clock(void)
{
    const extraction_summary_t summary = completed_summary();
    extraction_message_context_t ctx = context();
    char buffer[BUFFER_BYTES];
    ctx.epoch_ms_at_finish = 0U;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_TIME_BASE_UNAVAILABLE);
    /* un reloj sin sincronizar arranca en 1970 y tampoco se acepta */
    ctx.epoch_ms_at_finish = UINT64_C(86400000);
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_TIME_BASE_UNAVAILABLE);
}

static void test_refuses_an_unfinished_or_untyped_session(void)
{
    extraction_summary_t summary = completed_summary();
    const extraction_message_context_t ctx = context();
    char buffer[BUFFER_BYTES];
    summary.complete = false;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_SESSION_INCOMPLETE);
    summary.complete = true;
    summary.termination = OP_TERMINATION_NONE;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_SESSION_INCOMPLETE);
}

static void test_reports_a_small_buffer_instead_of_truncating(void)
{
    const extraction_summary_t summary = completed_summary();
    const extraction_message_context_t ctx = context();
    char buffer[128];
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_BUFFER_TOO_SMALL);
}

static void test_every_termination_maps_to_a_contract_value(void)
{
    extraction_summary_t summary = completed_summary();
    const extraction_message_context_t ctx = context();
    char buffer[BUFFER_BYTES];
    const struct {
        extraction_termination_t termination;
        const char *expected;
    } cases[] = {
        {OP_TERMINATION_MANUAL_NORMAL_STOP, "\"termination\":\"manual-normal-stop\""},
        {OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED,
         "\"termination\":\"maximum-extraction-time-reached\""},
        {OP_TERMINATION_SAFETY_PROTECTION_TRIP,
         "\"termination\":\"safety-protection-trip\""},
        {OP_TERMINATION_UNEXPECTED_POWER_LOSS,
         "\"termination\":\"unexpected-power-loss\""},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        summary.termination = cases[index].termination;
        assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
               EXTRACTION_MESSAGE_OK);
        assert(strstr(buffer, cases[index].expected) != NULL);
    }
}

/* Lo que deja la recuperación de una marca: cero muestras, sin ventana. */
static extraction_summary_t power_loss_summary(void)
{
    extraction_summary_t summary = {0};
    snprintf(summary.id, sizeof(summary.id), "sim-00000119");
    summary.effective_profile = extraction_simulation_default_profile();
    summary.termination = OP_TERMINATION_UNEXPECTED_POWER_LOSS;
    summary.complete = true;
    return summary;
}

static void test_a_power_loss_record_states_no_end_and_no_duration(void)
{
    extraction_summary_t summary = power_loss_summary();
    extraction_message_context_t ctx = context();
    ctx.epoch_ms_at_start = UINT64_C(1789397990000);  /* 2026-09-14T14:59:50.000Z */
    char buffer[BUFFER_BYTES];
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"termination\":\"unexpected-power-loss\"") != NULL);
    assert(strstr(buffer, "\"startedAt\":\"2026-09-14T14:59:50.000Z\"") != NULL);
    /* El fin y la duración no constan; la hora del rearranque es la del aviso. */
    assert(strstr(buffer, "\"finishedAt\":null") != NULL);
    assert(strstr(buffer, "\"durationMs\":null") != NULL);
    assert(strstr(buffer, "\"occurredAt\":\"2026-09-14T15:00:30.123Z\"") != NULL);
    assert(strstr(buffer, "\"sampleCount\":0") != NULL);
    assert(strstr(buffer, "\"temperatureMaeC\":null") != NULL);
    assert(strstr(buffer, "\"status\":\"undetermined\"") != NULL);

    /* Sin hora al abrir, tampoco se inventa el comienzo. */
    ctx.epoch_ms_at_start = 0U;
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"startedAt\":null") != NULL);

    /* Una preparación normal no cambia: comienzo deducido de su duración. */
    summary = completed_summary();
    ctx = context();
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"startedAt\":\"2026-09-14T15:00:02.623Z\"") != NULL);
    assert(strstr(buffer, "\"durationMs\":27500") != NULL);
}

/* Deja el mensaje en disco para validarlo contra el contrato JSON Schema. */
static void write_sample_for_contract_validation(const char *path,
                                                 extraction_summary_t summary,
                                                 uint64_t epoch_ms_at_start,
                                                 bool with_water_reference)
{
    extraction_message_context_t ctx = context();
    ctx.epoch_ms_at_start = epoch_ms_at_start;
    if (with_water_reference) {
        ctx.temperature_plant = "acta-temperatura-20261028-v11";
        ctx.water_reference = (water_reference_t){
            .defined = true, .gain = 0.8819F, .offset_c = -0.49F,
            .boiler_minimum_c = 97.3F, .boiler_maximum_c = 105.6F};
    }
    char buffer[BUFFER_BYTES];
    assert(extraction_message_build(&summary, &ctx, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    fputs(buffer, file);
    fclose(file);
}

int main(int argc, char **argv)
{
    test_identifier_is_stable_and_derived_from_the_session();
    test_builds_an_envelope_that_fits_the_queue_entry();
    test_declares_the_calibration_and_firmware_it_was_measured_with();
    test_refuses_to_invent_a_date_without_a_trustworthy_clock();
    test_refuses_an_unfinished_or_untyped_session();
    test_reports_a_small_buffer_instead_of_truncating();
    test_every_termination_maps_to_a_contract_value();
    test_states_the_stability_window_and_never_scores_without_one();
    test_a_power_loss_record_states_no_end_and_no_duration();
    test_declares_the_first_use_offset_only_when_known();
    test_declares_the_water_reference_it_converted_with();
    if (argc > 1) {
        write_sample_for_contract_validation(argv[1], completed_summary(), 0U, true);
    }
    if (argc > 2) {
        write_sample_for_contract_validation(argv[2], never_stabilized_summary(), 0U, false);
    }
    /* Cortada por la energía, con hora al abrir y sin ella. */
    if (argc > 3) {
        write_sample_for_contract_validation(argv[3], power_loss_summary(),
                                             UINT64_C(1789397990000), false);
    }
    if (argc > 4) {
        write_sample_for_contract_validation(argv[4], power_loss_summary(), 0U, false);
    }
    /* Con la compensación del primer uso declarada. */
    if (argc > 5) {
        write_sample_for_contract_validation(argv[5], compensated_summary(), 0U, true);
    }
    printf("PASS: serialización de la preparación conforme al contrato v1\n");
    return 0;
}
