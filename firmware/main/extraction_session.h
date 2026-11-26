#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "extraction_profile.h"
#include "operational_fsm.h"
#include "pre_start_history.h"

#define EXTRACTION_SESSION_ID_LENGTH 48U
#define EXTRACTION_MAX_SAMPLES 1200U

/*
 * Tramo sobre el que se calculan los indicadores de estabilidad (RF-019,
 * RNF-005). Empieza cuando la presión entra por primera vez en su tolerancia y
 * llega hasta el final: la subida inicial es una desviación esperada y contarla
 * castigaba a una máquina que no había fallado. Se descartó ignorar un número
 * fijo de segundos porque esa cifra no sale de ninguna medida. Si la presión
 * nunca entra, no hay indicadores: un promedio sobre una presión que nunca
 * llegó describe el fallo, no la estabilidad.
 *
 * La misma regla está en `packages/espresso_stability`, y las pruebas de los dos
 * lados repiten los mismos casos.
 */
#define EXTRACTION_STABILITY_WINDOW_RULE "pressure-first-within-tolerance-v1"

typedef enum {
    /* El cero es el estado conservador: nadie ha determinado la ventana. */
    EXTRACTION_WINDOW_UNDETERMINED = 0,
    EXTRACTION_WINDOW_STABILIZED,
    EXTRACTION_WINDOW_NEVER_STABILIZED,
} extraction_window_status_t;

typedef struct {
    uint32_t elapsed_ms;
    float observed_temperature_c;
    float observed_pressure_bar;
    float target_temperature_c;
    float target_pressure_bar;
    float simulated_pump_duty;
    bool temperature_valid;
    bool pressure_valid;
    bool simulated_heater_on;
} extraction_sample_t;

typedef struct {
    char id[EXTRACTION_SESSION_ID_LENGTH];
    extraction_profile_t effective_profile;
    uint64_t started_monotonic_ms;
    uint64_t finished_monotonic_ms;
    uint32_t duration_ms;
    extraction_termination_t termination;
    uint32_t sample_count;
    uint32_t dropped_samples;
    float mean_absolute_temperature_error_c;
    float mean_absolute_pressure_error_bar;
    float temperature_within_tolerance_percent;
    float pressure_within_tolerance_percent;
    /* Los indicadores de arriba solo significan algo si su cuenta no es cero:
     * con cero muestras en la ventana valen 0 y el mensaje los envía como null. */
    extraction_window_status_t window_status;
    uint32_t window_start_ms;
    uint32_t temperature_samples_in_window;
    uint32_t pressure_samples_in_window;
    /*
     * Desplazamiento de la referencia de caldera con que se preparó
     * (compensación del primer uso, ADR-009, enmienda del 2026-10-19). Sin
     * constancia —una preparación recuperada tras un corte, o abierta sin
     * estado del supervisor— el mensaje lo omite: un cero afirmaría que no se
     * compensó, y eso nadie lo comprobó.
     */
    bool boiler_offset_known;
    float boiler_offset_c;
    /*
     * Ganancia de la prealimentación con que se preparó: la del acta o la
     * reforzada del primer uso (ADR-009, enmienda del 2026-10-27). Misma regla
     * que el desplazamiento: sin constancia, el mensaje la omite.
     */
    bool feedforward_gain_known;
    float feedforward_gain;
    bool complete;
} extraction_summary_t;

typedef struct {
    extraction_summary_t summary;
    extraction_sample_t samples[EXTRACTION_MAX_SAMPLES];
    /*
     * El minuto previo a pulsar, con instantes negativos (enmienda del
     * 2026-10-19 a ADR-012 § 4.1). Va aparte de `samples` y no cuenta en
     * `summary.sample_count` ni en los indicadores: es la máquina lista antes
     * de la preparación, no la preparación, y la ventana de estabilidad de
     * RF-019 no debe verla. Se copia aquí, y no se lee del anillo al cerrar,
     * para que la evidencia de una preparación no dependa del estado vivo de
     * otra fase en el momento de serializarla.
     */
    pre_start_sample_t pre_start[PRE_START_HISTORY_CAPACITY];
    uint32_t pre_start_count;
    double accumulated_temperature_error;
    double accumulated_pressure_error;
    uint32_t valid_temperature_samples;
    uint32_t valid_pressure_samples;
    uint32_t temperature_samples_within_tolerance;
    uint32_t pressure_samples_within_tolerance;
    bool window_open;
    bool active;
} extraction_session_t;

bool extraction_session_start(extraction_session_t *session,
                              const char *session_id,
                              const extraction_profile_t *profile,
                              uint64_t started_monotonic_ms);
bool extraction_session_record(extraction_session_t *session,
                               const extraction_sample_t *sample);
/*
 * Toma del anillo el minuto previo a pulsar. El instante de pulsar es el de
 * apertura de la sesión, de modo que los instantes negativos y los positivos
 * comparten origen. Solo sirve con la sesión recién abierta y antes de la
 * primera muestra; devuelve cuántas muestras previas tomó, cero si no había
 * historia o su origen no es `measured`.
 */
/*
 * Anota el desplazamiento de caldera que regía al abrir. Solo con la sesión
 * activa y un valor finito y no negativo; si no, no anota nada y devuelve false.
 */
bool extraction_session_note_boiler_offset(extraction_session_t *session, float offset_c);
bool extraction_session_note_feedforward_gain(extraction_session_t *session, float gain);

uint32_t extraction_session_take_pre_start(extraction_session_t *session,
                                           const pre_start_history_t *history,
                                           bool measured);
bool extraction_session_finish(extraction_session_t *session,
                               extraction_termination_t termination,
                               uint64_t finished_monotonic_ms);
