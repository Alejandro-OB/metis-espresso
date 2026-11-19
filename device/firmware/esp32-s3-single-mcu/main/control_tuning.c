#include "control_tuning.h"

#include <math.h>
#include <stddef.h>

#include "commissioned_plant.h"
#include "control_plant.h"

/*
 * `λ` como múltiplo del retardo.
 *
 * SIMC admite `λ = θ` como elección agresiva y valores mayores como
 * conservadores. Se toma el triple porque el modelo no está identificado: si el
 * retardo real resultara ser el doble del supuesto, una sintonía con `λ = θ`
 * quedaría al borde de la inestabilidad y esta seguiría siendo estable, solo más
 * lenta.
 */
#define LAMBDA_OVER_DEAD_TIME 3.0F

/* Periodos de ADR-007 § 3.3 y ADR-009 § 3.6. No vienen del acta: el periodo es
 * parte de la arquitectura del lazo, no de la planta que se mide. */
#define CONTROL_TUNING_PRESSURE_PERIOD_S 0.020F
#define CONTROL_TUNING_TEMPERATURE_PERIOD_S 0.200F

/*
 * Retardo mínimo con el que se sintoniza.
 *
 * Con `θ = 0` la fórmula de SIMC entrega una ganancia infinita, que no es una
 * sintonía agresiva sino una división por cero disfrazada. Ninguna planta física
 * tiene retardo nulo; cuando el modelo lo declara así, se sustituye por medio
 * periodo de muestreo, que es el retardo que introduce el propio muestreo.
 */
static float effective_dead_time(float dead_time_s, float sample_period_s)
{
    const float floor_s = 0.5F * sample_period_s;
    return dead_time_s > floor_s ? dead_time_s : floor_s;
}

bool control_tuning_simc(float gain,
                         float time_constant_s,
                         float dead_time_s,
                         float lambda_s,
                         float *proportional_gain,
                         float *integral_gain)
{
    if (proportional_gain == NULL || integral_gain == NULL) {
        return false;
    }
    if (!isfinite(gain) || !isfinite(time_constant_s) || !isfinite(dead_time_s) ||
        !isfinite(lambda_s) || gain <= 0.0F || time_constant_s <= 0.0F ||
        dead_time_s < 0.0F || lambda_s < 0.0F) {
        return false;
    }
    const float horizon = lambda_s + dead_time_s;
    if (horizon <= 0.0F) {
        return false;
    }

    const float kp = time_constant_s / (gain * horizon);
    const float four_horizons = 4.0F * horizon;
    const float ti = time_constant_s < four_horizons ? time_constant_s : four_horizons;
    if (ti <= 0.0F || !isfinite(kp)) {
        return false;
    }

    *proportional_gain = kp;
    *integral_gain = kp / ti;
    return true;
}

/* Solo hace falta mientras algún lazo siga sobre el modelo de banco. */
/* La de banco del lazo térmico se ofrece siempre (`control_tuning_temperature_bench`),
 * así que esta función también. */
static control_tuning_t tuning_from(const control_plant_parameters_t *plant,
                                    float filter_time_constant_s,
                                    float setpoint_rate)
{
    const float dead_time_s =
        effective_dead_time(plant->dead_time_s, plant->sample_period_s);
    const float lambda_s = LAMBDA_OVER_DEAD_TIME * dead_time_s;

    control_tuning_t tuning = {
        .identifier = CONTROL_TUNING_IDENTIFIER,
        /* Falso, y debe seguir siéndolo: la planta real no está identificada. */
        .commissioned = false,
        .filter_time_constant_s = filter_time_constant_s,
        .sample_period_s = plant->sample_period_s,
        .pi = {
            .proportional_gain = 0.0F,
            .integral_gain = 0.0F,
            .sample_period_s = plant->sample_period_s,
            .output_minimum = 0.0F,
            .output_maximum = 100.0F,
            .setpoint_rate = setpoint_rate,
        },
    };

    /* Si el modelo no admitiera sintonía, las ganancias quedan en cero: un lazo
     * que no actúa es el estado seguro, y el guardián ya lo impide de todos
     * modos mientras `commissioned` sea falso. */
    (void)control_tuning_simc(plant->gain, plant->time_constant_s, dead_time_s, lambda_s,
                              &tuning.pi.proportional_gain, &tuning.pi.integral_gain);
    return tuning;
}

/* Un identificador que cabe en la columna del registro y no la parte en dos. */
static bool identifier_is_usable(const char *identifier)
{
    if (identifier == NULL || identifier[0] == '\0') {
        return false;
    }
    size_t length = 0U;
    for (; identifier[length] != '\0'; length++) {
        if (identifier[length] == ',' || length + 1U >= CONTROL_TUNING_ID_BYTES) {
            return false;
        }
    }
    return true;
}

control_tuning_t control_tuning_from_identified(const control_identified_plant_t *plant,
                                               float sample_period_s)
{
    control_tuning_t tuning = {
        .identifier = "invalid-plant",
        .commissioned = false,
        .sample_period_s = sample_period_s,
        .pi = {
            .sample_period_s = sample_period_s,
            .output_minimum = 0.0F,
            .output_maximum = 100.0F,
        },
    };
    if (plant == NULL || !identifier_is_usable(plant->identifier) ||
        !isfinite(sample_period_s) || sample_period_s <= 0.0F ||
        !isfinite(plant->filter_time_constant_s) || plant->filter_time_constant_s <= 0.0F ||
        !isfinite(plant->setpoint_rate) || plant->setpoint_rate <= 0.0F ||
        !(plant->output_maximum > 0.0F && plant->output_maximum <= 100.0F)) {
        return tuning;
    }
    tuning.identifier = plant->identifier;
    tuning.filter_time_constant_s = plant->filter_time_constant_s;
    tuning.pi.setpoint_rate = plant->setpoint_rate;
    tuning.pi.output_maximum = plant->output_maximum;

    /* La `λ` es la del acta y no el triple del retardo: elegirla es parte de la
     * sintonía que se validó. El retardo sí pasa por el mismo suelo, porque con
     * θ = 0 la fórmula no es una sintonía sino una división por cero. */
    const float dead_time_s = effective_dead_time(plant->dead_time_s, sample_period_s);
    float kp = 0.0F;
    float ki = 0.0F;
    if (!control_tuning_simc(plant->gain, plant->time_constant_s, dead_time_s, plant->lambda_s,
                             &kp, &ki)) {
        return tuning;
    }
    tuning.pi.proportional_gain = kp;
    tuning.pi.integral_gain = ki;
    tuning.commissioned = true;
    return tuning;
}

control_tuning_t control_tuning_pressure(void)
{
#if HAVE_COMMISSIONED_PRESSURE
    const control_identified_plant_t identified = {
        .identifier = COMMISSIONED_PRESSURE_IDENTIFIER,
        .gain = COMMISSIONED_PRESSURE_GAIN,
        .time_constant_s = COMMISSIONED_PRESSURE_TIME_CONSTANT_S,
        .dead_time_s = COMMISSIONED_PRESSURE_DEAD_TIME_S,
        .lambda_s = COMMISSIONED_PRESSURE_LAMBDA_S,
        .filter_time_constant_s = COMMISSIONED_PRESSURE_FILTER_TIME_CONSTANT_S,
        .setpoint_rate = COMMISSIONED_PRESSURE_SETPOINT_RATE,
        .output_maximum = COMMISSIONED_PRESSURE_OUTPUT_MAXIMUM,
    };
    return control_tuning_from_identified(&identified, CONTROL_TUNING_PRESSURE_PERIOD_S);
#else
    return control_tuning_pressure_bench();
#endif
}

control_tuning_t control_tuning_pressure_bench(void)
{
    const control_plant_parameters_t plant = control_plant_pressure_bench();
    /* ADR-007 § 3.3: τf = 50 ms. La velocidad de referencia lleva de 0 a 9 bar
     * en unos tres segundos, que es un arranque suave y no un escalón. */
    return tuning_from(&plant, 0.050F, 3.0F);
}

control_tuning_t control_tuning_temperature_bench(void)
{
    const control_plant_parameters_t plant = control_plant_temperature_bench();
    /*
     * ADR-009 § 3.6: τf = 1 s.
     *
     * La referencia sube a 0,3 °C/s, y el número importa. **Una referencia que
     * corre más de lo que la planta puede seguir no es un arranque suave: es un
     * escalón disfrazado.** El lazo satura igual, y la integral se carga contra
     * un error que el proceso no puede cerrar, de modo que al terminar la rampa
     * hay que descargarla y la temperatura se pasa.
     *
     * Medido sobre el modelo de banco el 2026-09-15: a 1 °C/s el lazo llega al
     * 100 % y sobrepasa 6,5 °C; a 0,3 °C/s no pasa del 47 % y sobrepasa 2,4 °C,
     * tardando unos cuatro minutos en calentar, que es lo que tarda una máquina
     * de este tamaño. El valor definitivo queda reservado a la puesta en marcha
     * (ADR-009 § 4), pero el criterio para elegirlo ya no es una opinión.
     */
    return tuning_from(&plant, 1.0F, 0.30F);
}

control_tuning_t control_tuning_temperature(void)
{
#if HAVE_COMMISSIONED_TEMPERATURE
    const control_identified_plant_t identified = {
        .identifier = COMMISSIONED_TEMPERATURE_IDENTIFIER,
        .gain = COMMISSIONED_TEMPERATURE_GAIN,
        .time_constant_s = COMMISSIONED_TEMPERATURE_TIME_CONSTANT_S,
        .dead_time_s = COMMISSIONED_TEMPERATURE_DEAD_TIME_S,
        .lambda_s = COMMISSIONED_TEMPERATURE_LAMBDA_S,
        .filter_time_constant_s = COMMISSIONED_TEMPERATURE_FILTER_TIME_CONSTANT_S,
        .setpoint_rate = COMMISSIONED_TEMPERATURE_SETPOINT_RATE,
        .output_maximum = COMMISSIONED_TEMPERATURE_OUTPUT_MAXIMUM,
    };
    return control_tuning_from_identified(&identified, CONTROL_TUNING_TEMPERATURE_PERIOD_S);
#else
    return control_tuning_temperature_bench();
#endif
}
