#include "stimulus_sequence.h"

#include <stddef.h>

/*
 * Escalón térmico: 30 s de reposo y después la demanda sostenida 1000 s.
 *
 * El reposo inicial registra el ruido contra el que se juzgará si la respuesta
 * es legible. Los 1000 s son cinco constantes de tiempo del modelo de banco
 * (`control_plant_temperature_bench`, τ = 200 s), lo que basta para ver el
 * régimen si la caldera real se parece al modelo; si no se parece, el registro
 * lo dirá y se publica una versión con otra duración. Provisional.
 *
 * No hay escalón final a cero: al agotarse la secuencia la autorización se
 * retira y el guardián vuelve a inhibir por falta de puesta en servicio, de
 * modo que el enfriamiento se registra igual, con el calentador cortado por la
 * ruta ordinaria.
 */
#define HEATER_STEP(id_, percent_)                                       \
    {                                                                    \
        .id = (id_), .target = IDENTIFICATION_TARGET_HEATER,             \
        .step_count = 2U,                                                \
        .steps = {{0U, 30000U}, {(percent_), 1000000U}},                 \
    }

/*
 * Escalón hidráulico: 2 s de reposo, 6 s de demanda y 4 s de descarga.
 *
 * Seis segundos son quince constantes de tiempo del modelo de banco
 * (`control_plant_pressure_bench`, τ = 0,40 s y θ = 0,05 s); la holgura es
 * grande porque la planta real puede ser bastante más lenta. Aquí sí hay
 * escalón final a cero, porque la descarga es uno de los datos que la etapa 6
 * busca y la bomba sigue autorizada mientras dura.
 *
 * Los 12 s caben con margen en una extracción de 30 s, que es el tiempo
 * máximo por omisión: el estado de extracción no se relaja en este modo, y una
 * secuencia más larga que la extracción se cortaría siempre. Provisional.
 */
#define PUMP_STEP(id_, percent_)                                         \
    {                                                                    \
        .id = (id_), .target = IDENTIFICATION_TARGET_PUMP,               \
        .step_count = 3U,                                                \
        .steps = {{0U, 2000U}, {(percent_), 6000U}, {0U, 4000U}},        \
    }

/*
 * Escalón de bomba para la prealimentación del calentador (puesta en servicio
 * 7.9; añadido el 2026-10-11): 1 s de reposo, 23 s de demanda y 1 s de
 * descarga, 25 s en total.
 *
 * El escalón hidráulico de 6 s no sirve aquí: el sensor de temperatura, sobre
 * el cuerpo, tarda unos 44 s en ver un cambio (acta v2), y en 6 s el agua fría
 * apenas llega a notarse. Unos 25 s es lo que dura una extracción de verdad, y
 * es la perturbación que la prealimentación tiene que compensar.
 *
 * Tiene que caber en el tiempo máximo del **perfil**, no en los 30 s por
 * omisión: la primera versión duraba 29 s, el perfil de la placa corta a los
 * 27 s, y el corte cayó en el reposo final, lo que cierra la sesión de la
 * escalera. Provisional.
 */
#define PUMP_THERMAL_STEP(id_, percent_)                                 \
    {                                                                    \
        .id = (id_), .target = IDENTIFICATION_TARGET_PUMP,               \
        .step_count = 3U,                                                \
        .steps = {{0U, 1000U}, {(percent_), 23000U}, {0U, 1000U}},       \
    }

/*
 * Escalón térmico largo, para la etapa 5.1: 30 s de reposo y 2 % durante tres
 * horas.
 *
 * Los escalones de 1000 s se quedaron cortos. Los registros de la calibración
 * del NTC (2026-10-08, puesta en servicio 4.1) dan una caldera de primer orden
 * con τ ≈ 2300 s, no los 200 s del modelo de banco, así que 1000 s no llegan ni
 * a media constante de tiempo y la 5.1 pide el registro hasta el régimen.
 * Tres horas son unas 4,7 τ.
 *
 * Solo al 2 %: con la ganancia estimada, el régimen queda cerca de 78 °C. Al
 * 3 % pasaría de 100 °C y la caldera herviría, porque en Popayán el agua hierve
 * a 94 °C; un escalón que cambia de fase no identifica la planta que se va a
 * regular. Provisional, como los demás: el registro de la 5.1 dirá si la
 * estimación se sostiene.
 */
/*
 * Calentamiento de partida para el enfriamiento libre (5.6): 5 % durante 25 min.
 *
 * Lo que se mide es el enfriamiento posterior, así que basta con dejar la
 * caldera caliente y no hace falta el régimen. Con el modelo de la 4.1, 25 min
 * al 5 % la llevan cerca de 88 °C desde ambiente y la secuencia termina sola,
 * por debajo de la ebullición. El 10 % llegaría antes, pero pasaría de 100 °C
 * si nadie cortara a tiempo, y aquí la parada no puede depender del operador.
 */
#define HEATER_LONG_STEP(id_, percent_, hold_ms_)                        \
    {                                                                    \
        .id = (id_), .target = IDENTIFICATION_TARGET_HEATER,             \
        .step_count = 2U,                                                \
        .steps = {{0U, 30000U}, {(percent_), (hold_ms_)}},               \
    }

static const stimulus_sequence_t s_sequences[] = {
    HEATER_STEP(STIMULUS_SEQUENCE_HEATER_STEP_1PCT_V1, 1U),
    HEATER_STEP(STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V1, 2U),
    HEATER_STEP(STIMULUS_SEQUENCE_HEATER_STEP_3PCT_V1, 3U),
    HEATER_STEP(STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V1, 5U),
    HEATER_STEP(STIMULUS_SEQUENCE_HEATER_STEP_10PCT_V1, 10U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1, 1U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1, 2U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_3PCT_V1, 3U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_5PCT_V1, 5U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_10PCT_V1, 10U),
    HEATER_LONG_STEP(STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V2, 2U, 10800000U),
    HEATER_LONG_STEP(STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V2, 5U, 1500000U),
    /*
     * Escalones de bomba por encima del 10 % (etapa 6, añadidos el 2026-10-10).
     * La escalera de la enmienda llega al 10 %, y con carga representativa eso
     * queda lejos de los ~9 bar de trabajo. Siguen siendo una secuencia por
     * escalón, con la misma forma de 12 s, y la regla no cambia: no se sube
     * mientras el anterior no haya dado una respuesta legible. Se detienen en
     * el 75 % porque ningún ensayo de la etapa 6 pasa de unos 9 bar; si el
     * 75 % no los alcanza, el siguiente escalón se publica entonces, con ese
     * registro delante. El corte de 2 min continuos del guardián rige igual.
     */
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_20PCT_V1, 20U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_30PCT_V1, 30U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_50PCT_V1, 50U),
    PUMP_STEP(STIMULUS_SEQUENCE_PUMP_STEP_75PCT_V1, 75U),
    /*
     * Techo para la sintonía en caliente (ADR-020 § 4.2; añadido el
     * 2026-10-10). No es un escalón: con `CAFE_IDENTIFICATION_THERMAL_TUNING`
     * el PI regula y esta demanda es solo lo máximo que puede pedir, que el
     * guardián juzga contra su tope como cualquier estímulo.
     *
     * El 10 % lleva la caldera de ambiente a la referencia de café en unos
     * diez minutos con la planta de la 4.1, cuando el régimen a 93 °C pide
     * cerca del 2,5 %; deja margen para ver el sobrepaso y la recuperación sin
     * acercarse al 100 %. Dos horas dan para varias pruebas seguidas.
     * Provisional, como las demás.
     */
    HEATER_LONG_STEP(STIMULUS_SEQUENCE_HEATER_TUNING_10PCT_V1, 10U, 7200000U),
    /*
     * Diagnóstico de la bomba, no escalón de identificación (añadido el
     * 2026-10-10). En la etapa 6 la bomba dejó de sonar a partir del 5 % aunque
     * la placa emitía los pulsos, y sus cables no son accesibles; esta
     * secuencia la deja 25 s al 50 % para oír y ver si mueve agua. Cabe en los
     * 30 s de extracción y está lejos de los 2 min continuos del guardián. Su
     * registro no entra en el ajuste de la planta de presión.
     */
    {
        .id = STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_50PCT_V1,
        .target = IDENTIFICATION_TARGET_PUMP,
        .step_count = 3U,
        .steps = {{0U, 2000U}, {50U, 25000U}, {0U, 2000U}},
    },
    /* El mismo diagnóstico a plena densidad, 10 s: conduce en todos los
     * semiciclos, lo más parecido a la bomba conectada a la red pero a través
     * del dimmer y con el guardián. Si así tampoco suena, el fallo es del
     * camino de potencia, no de la densidad. */
    {
        .id = STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_100PCT_V1,
        .target = IDENTIFICATION_TARGET_PUMP,
        .step_count = 3U,
        .steps = {{0U, 2000U}, {100U, 10000U}, {0U, 2000U}},
    },
    /*
     * Arrastre a plena potencia (añadido el 2026-10-11). El arranque rápido
     * —todo el calentador hasta cerca de la referencia y después el PI— exige
     * saber cuánto sigue subiendo la caldera tras cortar el 100 %, y hoy solo
     * se ha medido al 2 y al 5 % (5.8 y 5.10).
     *
     * Es un pulso y no un escalón hasta el corte porque así la energía queda
     * acotada por construcción: 30 s a unos 1029 W son unos 31 kJ, y con los
     * 742 J/K del conjunto la caldera no puede subir más de unos 42 °C en
     * total, pase lo que pase con el retardo. Desde frío termina lejos de
     * hervir. Lo que se mide es la subida después del pulso. Si 30 s resultan
     * cortos para que el elemento alcance su gradiente de régimen, se publica
     * una versión más larga con este registro delante. Provisional.
     */
    {
        .id = STIMULUS_SEQUENCE_HEATER_PULSE_100PCT_V1,
        .target = IDENTIFICATION_TARGET_HEATER,
        .step_count = 3U,
        .steps = {{0U, 10000U}, {100U, 30000U}, {0U, 900000U}},
    },
    PUMP_THERMAL_STEP(STIMULUS_SEQUENCE_PUMP_THERMAL_10PCT_V1, 10U),
    PUMP_THERMAL_STEP(STIMULUS_SEQUENCE_PUMP_THERMAL_30PCT_V1, 30U),
    PUMP_THERMAL_STEP(STIMULUS_SEQUENCE_PUMP_THERMAL_50PCT_V1, 50U),
    /*
     * Techo para la sintonía en caliente de la presión (enmienda a ADR-007
     * § 5 del 2026-10-12). Como el del calentador, no es un escalón: con
     * `CAFE_IDENTIFICATION_PRESSURE_TUNING` el PI regula hacia la referencia
     * del perfil y el 75 % es solo lo máximo que puede pedir.
     *
     * Es el escalón más alto medido con café (6.5, 10,3 bar), así que el PI
     * nunca lleva la bomba a una zona que no se haya visto. Dura lo mismo que
     * los escalones de la 7.9, 25 s, y cabe en los 27 s de extracción del
     * perfil. Una extracción por prueba: `B,step,75` la vuelve a armar.
     */
    PUMP_THERMAL_STEP(STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1, 75U),
};

const stimulus_sequence_t *stimulus_sequence_find(stimulus_sequence_id_t id)
{
    for (size_t index = 0U; index < sizeof(s_sequences) / sizeof(s_sequences[0]); index++) {
        if (s_sequences[index].id == id) {
            return &s_sequences[index];
        }
    }
    return NULL;
}

const char *stimulus_sequence_name(stimulus_sequence_id_t id)
{
    switch (id) {
    case STIMULUS_SEQUENCE_HEATER_STEP_1PCT_V1:
        return "heater-step-1pct-v1";
    case STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V1:
        return "heater-step-2pct-v1";
    case STIMULUS_SEQUENCE_HEATER_STEP_3PCT_V1:
        return "heater-step-3pct-v1";
    case STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V1:
        return "heater-step-5pct-v1";
    case STIMULUS_SEQUENCE_HEATER_STEP_10PCT_V1:
        return "heater-step-10pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1:
        return "pump-step-1pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1:
        return "pump-step-2pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_3PCT_V1:
        return "pump-step-3pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_5PCT_V1:
        return "pump-step-5pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_10PCT_V1:
        return "pump-step-10pct-v1";
    case STIMULUS_SEQUENCE_HEATER_STEP_2PCT_V2:
        return "heater-step-2pct-v2";
    case STIMULUS_SEQUENCE_HEATER_STEP_5PCT_V2:
        return "heater-step-5pct-v2";
    case STIMULUS_SEQUENCE_PUMP_STEP_20PCT_V1:
        return "pump-step-20pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_30PCT_V1:
        return "pump-step-30pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_50PCT_V1:
        return "pump-step-50pct-v1";
    case STIMULUS_SEQUENCE_PUMP_STEP_75PCT_V1:
        return "pump-step-75pct-v1";
    case STIMULUS_SEQUENCE_HEATER_TUNING_10PCT_V1:
        return "heater-tuning-10pct-v1";
    case STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_50PCT_V1:
        return "pump-diagnostic-50pct-v1";
    case STIMULUS_SEQUENCE_PUMP_DIAGNOSTIC_100PCT_V1:
        return "pump-diagnostic-100pct-v1";
    case STIMULUS_SEQUENCE_HEATER_PULSE_100PCT_V1:
        return "heater-pulse-100pct-v1";
    case STIMULUS_SEQUENCE_PUMP_THERMAL_10PCT_V1:
        return "pump-thermal-10pct-v1";
    case STIMULUS_SEQUENCE_PUMP_THERMAL_30PCT_V1:
        return "pump-thermal-30pct-v1";
    case STIMULUS_SEQUENCE_PUMP_THERMAL_50PCT_V1:
        return "pump-thermal-50pct-v1";
    case STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1:
        return "pump-tuning-75pct-v1";
    case STIMULUS_SEQUENCE_NONE:
    case STIMULUS_SEQUENCE_COUNT:
    default:
        return "none";
    }
}

uint32_t stimulus_sequence_duration_ms(const stimulus_sequence_t *sequence)
{
    if (sequence == NULL || sequence->step_count > STIMULUS_SEQUENCE_MAXIMUM_STEPS) {
        return 0U;
    }
    uint32_t total = 0U;
    for (uint32_t index = 0U; index < sequence->step_count; index++) {
        const uint32_t duration = sequence->steps[index].duration_ms;
        total = UINT32_MAX - total < duration ? UINT32_MAX : total + duration;
    }
    return total;
}

uint8_t stimulus_sequence_peak_percent(const stimulus_sequence_t *sequence)
{
    if (sequence == NULL || sequence->step_count > STIMULUS_SEQUENCE_MAXIMUM_STEPS) {
        return 0U;
    }
    uint8_t peak = 0U;
    for (uint32_t index = 0U; index < sequence->step_count; index++) {
        if (sequence->steps[index].demand_percent > peak) {
            peak = sequence->steps[index].demand_percent;
        }
    }
    return peak;
}

bool stimulus_sequence_is_valid(const stimulus_sequence_t *sequence)
{
    if (sequence == NULL || sequence->target == IDENTIFICATION_TARGET_NONE ||
        sequence->step_count == 0U ||
        sequence->step_count > STIMULUS_SEQUENCE_MAXIMUM_STEPS) {
        return false;
    }
    for (uint32_t index = 0U; index < sequence->step_count; index++) {
        if (sequence->steps[index].duration_ms == 0U ||
            sequence->steps[index].demand_percent > 100U) {
            return false;
        }
    }
    return true;
}

uint8_t stimulus_sequence_demand_at(const stimulus_sequence_t *sequence,
                                    uint32_t elapsed_ms)
{
    if (!stimulus_sequence_is_valid(sequence)) {
        return 0U;
    }
    uint32_t start = 0U;
    for (uint32_t index = 0U; index < sequence->step_count; index++) {
        const uint32_t duration = sequence->steps[index].duration_ms;
        if (elapsed_ms - start < duration) {
            return sequence->steps[index].demand_percent;
        }
        start += duration;
    }
    return 0U;
}
