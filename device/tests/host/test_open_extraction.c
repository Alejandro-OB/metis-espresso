/*
 * Marca de preparación abierta (ADR-005 § 3, RNF-006).
 *
 * La propiedad que importa: una preparación cortada a la mitad reaparece al
 * arrancar como registro cerrado por `unexpected-power-loss`, con lo que había
 * al abrirla y sin un final que nadie midió. Y una marca dudosa no produce un
 * registro.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "extraction_message.h"
#include "open_extraction.h"

static open_extraction_opening_t opening(const extraction_profile_t *profile)
{
    return (open_extraction_opening_t){
        .session_id = "sim-00000119",
        .profile = profile,
        .started_epoch_ms = UINT64_C(1789397990000),
        .source = OPEN_EXTRACTION_SOURCE_MEASURED,
        .calibration_id = "ntc-2026-10-09",
        .calibration_version = 4U,
        .firmware_version = "acta-presion-20261012-2-gde4876",
    };
}

static void test_zero_is_no_marker(void)
{
    const open_extraction_marker_t marker = {0};
    extraction_summary_t summary;
    assert(!open_extraction_is_valid(&marker));
    assert(!open_extraction_recover(&marker, &summary));
    assert(summary.termination == OP_TERMINATION_NONE && !summary.complete);
    assert(!open_extraction_is_valid(NULL));
}

static void test_recovers_what_there_was_at_the_opening(void)
{
    const extraction_profile_t profile = extraction_simulation_default_profile();
    const open_extraction_opening_t open = opening(&profile);
    open_extraction_marker_t marker;
    assert(open_extraction_mark(&marker, &open));
    assert(open_extraction_is_valid(&marker));
    assert(strcmp(marker.calibration_id, "ntc-2026-10-09") == 0);
    assert(marker.source == OPEN_EXTRACTION_SOURCE_MEASURED);

    extraction_summary_t summary;
    assert(open_extraction_recover(&marker, &summary));
    assert(strcmp(summary.id, "sim-00000119") == 0);
    assert(strcmp(summary.effective_profile.id, profile.id) == 0);
    assert(summary.termination == OP_TERMINATION_UNEXPECTED_POWER_LOSS);
    assert(summary.complete);
    /* Ni muestras ni indicadores: vivían en RAM. */
    assert(summary.sample_count == 0U && summary.duration_ms == 0U);
    assert(summary.window_status == EXTRACTION_WINDOW_UNDETERMINED);
    assert(summary.temperature_samples_in_window == 0U);

    /* Y el mensaje que sale de él no tiene fin ni duración. */
    const extraction_message_context_t context = {
        .device_id = "esp32s3-prototype-01",
        .measured = true,
        .sequence = 1U,
        .epoch_ms_at_finish = UINT64_C(1789398030123),
        .epoch_ms_at_start = marker.started_epoch_ms,
        .calibration_id = marker.calibration_id,
        .calibration_version = marker.calibration_version,
        .firmware_version = marker.firmware_version,
    };
    char buffer[1537];
    assert(extraction_message_build(&summary, &context, buffer, sizeof(buffer), NULL) ==
           EXTRACTION_MESSAGE_OK);
    assert(strstr(buffer, "\"durationMs\":null") != NULL);
    assert(strstr(buffer, "\"finishedAt\":null") != NULL);
}

static void test_refuses_to_mark_without_identity_or_with_truncated_text(void)
{
    const extraction_profile_t profile = extraction_simulation_default_profile();
    open_extraction_marker_t marker;
    open_extraction_opening_t open = opening(&profile);
    open.session_id = "";
    assert(!open_extraction_mark(&marker, &open));
    assert(marker.format_version == 0U);
    open = opening(&profile);
    open.profile = NULL;
    assert(!open_extraction_mark(&marker, &open));
    /* Una versión de firmware que no cabe no se recorta: sería otra. */
    open = opening(&profile);
    open.firmware_version = "una-version-de-firmware-demasiado-larga-para-su-campo";
    assert(!open_extraction_mark(&marker, &open));
    assert(!open_extraction_is_valid(&marker));
    /* Lo que no consta queda vacío, no inventado. */
    open = opening(&profile);
    open.calibration_id = NULL;
    open.firmware_version = NULL;
    assert(open_extraction_mark(&marker, &open));
    assert(marker.calibration_id[0] == '\0' && marker.firmware_version[0] == '\0');
}

static void test_a_doubtful_marker_read_back_is_discarded(void)
{
    const extraction_profile_t profile = extraction_simulation_default_profile();
    const open_extraction_opening_t open = opening(&profile);
    open_extraction_marker_t marker;
    assert(open_extraction_mark(&marker, &open));

    open_extraction_marker_t other_format = marker;
    other_format.format_version = OPEN_EXTRACTION_FORMAT_VERSION + 1U;
    assert(!open_extraction_is_valid(&other_format));

    open_extraction_marker_t unterminated = marker;
    memset(unterminated.session_id, 'x', sizeof(unterminated.session_id));
    assert(!open_extraction_is_valid(&unterminated));

    open_extraction_marker_t bad_source = marker;
    bad_source.source = (open_extraction_source_t)7;
    assert(!open_extraction_is_valid(&bad_source));

    extraction_summary_t summary;
    assert(!open_extraction_recover(&unterminated, &summary));
}

int main(void)
{
    test_zero_is_no_marker();
    test_recovers_what_there_was_at_the_opening();
    test_refuses_to_mark_without_identity_or_with_truncated_text();
    test_a_doubtful_marker_read_back_is_discarded();
    printf("PASS: marca de preparación abierta\n");
    return 0;
}
