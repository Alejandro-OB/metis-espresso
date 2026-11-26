/*
 * Verificación en host de la conversión de señales y de la calidad de medida.
 *
 * Los valores esperados se calcularon aparte del firmware a partir del esquema
 * eléctrico, para no comprobar el código contra sí mismo.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "sensor_scaling.h"
#include "signal_quality.h"

static bool cerca(float valor, float esperado, float tolerancia)
{
    return fabsf(valor - esperado) <= tolerancia;
}

static void test_counts_convert_to_the_input_voltage(void)
{
    /* Fondo de escala ±4,096 V sobre 15 bits de magnitud. */
    assert(cerca(sensor_volts_from_counts(0), 0.0F, 1e-6F));
    assert(cerca(sensor_volts_from_counts(32767), 4.096F, 1e-3F));
    assert(cerca(sensor_volts_from_counts(16384), 2.048F, 1e-4F));
    assert(cerca(sensor_volts_from_counts(12160), 1.5200F, 1e-3F));
}

static void test_the_thermistor_chain_reproduces_the_reference_table(void)
{
    const ntc_scaling_t escala = sensor_ntc_default();
    const struct { float volts; float esperado_c; } casos[] = {
        {3.0000F,  25.0F},
        {2.3534F,  60.0F},
        {1.5200F,  93.0F},
        {0.7949F, 130.0F},
        {0.5493F, 150.0F},
    };
    for (size_t i = 0U; i < sizeof(casos) / sizeof(casos[0]); ++i) {
        const float resistencia = sensor_ntc_resistance_ohms(casos[i].volts, &escala);
        float temperatura = 0.0F;
        assert(resistencia > 0.0F);
        assert(sensor_ntc_temperature_c(resistencia, &escala, &temperatura));
        assert(cerca(temperatura, casos[i].esperado_c, 0.1F));
    }
    /* La resistencia a 25 °C debe ser la de referencia del componente. */
    assert(cerca(sensor_ntc_resistance_ohms(3.0F, &escala), 100000.0F, 50.0F));
}

static void test_the_thermistor_chain_is_reversible(void)
{
    const ntc_scaling_t escala = sensor_ntc_default();
    for (float objetivo = 20.0F; objetivo <= 140.0F; objetivo += 10.0F) {
        const float volts = sensor_ntc_volts_at(objetivo, &escala);
        float temperatura = 0.0F;
        assert(sensor_ntc_temperature_c(sensor_ntc_resistance_ohms(volts, &escala), &escala,
                                        &temperatura));
        assert(cerca(temperatura, objetivo, 0.05F));
    }
}

static void test_impossible_voltages_yield_no_resistance(void)
{
    const ntc_scaling_t escala = sensor_ntc_default();
    assert(sensor_ntc_resistance_ohms(0.0F, &escala) < 0.0F);    /* cortocircuito */
    assert(sensor_ntc_resistance_ohms(3.3F, &escala) < 0.0F);    /* circuito abierto */
    assert(sensor_ntc_resistance_ohms(4.0F, &escala) < 0.0F);    /* por encima del divisor */
    float temperatura = 0.0F;
    assert(!sensor_ntc_temperature_c(-1.0F, &escala, &temperatura));
}

static void test_the_pressure_chain_spans_the_full_transducer_range(void)
{
    const pressure_scaling_t escala = sensor_pressure_default();
    /* Tras el divisor 2/3: 0,5 V del sensor son 0,3333 V; 4,5 V son 3,0 V. */
    assert(cerca(sensor_pressure_bar(0.33333F, &escala), 0.0F, 0.01F));
    assert(cerca(sensor_pressure_bar(1.00000F, &escala), 3.0F, 0.01F));
    assert(cerca(sensor_pressure_bar(1.66667F, &escala), 6.0F, 0.01F));
    assert(cerca(sensor_pressure_bar(2.33333F, &escala), 9.0F, 0.01F));
    assert(cerca(sensor_pressure_bar(3.00000F, &escala), 12.0F, 0.01F));
}

static void test_quality_separates_a_disconnected_sensor_from_a_cold_one(void)
{
    const signal_limits_t limites = signal_limits_temperature();
    const ntc_scaling_t escala = sensor_ntc_default();
    /* A temperatura de trabajo la lectura es válida. */
    assert(signal_quality_evaluate(1.52F, 93.0F, 100U, true, &limites) == SIGNAL_VALID);
    /* Un sensor desconectado lleva la entrada al extremo alto. */
    assert(signal_quality_evaluate(3.29F, 0.0F, 100U, true, &limites) == SIGNAL_OPEN_CIRCUIT);
    /* Un cortocircuito, al extremo bajo. */
    assert(signal_quality_evaluate(0.01F, 300.0F, 100U, true, &limites) == SIGNAL_SHORT_CIRCUIT);
    /* Ambiente sigue siendo distinguible de la desconexión, con poco margen. */
    const float ambiente = sensor_ntc_volts_at(22.0F, &escala);
    assert(ambiente < limites.maximum_volts);
    assert(signal_quality_evaluate(ambiente, 22.0F, 100U, true, &limites) == SIGNAL_VALID);
    /* Pero por debajo de unos −12 °C ya no: el divisor no da margen suficiente. */
    assert(sensor_ntc_volts_at(-20.0F, &escala) > limites.maximum_volts);
    assert(sensor_ntc_volts_at(-5.0F, &escala) < limites.maximum_volts);
}

static void test_quality_flags_saturation_range_staleness_and_absence(void)
{
    const signal_limits_t temperatura = signal_limits_temperature();
    const signal_limits_t presion = signal_limits_pressure();
    assert(signal_quality_evaluate(ADS1115_FULL_SCALE_V, 50.0F, 10U, true, &temperatura) ==
           SIGNAL_SATURATED);
    assert(signal_quality_evaluate(1.5F, 400.0F, 10U, true, &temperatura) == SIGNAL_OUT_OF_RANGE);
    assert(signal_quality_evaluate(1.5F, 93.0F, 5000U, true, &temperatura) == SIGNAL_STALE);
    assert(signal_quality_evaluate(1.5F, 93.0F, 10U, false, &temperatura) == SIGNAL_NOT_ACQUIRED);
    assert(signal_quality_evaluate(NAN, 93.0F, 10U, true, &temperatura) == SIGNAL_OUT_OF_RANGE);
    /* La presión tiene su propio suelo: por debajo de 0,2 V el sensor no está. */
    assert(signal_quality_evaluate(0.1F, 0.0F, 10U, true, &presion) == SIGNAL_SHORT_CIRCUIT);
    assert(signal_quality_evaluate(0.3333F, 0.0F, 10U, true, &presion) == SIGNAL_VALID);
    /*
     * Y caduca antes, porque gobierna un lazo más rápido: una antigüedad que la
     * presión ya rechaza sigue sirviendo para la temperatura. Se compara contra
     * los propios límites y no contra una cifra escrita aquí, de modo que la
     * propiedad sobreviva a un cambio de cadencia.
     */
    assert(presion.maximum_age_ms < temperatura.maximum_age_ms);
    const uint32_t entre_ambos = presion.maximum_age_ms + 1U;
    assert(signal_quality_evaluate(1.0F, 3.0F, entre_ambos, true, &presion) == SIGNAL_STALE);
    assert(signal_quality_evaluate(1.0F, 3.0F, entre_ambos, true, &temperatura) == SIGNAL_VALID);
}

/*
 * El plazo de caducidad acompaña a la cadencia de su canal (H-04).
 *
 * Cinco ciclos de cada uno: la presión se convierte cada 10 ms y la temperatura
 * cada 100 ms. La comprobación es deliberadamente literal porque su valor está
 * en fallar: si alguien cambia la cadencia de adquisición y olvida revisar el
 * plazo, esta prueba lo dice. Ese olvido es justamente lo que ocurrió cuando el
 * ciclo era de 100 ms y la presión admitía medio segundo, cincuenta ciclos.
 */
static void test_staleness_limits_match_the_acquisition_cadence(void)
{
    const signal_limits_t temperatura = signal_limits_temperature();
    const signal_limits_t presion = signal_limits_pressure();
    assert(presion.maximum_age_ms == 5U * 10U);
    assert(temperatura.maximum_age_ms == 5U * 100U);
}

/*
 * Una estructura sin escribir no puede declararse válida: el cero del
 * enumerado debe ser el estado más conservador.
 */
static void test_a_zeroed_measurement_is_never_usable(void)
{
    signal_quality_t recien_inicializada = (signal_quality_t)0;
    assert(recien_inicializada == SIGNAL_NOT_ACQUIRED);
    assert(!signal_quality_is_usable(recien_inicializada));
}

static void test_only_a_valid_reading_may_be_used(void)
{
    assert(signal_quality_is_usable(SIGNAL_VALID));
    const signal_quality_t rechazadas[] = {SIGNAL_OPEN_CIRCUIT, SIGNAL_SHORT_CIRCUIT,
                                           SIGNAL_SATURATED,    SIGNAL_OUT_OF_RANGE,
                                           SIGNAL_STALE,        SIGNAL_NOT_ACQUIRED};
    for (size_t i = 0U; i < sizeof(rechazadas) / sizeof(rechazadas[0]); ++i) {
        assert(!signal_quality_is_usable(rechazadas[i]));
        assert(signal_quality_name(rechazadas[i]) != NULL);
    }
}

int main(void)
{
    test_counts_convert_to_the_input_voltage();
    test_the_thermistor_chain_reproduces_the_reference_table();
    test_the_thermistor_chain_is_reversible();
    test_impossible_voltages_yield_no_resistance();
    test_the_pressure_chain_spans_the_full_transducer_range();
    test_quality_separates_a_disconnected_sensor_from_a_cold_one();
    test_quality_flags_saturation_range_staleness_and_absence();
    test_staleness_limits_match_the_acquisition_cadence();
    test_a_zeroed_measurement_is_never_usable();
    test_only_a_valid_reading_may_be_used();
    printf("PASS: conversión de sensores y calidad de señal\n");
    return 0;
}
