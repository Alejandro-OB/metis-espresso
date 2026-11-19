#include "control_log.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * Columnas comunes a las dos clases de muestra, en el orden en que se escriben.
 *
 * La cabecera y la línea de datos se construyen en dos sitios distintos y
 * podrían separarse sin que el compilador dijera nada. Lo que lo impide es una
 * prueba del anfitrión que compara el número de columnas de cada cabecera con
 * el de su línea correspondiente, para cada clase de registro.
 */
#define CONTROL_LOG_COMMON_COLUMNS                                              \
    "t_us,seq,state,source,quality,verdict,commissioned,tuning,output_enabled," \
    "counts,volts,converted,filtered,"                                          \
    "setpoint_auth,setpoint_int,error,p_term,i_term,ff,u_unlimited,u,"          \
    "sat_hi,sat_lo,i_held"

typedef struct {
    char *buffer;
    size_t size;
    size_t used;
    bool overflow;
} line_writer_t;

static void append(line_writer_t *writer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void append(line_writer_t *writer, const char *format, ...)
{
    if (writer->overflow) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    const int written = vsnprintf(writer->buffer + writer->used,
                                  writer->size - writer->used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= writer->size - writer->used) {
        writer->overflow = true;
        return;
    }
    writer->used += (size_t)written;
}

/*
 * Nombre del origen de la magnitud.
 *
 * `process_source_name` existe ya, pero vive en un módulo que alcanza la tarea
 * de adquisición y por tanto no se enlaza en el anfitrión. Repetir aquí las
 * tres cadenas es preferible a arrastrar ESP-IDF dentro de una prueba de
 * formato; son tres valores y el enumerado está cerrado.
 */
static const char *source_name(process_source_t source)
{
    switch (source) {
    case PROCESS_SOURCE_MEASURED:
        return "measured";
    case PROCESS_SOURCE_SIMULATED:
        return "simulated";
    case PROCESS_SOURCE_NONE:
    default:
        return "none";
    }
}

const char *control_log_loop_name(control_log_loop_t loop)
{
    return loop == CONTROL_LOG_LOOP_TEMPERATURE ? "temperature" : "pressure";
}

const char *control_log_event_name(control_log_event_kind_t kind)
{
    switch (kind) {
    case CONTROL_LOG_EVENT_SESSION_OPENED:
        return "session-opened";
    case CONTROL_LOG_EVENT_SESSION_CLOSED:
        return "session-closed";
    case CONTROL_LOG_EVENT_LOOP_ENABLED:
        return "loop-enabled";
    case CONTROL_LOG_EVENT_LOOP_INHIBITED:
        return "loop-inhibited";
    case CONTROL_LOG_EVENT_RECORDS_LOST:
        return "records-lost";
    case CONTROL_LOG_EVENT_STIMULUS_STARTED:
        return "stimulus-started";
    case CONTROL_LOG_EVENT_STIMULUS_FINISHED:
        return "stimulus-finished";
    case CONTROL_LOG_EVENT_STIMULUS_ABORTED:
        return "stimulus-aborted";
    case CONTROL_LOG_EVENT_NONE:
    default:
        return "none";
    }
}

bool control_log_ring_init(control_log_ring_t *ring,
                           control_log_record_t *storage,
                           uint32_t capacity)
{
    if (ring == NULL || storage == NULL || capacity == 0U) {
        return false;
    }
    memset(ring, 0, sizeof(*ring));
    ring->storage = storage;
    ring->capacity = capacity;
    return true;
}

bool control_log_ring_push(control_log_ring_t *ring, const control_log_record_t *record)
{
    if (ring == NULL || ring->storage == NULL || record == NULL) {
        return false;
    }
    if (ring->pending >= ring->capacity) {
        /* Contrapresión declarada: lo nuevo se pierde y queda contado, para que
         * el drenaje lo anuncie. Lo ya encolado no se toca. */
        if (ring->lost < UINT32_MAX) {
            ring->lost++;
        }
        if (ring->lost_total < UINT32_MAX) {
            ring->lost_total++;
        }
        return false;
    }
    ring->storage[ring->head] = *record;
    ring->head = (ring->head + 1U) % ring->capacity;
    ring->pending++;
    if (ring->accepted < UINT32_MAX) {
        ring->accepted++;
    }
    return true;
}

bool control_log_ring_pop(control_log_ring_t *ring, control_log_record_t *record)
{
    if (ring == NULL || ring->storage == NULL || record == NULL || ring->pending == 0U) {
        return false;
    }
    *record = ring->storage[ring->tail];
    ring->tail = (ring->tail + 1U) % ring->capacity;
    ring->pending--;
    return true;
}

uint32_t control_log_ring_pending(const control_log_ring_t *ring)
{
    return ring == NULL ? 0U : ring->pending;
}

uint32_t control_log_ring_accepted(const control_log_ring_t *ring)
{
    return ring == NULL ? 0U : ring->accepted;
}

uint32_t control_log_ring_lost_total(const control_log_ring_t *ring)
{
    return ring == NULL ? 0U : ring->lost_total;
}

uint32_t control_log_ring_take_lost(control_log_ring_t *ring)
{
    if (ring == NULL) {
        return 0U;
    }
    const uint32_t lost = ring->lost;
    ring->lost = 0U;
    return lost;
}

const char *control_log_header_pressure(void)
{
    return "#P," CONTROL_LOG_COMMON_COLUMNS
           ",density,opportunities,emitted,residue,enabled_ms,rest_ms\n";
}

const char *control_log_header_temperature(void)
{
    return "#T," CONTROL_LOG_COMMON_COLUMNS
           ",window_ms,requested_ms,applied_ms,residue_ms,rise_c_s,fast_start\n";
}

const char *control_log_header_event(void)
{
    return "#E,t_us,loop,event,detail,detail_name\n";
}

const char *control_log_header_scale(void)
{
    return "#S,t_us,sequence,quality,counts,grams,mass_available,settled,"
           "spread_counts,calibration\n";
}

static void append_common(line_writer_t *writer, const control_log_sample_t *sample)
{
    /* Una sintonía sin nombre se marca, no se deja vacía: una columna vacía
     * en un CSV se confunde con un campo perdido. */
    append(writer, "%" PRIu64 ",%" PRIu32 ",%s,%s,%s,%s,%d,%s,%d,",
           sample->monotonic_us, sample->sequence,
           operational_state_name(sample->state), source_name(sample->source),
           signal_quality_name(sample->quality), actuation_verdict_name(sample->verdict),
           sample->commissioned ? 1 : 0, sample->tuning[0] != '\0' ? sample->tuning : "-",
           sample->output_enabled ? 1 : 0);
    append(writer, "%d,%.4f,%.4f,%.4f,", (int)sample->signal.counts,
           (double)sample->signal.volts, (double)sample->signal.converted,
           (double)sample->signal.filtered);
    append(writer, "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,",
           (double)sample->control.authorized_setpoint,
           (double)sample->control.internal_setpoint, (double)sample->control.error,
           (double)sample->control.proportional, (double)sample->control.integral,
           (double)sample->control.feedforward, (double)sample->control.unlimited,
           (double)sample->control.output);
    append(writer, "%d,%d,%d", sample->control.saturated_high ? 1 : 0,
           sample->control.saturated_low ? 1 : 0,
           sample->control.integral_held ? 1 : 0);
}

/*
 * Nombre del detalle de un evento.
 *
 * El detalle es un entero cuyo significado depende del suceso: una causa de
 * terminación, un veredicto del guardián o una cuenta. Se escribe tal cual en
 * su columna y además traducido, para que la línea se lea sin consultar ningún
 * enumerado. Cuando el detalle es un número y no un código, la traducción es
 * `-`: inventarle un nombre sería peor que no darlo.
 */
static const char *event_detail_name(const control_log_event_t *event)
{
    switch (event->kind) {
    case CONTROL_LOG_EVENT_SESSION_CLOSED:
        return extraction_termination_name((extraction_termination_t)event->detail);
    case CONTROL_LOG_EVENT_LOOP_INHIBITED:
    case CONTROL_LOG_EVENT_STIMULUS_ABORTED:
        return actuation_verdict_name((actuation_verdict_t)event->detail);
    case CONTROL_LOG_EVENT_STIMULUS_STARTED:
    case CONTROL_LOG_EVENT_STIMULUS_FINISHED:
        return stimulus_sequence_name((stimulus_sequence_id_t)event->detail);
    default:
        return "-";
    }
}

int control_log_format(const control_log_record_t *record, char *buffer, size_t size)
{
    if (record == NULL || buffer == NULL || size == 0U) {
        return -1;
    }
    line_writer_t writer = {.buffer = buffer, .size = size, .used = 0U, .overflow = false};
    buffer[0] = '\0';

    if (record->kind == CONTROL_LOG_RECORD_SCALE) {
        const control_log_scale_t *scale = &record->as.scale;
        append(&writer, "S,%" PRIu64 ",%" PRIu32 ",%s,%" PRId32 ",", scale->monotonic_us,
               scale->sequence, signal_quality_name(scale->quality), scale->counts);
        /*
         * Sin factor de calibración medido la columna de gramos escribe `-`.
         *
         * No es un hueco por descuido: un cero tiene forma de medida y un lector
         * que promediara la columna obtendría una masa falsa sin enterarse. La
         * marca obliga a mirar `mass_available`, que es la única que dice si hay
         * gramos. Las cuentas quedan siempre, de modo que el factor se puede
         * medir después y aplicarse sobre capturas ya tomadas.
         */
        if (scale->mass_available) {
            append(&writer, "%.4f,", (double)scale->grams);
        } else {
            append(&writer, "-,");
        }
        append(&writer, "%d,%d,%" PRIu32 ",%s\n", scale->mass_available ? 1 : 0,
               scale->settled ? 1 : 0, scale->spread_counts,
               scale->calibration_id[0] != '\0' ? scale->calibration_id : "-");
    } else if (record->kind == CONTROL_LOG_RECORD_EVENT) {
        const control_log_event_t *event = &record->as.event;
        append(&writer, "E,%" PRIu64 ",%s,%s,%" PRIu32 ",%s\n", event->monotonic_us,
               control_log_loop_name(event->loop), control_log_event_name(event->kind),
               event->detail, event_detail_name(event));
    } else {
        const control_log_sample_t *sample = &record->as.sample;
        append(&writer, "%s,", sample->loop == CONTROL_LOG_LOOP_TEMPERATURE ? "T" : "P");
        append_common(&writer, sample);
        if (sample->loop == CONTROL_LOG_LOOP_TEMPERATURE) {
            const control_log_temperature_t *specific = &sample->specific.temperature;
            append(&writer, ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRId32 ",%.4f,%s\n",
                   specific->window_ms, specific->requested_ms, specific->applied_ms,
                   specific->residue_ms, (double)specific->rise_rate_c_s,
                   specific->fast_start != NULL ? specific->fast_start : "-");
        } else {
            const control_log_pressure_t *specific = &sample->specific.pressure;
            append(&writer, ",%u,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n",
                   (unsigned)specific->density, specific->opportunities, specific->emitted,
                   specific->residue, specific->enabled_ms, specific->rest_ms);
        }
    }

    if (writer.overflow) {
        /* Una línea truncada corrompe la columna, no solo el valor: se descarta
         * entera y el llamante decide. */
        buffer[0] = '\0';
        return -1;
    }
    return (int)writer.used;
}
