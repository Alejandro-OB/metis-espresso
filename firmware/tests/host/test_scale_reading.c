/*
 * Pruebas de la interpretación de la báscula de ensayo.
 *
 * Dos propiedades sostienen todo lo demás:
 *
 * **La extensión de signo.** El HX711 entrega complemento a dos de 24 bits.
 * Copiarlos a un entero de 32 sin extender el signo convierte cualquier lectura
 * negativa —una célula montada al revés, una tara tomada de más— en un valor
 * enorme y positivo. Es el error clásico de este integrado, no rompe nada
 * visiblemente y solo se nota cuando la báscula marca ocho millones.
 *
 * **Que sin calibración medida no se entreguen gramos.** El factor de
 * conversión depende de la célula concreta y solo se obtiene pesando una masa
 * patrón. Un número en gramos que nadie calibró se parece demasiado a una medida
 * para dejarlo suelto en el registro de una campaña experimental.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "scale_reading.h"

static bool close_enough(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/* El complemento a dos de 24 bits se extiende a 32 sin perder el signo. */
static void test_the_sign_survives_the_widening(void)
{
    assert(scale_reading_sign_extend(0x000000U) == 0);
    assert(scale_reading_sign_extend(0x000001U) == 1);
    assert(scale_reading_sign_extend(0x7FFFFFU) == SCALE_COUNTS_MAXIMUM);

    /* 0x800000 es el más negativo, no el más positivo. */
    assert(scale_reading_sign_extend(0x800000U) == SCALE_COUNTS_MINIMUM);
    assert(scale_reading_sign_extend(0xFFFFFFU) == -1);
    assert(scale_reading_sign_extend(0xFFFFFEU) == -2);

    /* Los bits por encima de los 24 se ignoran: el convertidor no los produce y
     * el llamante podría traerlos sucios del desplazamiento. */
    assert(scale_reading_sign_extend(0xFF000001U) == 1);
    assert(scale_reading_sign_extend(0xABFFFFFFU) == -1);
}

/*
 * Sin factor medido hay cuentas, pero no hay masa.
 *
 * La lectura sigue siendo válida: las cuentas son reales y sirven para ver si la
 * célula responde. Lo que no existe es la conversión.
 */
static void test_without_a_measured_factor_there_are_no_grams(void)
{
    const scale_calibration_t sin_calibrar = {
        .counts_per_gram = 0.0F, .tare_counts = 0, .calibration_id = NULL};

    const scale_reading_t lectura =
        scale_reading_evaluate(123456, &sin_calibrar, 0U, 1000U, true, true);
    assert(lectura.quality == SIGNAL_VALID);
    assert(lectura.counts == 123456);
    assert(!lectura.mass_available);

    /* Un factor negativo o no finito tampoco autoriza a convertir. */
    scale_calibration_t negativo = sin_calibrar;
    negativo.counts_per_gram = -420.0F;
    assert(!scale_reading_evaluate(123456, &negativo, 0U, 1000U, true, true).mass_available);

    scale_calibration_t no_finito = sin_calibrar;
    no_finito.counts_per_gram = NAN;
    assert(!scale_reading_evaluate(123456, &no_finito, 0U, 1000U, true, true).mass_available);

    /* Y sin calibración ninguna, tampoco. */
    assert(!scale_reading_evaluate(123456, NULL, 0U, 1000U, true, true).mass_available);
}

/* Con factor y tara medidos, la conversión es la que dice la calibración. */
static void test_with_a_calibration_the_mass_is_the_net_one(void)
{
    /* 420 cuentas por gramo y una tara de 10 000 cuentas: cifras de ejemplo. */
    const scale_calibration_t calibrada = {
        .counts_per_gram = 420.0F, .tare_counts = 10000, .calibration_id = "patron-500g"};

    const scale_reading_t vacia =
        scale_reading_evaluate(10000, &calibrada, 0U, 1000U, true, true);
    assert(vacia.mass_available);
    assert(close_enough(vacia.grams, 0.0F, 0.001F));  /* la tara pesa cero */

    const scale_reading_t con_taza =
        scale_reading_evaluate(10000 + 42000, &calibrada, 0U, 1000U, true, true);
    assert(close_enough(con_taza.grams, 100.0F, 0.001F));

    /* Retirar peso por debajo de la tara da masa negativa, no un número enorme. */
    const scale_reading_t levantada =
        scale_reading_evaluate(10000 - 4200, &calibrada, 0U, 1000U, true, true);
    assert(levantada.mass_available);
    assert(close_enough(levantada.grams, -10.0F, 0.001F));
}

/*
 * Los extremos del convertidor no son «mucho peso».
 *
 * Con una célula sana no se alcanzan: delatan una célula desconectada, un puente
 * roto o una excitación ausente.
 */
static void test_the_converter_extremes_mean_something_is_broken(void)
{
    const scale_calibration_t calibrada = {
        .counts_per_gram = 420.0F, .tare_counts = 0, .calibration_id = "patron-500g"};

    const scale_reading_t tope_alto =
        scale_reading_evaluate(SCALE_COUNTS_MAXIMUM, &calibrada, 0U, 1000U, true, true);
    assert(tope_alto.quality == SIGNAL_SATURATED);
    assert(!tope_alto.mass_available);

    const scale_reading_t tope_bajo =
        scale_reading_evaluate(SCALE_COUNTS_MINIMUM, &calibrada, 0U, 1000U, true, true);
    assert(tope_bajo.quality == SIGNAL_SATURATED);
    assert(!tope_bajo.mass_available);
}

/* Una lectura vieja se marca, y una que nunca llegó no inventa cuentas. */
static void test_an_old_or_absent_reading_is_declared(void)
{
    const scale_calibration_t calibrada = {
        .counts_per_gram = 420.0F, .tare_counts = 0, .calibration_id = "patron-500g"};

    const scale_reading_t vieja =
        scale_reading_evaluate(42000, &calibrada, 5000U, 1000U, true, true);
    assert(vieja.quality == SIGNAL_STALE);
    assert(!vieja.mass_available);

    /* Sin plazo declarado, la antigüedad no se juzga. */
    const scale_reading_t sin_plazo =
        scale_reading_evaluate(42000, &calibrada, 5000U, 0U, true, true);
    assert(sin_plazo.quality == SIGNAL_VALID);

    /* El hilo de datos nunca se puso a nivel bajo: no hubo conversión. */
    const scale_reading_t ausente =
        scale_reading_evaluate(42000, &calibrada, 0U, 1000U, false, true);
    assert(ausente.quality == SIGNAL_NOT_ACQUIRED);
    assert(ausente.counts == 0);
    assert(!ausente.mass_available);
}

/*
 * Una ráfaga que termina con `DOUT` todavía bajo no la leyó ningún integrado.
 *
 * Así se ve una línea de datos pegada a masa, o un reloj que no llega al HX711:
 * el firmware encuentra «conversión lista» en cada sondeo y lee 24 ceros. Ese
 * cero tiene forma de medida y hasta parece asentado; se marca y no se sustituye.
 */
static void test_a_data_line_stuck_low_is_not_a_reading(void)
{
    const scale_calibration_t calibrada = {
        .counts_per_gram = 420.0F, .tare_counts = 0, .calibration_id = "patron-500g"};

    const scale_reading_t pegada =
        scale_reading_evaluate(0, &calibrada, 0U, 1000U, true, false);
    assert(pegada.quality == SIGNAL_SHORT_CIRCUIT);
    assert(pegada.counts == 0);
    assert(!pegada.mass_available);

    /* Un cero de verdad, con la línea liberada, sigue siendo una lectura. */
    const scale_reading_t cero =
        scale_reading_evaluate(0, &calibrada, 0U, 1000U, true, true);
    assert(cero.quality == SIGNAL_VALID);
    assert(cero.mass_available);
}

/*
 * La ventana no declara nada estable antes de estar llena.
 *
 * Importa porque la tara se toma cuando la lectura está asentada: tomarla sobre
 * una báscula que aún oscila la deja mal tomada y desplaza todo lo que venga
 * después.
 */
static void test_stability_needs_a_full_window(void)
{
    scale_stability_t ventana;
    scale_stability_reset(&ventana);

    /* Muestras idénticas: no se puede afirmar nada hasta llenar la ventana. */
    for (uint32_t index = 0U; index < SCALE_STABILITY_SAMPLES - 1U; index++) {
        assert(!scale_stability_push(&ventana, 42000, 10U));
    }
    assert(scale_stability_push(&ventana, 42000, 10U));
    assert(scale_stability_spread(&ventana) == 0U);
    assert(scale_stability_average(&ventana) == 42000);
}

/* Una muestra fuera de tolerancia rompe la estabilidad, y se recupera sola. */
static void test_a_disturbance_breaks_the_stability(void)
{
    scale_stability_t ventana;
    scale_stability_reset(&ventana);
    for (uint32_t index = 0U; index < SCALE_STABILITY_SAMPLES; index++) {
        (void)scale_stability_push(&ventana, 42000, 10U);
    }
    assert(scale_stability_push(&ventana, 42000, 10U));

    /* Un golpe: el recorrido de la ventana se dispara. */
    assert(!scale_stability_push(&ventana, 52000, 10U));

    /* Y hace falta llenar la ventana de nuevo para volver a declararla quieta. */
    for (uint32_t index = 0U; index < SCALE_STABILITY_SAMPLES - 1U; index++) {
        assert(!scale_stability_push(&ventana, 42000, 10U));
    }
    assert(scale_stability_push(&ventana, 42000, 10U));
}

/* La media de la ventana sirve para tomar la tara sobre algo más firme. */
static void test_the_average_smooths_the_tare(void)
{
    scale_stability_t ventana;
    scale_stability_reset(&ventana);
    /* Ocho muestras alrededor de 1000, con ruido de ±4. */
    const int32_t muestras[SCALE_STABILITY_SAMPLES] = {996,  1004, 998, 1002,
                                                        1000, 999,  1001, 1000};
    for (uint32_t index = 0U; index < SCALE_STABILITY_SAMPLES; index++) {
        (void)scale_stability_push(&ventana, muestras[index], 10U);
    }
    assert(scale_stability_spread(&ventana) == 8U);
    assert(scale_stability_average(&ventana) == 1000);
}

/*
 * El recorrido no desborda ni con los dos extremos del convertidor.
 *
 * La diferencia entre ellos son 16 777 215 cuentas, que cabe en 32 bits sin
 * signo pero no en uno con signo si se calcula ingenuamente.
 */
static void test_the_spread_survives_both_extremes(void)
{
    scale_stability_t ventana;
    scale_stability_reset(&ventana);
    (void)scale_stability_push(&ventana, SCALE_COUNTS_MINIMUM, 0U);
    (void)scale_stability_push(&ventana, SCALE_COUNTS_MAXIMUM, 0U);
    assert(scale_stability_spread(&ventana) == 16777215U);
}

static void test_it_survives_a_null_window(void)
{
    scale_stability_reset(NULL);
    assert(!scale_stability_push(NULL, 1, 0U));
    assert(scale_stability_spread(NULL) == 0U);
    assert(scale_stability_average(NULL) == 0);

    /* Una ventana recién puesta a ceros no afirma que nada esté quieto. */
    scale_stability_t vacia = {0};
    assert(scale_stability_spread(&vacia) == 0U);
    assert(scale_stability_average(&vacia) == 0);
}

int main(void)
{
    test_the_sign_survives_the_widening();
    test_without_a_measured_factor_there_are_no_grams();
    test_with_a_calibration_the_mass_is_the_net_one();
    test_the_converter_extremes_mean_something_is_broken();
    test_an_old_or_absent_reading_is_declared();
    test_a_data_line_stuck_low_is_not_a_reading();
    test_stability_needs_a_full_window();
    test_a_disturbance_breaks_the_stability();
    test_the_average_smooths_the_tare();
    test_the_spread_survives_both_extremes();
    test_it_survives_a_null_window();
    printf("test_scale_reading: 10 pruebas correctas\n");
    return 0;
}
