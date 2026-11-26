/*
 * Pruebas del registro obligatorio del lazo (ADR-007 § 3.6, ADR-009 § 3.9).
 *
 * Lo que se comprueba aquí no es una implementación concreta, sino las dos
 * propiedades de las que depende que el registro sirva para algo:
 *
 * **Que la cola no mienta.** Conserva el orden, y cuando se llena declara lo
 * que perdió en vez de dejar una laguna silenciosa. Un registro incompleto que
 * parece completo es peor que no tener registro.
 *
 * **Que la cabecera describa la línea.** Cabecera y datos se escriben en
 * lugares distintos del código y ningún compilador las ata. Si se separaran,
 * cada columna quedaría desplazada y todo el análisis posterior leería una
 * magnitud por otra. Aquí se cuentan las columnas de ambas, para cada clase de
 * registro.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "control_log.h"

#define CAPACITY 4U

static uint32_t count_columns(const char *line)
{
    uint32_t columns = 1U;
    for (const char *cursor = line; *cursor != '\0'; cursor++) {
        if (*cursor == ',') {
            columns++;
        }
    }
    return columns;
}

static control_log_record_t make_sample(control_log_loop_t loop, uint32_t sequence)
{
    control_log_record_t record = {0};
    record.kind = CONTROL_LOG_RECORD_SAMPLE;
    record.as.sample.monotonic_us = 1234567890ULL + sequence;
    record.as.sample.sequence = sequence;
    record.as.sample.loop = loop;
    record.as.sample.state = OP_STATE_EXTRACTION;
    record.as.sample.source = PROCESS_SOURCE_MEASURED;
    record.as.sample.quality = SIGNAL_VALID;
    record.as.sample.verdict = ACTUATION_ALLOWED;
    record.as.sample.commissioned = true;
    record.as.sample.output_enabled = true;
    record.as.sample.signal.counts = 12345;
    record.as.sample.signal.volts = 1.2345F;
    record.as.sample.signal.converted = 8.5F;
    record.as.sample.signal.filtered = 8.4F;
    record.as.sample.control.authorized_setpoint = 9.0F;
    record.as.sample.control.internal_setpoint = 8.7F;
    record.as.sample.control.error = 0.3F;
    record.as.sample.control.proportional = 3.0F;
    record.as.sample.control.integral = 12.5F;
    record.as.sample.control.feedforward = 0.0F;
    record.as.sample.control.unlimited = 15.5F;
    record.as.sample.control.output = 15.5F;
    if (loop == CONTROL_LOG_LOOP_TEMPERATURE) {
        record.as.sample.specific.temperature.window_ms = 1000U;
        record.as.sample.specific.temperature.requested_ms = 155U;
        record.as.sample.specific.temperature.applied_ms = 150U;
        record.as.sample.specific.temperature.residue_ms = 5;
        record.as.sample.specific.temperature.rise_rate_c_s = 1.4F;
    } else {
        record.as.sample.specific.pressure.density = 16U;
        record.as.sample.specific.pressure.opportunities = 2400U;
        record.as.sample.specific.pressure.emitted = 384U;
        record.as.sample.specific.pressure.residue = 40U;
        record.as.sample.specific.pressure.enabled_ms = 20000U;
        record.as.sample.specific.pressure.rest_ms = 0U;
    }
    return record;
}

static control_log_record_t make_scale(uint32_t sequence, bool calibrated)
{
    control_log_record_t record = {0};
    record.kind = CONTROL_LOG_RECORD_SCALE;
    record.as.scale.monotonic_us = 777ULL + sequence;
    record.as.scale.sequence = sequence;
    record.as.scale.counts = -123456;
    record.as.scale.quality = SIGNAL_VALID;
    record.as.scale.settled = true;
    record.as.scale.spread_counts = 12U;
    if (calibrated) {
        record.as.scale.grams = 36.25F;
        record.as.scale.mass_available = true;
        snprintf(record.as.scale.calibration_id,
                 sizeof(record.as.scale.calibration_id), "cal-001");
    }
    return record;
}

static control_log_record_t make_event(control_log_event_kind_t kind, uint32_t detail)
{
    control_log_record_t record = {0};
    record.kind = CONTROL_LOG_RECORD_EVENT;
    record.as.event.monotonic_us = 99ULL;
    record.as.event.kind = kind;
    record.as.event.loop = CONTROL_LOG_LOOP_PRESSURE;
    record.as.event.detail = detail;
    return record;
}

/* La cola entrega en el orden en que recibió, incluso al dar la vuelta. */
static void test_the_queue_preserves_order_across_the_wrap(void)
{
    control_log_record_t storage[CAPACITY];
    control_log_ring_t ring = {0};
    assert(control_log_ring_init(&ring, storage, CAPACITY));

    /* Diez vueltas completas sobre una cola de cuatro: los índices dan la vuelta
     * varias veces y el orden debe sobrevivir a cada una. */
    for (uint32_t round = 0U; round < 10U; round++) {
        control_log_record_t pushed = make_sample(CONTROL_LOG_LOOP_PRESSURE, round);
        control_log_record_t popped = {0};
        assert(control_log_ring_push(&ring, &pushed));
        assert(control_log_ring_pop(&ring, &popped));
        assert(popped.as.sample.sequence == round);
    }

    control_log_ring_t fresh = {0};
    assert(control_log_ring_init(&fresh, storage, CAPACITY));
    for (uint32_t index = 0U; index < CAPACITY; index++) {
        control_log_record_t record = make_sample(CONTROL_LOG_LOOP_PRESSURE, index);
        assert(control_log_ring_push(&fresh, &record));
    }
    assert(control_log_ring_pending(&fresh) == CAPACITY);
    for (uint32_t index = 0U; index < CAPACITY; index++) {
        control_log_record_t record = {0};
        assert(control_log_ring_pop(&fresh, &record));
        assert(record.as.sample.sequence == index);
    }
    assert(control_log_ring_pending(&fresh) == 0U);
}

/*
 * Al llenarse se descarta lo nuevo, no lo antiguo.
 *
 * Es la decisión que hace estable lo ya encolado: el productor deja de escribir
 * en vez de disputarle al consumidor la posición que está leyendo.
 */
static void test_a_full_queue_discards_the_new_record(void)
{
    control_log_record_t storage[CAPACITY];
    control_log_ring_t ring = {0};
    assert(control_log_ring_init(&ring, storage, CAPACITY));

    for (uint32_t index = 0U; index < CAPACITY; index++) {
        control_log_record_t record = make_sample(CONTROL_LOG_LOOP_PRESSURE, index);
        assert(control_log_ring_push(&ring, &record));
    }
    control_log_record_t rejected = make_sample(CONTROL_LOG_LOOP_PRESSURE, 99U);
    assert(!control_log_ring_push(&ring, &rejected));
    assert(!control_log_ring_push(&ring, &rejected));

    assert(control_log_ring_accepted(&ring) == CAPACITY);
    assert(control_log_ring_lost_total(&ring) == 2U);

    control_log_record_t first = {0};
    assert(control_log_ring_pop(&ring, &first));
    assert(first.as.sample.sequence == 0U);  /* el más antiguo sigue ahí */
}

/* El aviso de pérdida se emite una vez por racha, no en cada vuelta. */
static void test_the_loss_counter_is_consumed_once(void)
{
    control_log_record_t storage[1];
    control_log_ring_t ring = {0};
    assert(control_log_ring_init(&ring, storage, 1U));

    control_log_record_t record = make_sample(CONTROL_LOG_LOOP_PRESSURE, 0U);
    assert(control_log_ring_push(&ring, &record));
    assert(!control_log_ring_push(&ring, &record));
    assert(!control_log_ring_push(&ring, &record));
    assert(!control_log_ring_push(&ring, &record));

    assert(control_log_ring_take_lost(&ring) == 3U);
    assert(control_log_ring_take_lost(&ring) == 0U);
    /* El total acumulado no se borra: es la trazabilidad de la prueba. */
    assert(control_log_ring_lost_total(&ring) == 3U);
}

static void test_the_queue_rejects_unusable_arguments(void)
{
    control_log_record_t storage[CAPACITY];
    control_log_ring_t ring = {0};
    assert(!control_log_ring_init(NULL, storage, CAPACITY));
    assert(!control_log_ring_init(&ring, NULL, CAPACITY));
    assert(!control_log_ring_init(&ring, storage, 0U));

    control_log_record_t record = {0};
    assert(!control_log_ring_push(NULL, &record));
    assert(!control_log_ring_pop(NULL, &record));
    assert(control_log_ring_pending(NULL) == 0U);
}

/* Cada cabecera describe exactamente las columnas que escribe su línea. */
static void test_every_header_matches_its_line(void)
{
    char line[CONTROL_LOG_LINE_BYTES];

    control_log_record_t pressure = make_sample(CONTROL_LOG_LOOP_PRESSURE, 1U);
    assert(control_log_format(&pressure, line, sizeof(line)) > 0);
    assert(count_columns(line) == count_columns(control_log_header_pressure()));

    control_log_record_t temperature = make_sample(CONTROL_LOG_LOOP_TEMPERATURE, 2U);
    assert(control_log_format(&temperature, line, sizeof(line)) > 0);
    assert(count_columns(line) == count_columns(control_log_header_temperature()));
    /* Sin fase declarada se escribe «-», no un hueco que desplace la columna. */
    assert(strstr(line, ",-\n") != NULL);
    temperature.as.sample.specific.temperature.fast_start = "boost";
    assert(control_log_format(&temperature, line, sizeof(line)) > 0);
    assert(count_columns(line) == count_columns(control_log_header_temperature()));
    assert(strstr(line, ",boost\n") != NULL);

    control_log_record_t event = make_event(CONTROL_LOG_EVENT_SESSION_CLOSED,
                                            OP_TERMINATION_SAFETY_PROTECTION_TRIP);
    assert(control_log_format(&event, line, sizeof(line)) > 0);
    assert(count_columns(line) == count_columns(control_log_header_event()));

    /* La línea de báscula conserva sus columnas con y sin calibración: la falta
     * de gramos se marca dentro de la columna, nunca omitiéndola. */
    control_log_record_t calibrated = make_scale(1U, true);
    assert(control_log_format(&calibrated, line, sizeof(line)) > 0);
    assert(count_columns(line) == count_columns(control_log_header_scale()));

    control_log_record_t uncalibrated = make_scale(2U, false);
    assert(control_log_format(&uncalibrated, line, sizeof(line)) > 0);
    assert(count_columns(line) == count_columns(control_log_header_scale()));
}

/*
 * Una báscula sin calibrar no publica gramos.
 *
 * Es el invariante que más fácil sería romper al añadir esta columna: un cero
 * tiene forma de medida, y quien promediara la columna obtendría una masa falsa
 * sin enterarse. Se comprueba que la marca está y que ningún cero ocupa su
 * lugar, y que las cuentas viajan siempre, que es lo que permite medir el factor
 * después y aplicarlo sobre capturas ya tomadas.
 */
static void test_an_uncalibrated_scale_reports_no_grams(void)
{
    char line[CONTROL_LOG_LINE_BYTES];

    control_log_record_t uncalibrated = make_scale(7U, false);
    assert(control_log_format(&uncalibrated, line, sizeof(line)) > 0);
    assert(strstr(line, ",-,0,") != NULL);
    assert(strstr(line, "0.0000") == NULL);
    assert(strstr(line, ",-\n") != NULL);
    assert(strstr(line, "-123456") != NULL);

    control_log_record_t calibrated = make_scale(8U, true);
    assert(control_log_format(&calibrated, line, sizeof(line)) > 0);
    assert(strstr(line, "36.2500") != NULL);
    assert(strstr(line, "cal-001") != NULL);
}

/* Cada clase de línea se reconoce por su primera columna, sin ambigüedad. */
static void test_each_line_declares_its_class(void)
{
    char line[CONTROL_LOG_LINE_BYTES];

    control_log_record_t pressure = make_sample(CONTROL_LOG_LOOP_PRESSURE, 1U);
    assert(control_log_format(&pressure, line, sizeof(line)) > 0);
    assert(strncmp(line, "P,", 2) == 0);
    assert(line[strlen(line) - 1U] == '\n');

    control_log_record_t temperature = make_sample(CONTROL_LOG_LOOP_TEMPERATURE, 1U);
    assert(control_log_format(&temperature, line, sizeof(line)) > 0);
    assert(strncmp(line, "T,", 2) == 0);

    control_log_record_t event = make_event(CONTROL_LOG_EVENT_RECORDS_LOST, 42U);
    assert(control_log_format(&event, line, sizeof(line)) > 0);
    assert(strncmp(line, "E,", 2) == 0);

    control_log_record_t scale = make_scale(1U, true);
    assert(control_log_format(&scale, line, sizeof(line)) > 0);
    assert(strncmp(line, "S,", 2) == 0);

    assert(strncmp(control_log_header_pressure(), "#P,", 3) == 0);
    assert(strncmp(control_log_header_temperature(), "#T,", 3) == 0);
    assert(strncmp(control_log_header_event(), "#E,", 3) == 0);
    assert(strncmp(control_log_header_scale(), "#S,", 3) == 0);
}

/*
 * Los enumerados viajan por su nombre.
 *
 * Un registro que dijera `verdict=7` obligaría a tener delante la versión
 * exacta del firmware que lo produjo para poder leerlo.
 */
static void test_enumerations_travel_as_names(void)
{
    char line[CONTROL_LOG_LINE_BYTES];
    control_log_record_t sample = make_sample(CONTROL_LOG_LOOP_PRESSURE, 1U);
    sample.as.sample.state = OP_STATE_MANUAL_PURGE;
    sample.as.sample.quality = SIGNAL_OUT_OF_RANGE;
    sample.as.sample.verdict = ACTUATION_INHIBITED_ZERO_CROSS;
    sample.as.sample.source = PROCESS_SOURCE_SIMULATED;
    assert(control_log_format(&sample, line, sizeof(line)) > 0);
    assert(strstr(line, operational_state_name(OP_STATE_MANUAL_PURGE)) != NULL);
    assert(strstr(line, "out-of-range") != NULL);
    assert(strstr(line, "zero-cross-lost") != NULL);
    assert(strstr(line, "simulated") != NULL);

    control_log_record_t closed = make_event(CONTROL_LOG_EVENT_SESSION_CLOSED,
                                             OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED);
    assert(control_log_format(&closed, line, sizeof(line)) > 0);
    assert(strstr(line, "session-closed") != NULL);
    assert(strstr(line,
                  extraction_termination_name(
                      OP_TERMINATION_MAXIMUM_EXTRACTION_TIME_REACHED)) != NULL);

    /* Un detalle que es una cuenta y no un código no recibe nombre inventado. */
    control_log_record_t lost = make_event(CONTROL_LOG_EVENT_RECORDS_LOST, 7U);
    assert(control_log_format(&lost, line, sizeof(line)) > 0);
    assert(strstr(line, ",7,-\n") != NULL);

    /*
     * El modo restringido deja en el registro qué secuencia se aplicó y por qué
     * se interrumpió, y las líneas de muestra dicen bajo qué veredicto se
     * tomaron. Es lo que impide archivar una serie de identificación como si
     * fuera de operación.
     */
    control_log_record_t started = make_event(CONTROL_LOG_EVENT_STIMULUS_STARTED,
                                              STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1);
    assert(control_log_format(&started, line, sizeof(line)) > 0);
    assert(strstr(line, "stimulus-started") != NULL);
    assert(strstr(line, "pump-step-2pct-v1") != NULL);
    assert(count_columns(line) == count_columns(control_log_header_event()));

    control_log_record_t finished = make_event(CONTROL_LOG_EVENT_STIMULUS_FINISHED,
                                               STIMULUS_SEQUENCE_HEATER_STEP_1PCT_V1);
    assert(control_log_format(&finished, line, sizeof(line)) > 0);
    assert(strstr(line, "stimulus-finished") != NULL);
    assert(strstr(line, "heater-step-1pct-v1") != NULL);

    control_log_record_t aborted = make_event(CONTROL_LOG_EVENT_STIMULUS_ABORTED,
                                              ACTUATION_INHIBITED_HARD_LIMIT);
    assert(control_log_format(&aborted, line, sizeof(line)) > 0);
    assert(strstr(line, "stimulus-aborted") != NULL);
    assert(strstr(line, "hard-limit") != NULL);

    /* La sintonía viaja en cada línea; sin nombre se marca, no se deja vacía. */
    assert(control_log_format(&sample, line, sizeof(line)) > 0);
    assert(strstr(line, ",-,") != NULL);
    snprintf(sample.as.sample.tuning, sizeof(sample.as.sample.tuning), "acta-presion-v2");
    assert(control_log_format(&sample, line, sizeof(line)) > 0);
    assert(strstr(line, ",acta-presion-v2,") != NULL);
    assert(count_columns(line) == count_columns(control_log_header_pressure()));

    sample.as.sample.verdict = ACTUATION_ALLOWED_IDENTIFICATION;
    assert(control_log_format(&sample, line, sizeof(line)) > 0);
    assert(strstr(line, ",allowed-identification,") != NULL);
}

/* Las tres señales de la cadena de medida aparecen por separado. */
static void test_the_measurement_chain_is_not_collapsed(void)
{
    char line[CONTROL_LOG_LINE_BYTES];
    control_log_record_t sample = make_sample(CONTROL_LOG_LOOP_PRESSURE, 1U);
    sample.as.sample.signal.counts = -4096;
    sample.as.sample.signal.volts = 0.5000F;
    sample.as.sample.signal.converted = 2.0000F;
    sample.as.sample.signal.filtered = 1.5000F;
    assert(control_log_format(&sample, line, sizeof(line)) > 0);
    assert(strstr(line, "-4096,0.5000,2.0000,1.5000") != NULL);
}

/* Un destino corto no produce media línea: produce un fallo. */
static void test_a_short_buffer_never_yields_a_truncated_line(void)
{
    char line[32];
    control_log_record_t sample = make_sample(CONTROL_LOG_LOOP_PRESSURE, 1U);
    assert(control_log_format(&sample, line, sizeof(line)) < 0);
    assert(line[0] == '\0');

    char nothing[1];
    assert(control_log_format(&sample, nothing, 0U) < 0);
    assert(control_log_format(NULL, line, sizeof(line)) < 0);
    assert(control_log_format(&sample, NULL, sizeof(line)) < 0);
    (void)nothing;
}

/*
 * Un valor no finito se escribe, no se oculta.
 *
 * Si el lazo llega a producir un `nan`, el registro es el único sitio donde eso
 * puede verse. Sustituirlo por un cero lo haría indistinguible de una medida
 * legítima, y la línea debe seguir teniendo todas sus columnas.
 */
static void test_non_finite_values_keep_the_line_readable(void)
{
    char line[CONTROL_LOG_LINE_BYTES];
    control_log_record_t sample = make_sample(CONTROL_LOG_LOOP_PRESSURE, 1U);
    sample.as.sample.signal.filtered = NAN;
    sample.as.sample.control.integral = INFINITY;
    assert(control_log_format(&sample, line, sizeof(line)) > 0);
    assert(count_columns(line) == count_columns(control_log_header_pressure()));
    assert(strstr(line, "nan") != NULL || strstr(line, "-nan") != NULL);
    assert(strstr(line, "inf") != NULL);
}

int main(void)
{
    test_the_queue_preserves_order_across_the_wrap();
    test_a_full_queue_discards_the_new_record();
    test_the_loss_counter_is_consumed_once();
    test_the_queue_rejects_unusable_arguments();
    test_every_header_matches_its_line();
    test_each_line_declares_its_class();
    test_enumerations_travel_as_names();
    test_the_measurement_chain_is_not_collapsed();
    test_a_short_buffer_never_yields_a_truncated_line();
    test_non_finite_values_keep_the_line_readable();
    test_an_uncalibrated_scale_reports_no_grams();
    printf("test_control_log: 11 pruebas correctas\n");
    return 0;
}
