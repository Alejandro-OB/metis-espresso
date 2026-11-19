/*
 * Pruebas del modulador por ciclo integral (ADR-009 § 3.5).
 *
 * La propiedad que justifica que este módulo exista es una sola: **la
 * conducción media sigue exactamente a la demanda, sin sesgo acumulado**. La
 * ventana vale 1 s y el firmware solo conmuta cada 20 ms, de modo que sin
 * residuo la resolución efectiva sería del 2 % y una demanda del 23 % se
 * aplicaría siempre como 20 % o siempre como 40 %. Ese sesgo permanente sería
 * indistinguible de un error de calibración, y es exactamente lo que se
 * comprueba aquí contando milisegundos a lo largo de cien ventanas.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "heater_modulator.h"

#define WINDOW_MS 1000U
#define TICK_MS 20U
#define TICKS_PER_WINDOW (WINDOW_MS / TICK_MS)

/* Conduce `windows` ventanas con demanda constante y devuelve los ms emitidos. */
static uint32_t conduct(heater_modulator_t *modulator, uint8_t demand, uint32_t windows)
{
    uint32_t conducted_ms = 0U;
    for (uint32_t tick = 0U; tick < windows * TICKS_PER_WINDOW; tick++) {
        if (heater_modulator_step(modulator, demand)) {
            conducted_ms += TICK_MS;
        }
    }
    return conducted_ms;
}

static void test_the_configuration_refuses_a_tick_that_splits_the_window(void)
{
    heater_modulator_t modulator = {0};
    assert(heater_modulator_configure(&modulator, WINDOW_MS, TICK_MS));
    /* 30 no divide a 1000: la última porción quedaría más corta que las demás. */
    assert(!heater_modulator_configure(&modulator, 1000U, 30U));
    assert(!heater_modulator_configure(&modulator, 1000U, 0U));
    assert(!heater_modulator_configure(&modulator, 0U, 20U));
    assert(!heater_modulator_configure(&modulator, 20U, 1000U));
    assert(!heater_modulator_configure(NULL, WINDOW_MS, TICK_MS));
}

/*
 * El residuo elimina el sesgo: la media a largo plazo es exacta.
 *
 * Se recorren todas las demandas de 0 a 100 y en cada una se comprueba que los
 * milisegundos conducidos en cien ventanas coinciden **exactamente** con la
 * fracción pedida. No es una tolerancia: la aritmética es entera y el resultado
 * debe ser el número justo.
 */
static void test_the_long_run_average_matches_the_demand_exactly(void)
{
    for (uint32_t demand = 0U; demand <= 100U; demand++) {
        heater_modulator_t modulator = {0};
        assert(heater_modulator_configure(&modulator, WINDOW_MS, TICK_MS));
        const uint32_t windows = 100U;
        const uint32_t conducted = conduct(&modulator, (uint8_t)demand, windows);
        const uint32_t expected = demand * WINDOW_MS * windows / 100U;
        assert(conducted == expected);
    }
}

/* Una demanda que no cabe en el paso alterna ventanas, sin deriva. */
static void test_a_demand_between_ticks_alternates_windows(void)
{
    heater_modulator_t modulator = {0};
    assert(heater_modulator_configure(&modulator, WINDOW_MS, TICK_MS));

    /* 23 % son 230 ms, que no es múltiplo de 20: la primera ventana conduce de
     * más y la segunda lo compensa. */
    const uint32_t first = conduct(&modulator, 23U, 1U);
    assert(first == 240U);
    assert(heater_modulator_residue_ms(&modulator) == -10);

    const uint32_t second = conduct(&modulator, 23U, 1U);
    assert(second == 220U);
    assert(heater_modulator_residue_ms(&modulator) == 0);
    assert(heater_modulator_windows(&modulator) == 2U);
}

/* Una demanda que cae justo en el paso no genera residuo alguno. */
static void test_a_demand_on_the_tick_leaves_no_residue(void)
{
    heater_modulator_t modulator = {0};
    assert(heater_modulator_configure(&modulator, WINDOW_MS, TICK_MS));
    assert(conduct(&modulator, 40U, 1U) == 400U);
    assert(heater_modulator_residue_ms(&modulator) == 0);
    assert(conduct(&modulator, 40U, 1U) == 400U);
    assert(heater_modulator_residue_ms(&modulator) == 0);
}

static void test_the_extremes_behave(void)
{
    heater_modulator_t apagado = {0};
    assert(heater_modulator_configure(&apagado, WINDOW_MS, TICK_MS));
    assert(conduct(&apagado, 0U, 10U) == 0U);

    heater_modulator_t lleno = {0};
    assert(heater_modulator_configure(&lleno, WINDOW_MS, TICK_MS));
    assert(conduct(&lleno, 100U, 10U) == 10U * WINDOW_MS);
    assert(heater_modulator_residue_ms(&lleno) == 0);

    /* Una demanda mal formada por encima del fondo de escala se trata como el
     * máximo, nunca como algo mayor. */
    heater_modulator_t pasado = {0};
    assert(heater_modulator_configure(&pasado, WINDOW_MS, TICK_MS));
    assert(conduct(&pasado, 250U, 2U) == 2U * WINDOW_MS);
}

/*
 * Cero corta de inmediato y no deja deuda.
 *
 * Si el residuo sobreviviera a una demanda nula, la ventana siguiente
 * conduciría para saldar algo que el controlador ya no quiere. Cero significa
 * apagado.
 */
static void test_zero_demand_cuts_at_once_and_discards_the_residue(void)
{
    heater_modulator_t modulator = {0};
    assert(heater_modulator_configure(&modulator, WINDOW_MS, TICK_MS));

    /* Media ventana al 100 %, y entonces la demanda cae a cero. */
    for (uint32_t tick = 0U; tick < TICKS_PER_WINDOW / 2U; tick++) {
        assert(heater_modulator_step(&modulator, 100U));
    }
    for (uint32_t tick = 0U; tick < TICKS_PER_WINDOW / 2U; tick++) {
        assert(!heater_modulator_step(&modulator, 0U));
    }
    assert(heater_modulator_residue_ms(&modulator) == 0);
    /* Lo que sí se condujo antes del corte sigue registrado: ocurrió. */
    assert(heater_modulator_applied_ms(&modulator) == WINDOW_MS / 2U);

    /* Y la ventana siguiente no arrastra ninguna deuda. */
    assert(conduct(&modulator, 0U, 3U) == 0U);
}

/*
 * La demanda solo se lee al abrir la ventana.
 *
 * Es lo que hace que esto sea una modulación por fracción de ventana y no una
 * orden instantánea: subir la demanda a mitad de ventana no adelanta nada.
 */
static void test_the_demand_is_read_when_the_window_opens(void)
{
    heater_modulator_t modulator = {0};
    assert(heater_modulator_configure(&modulator, WINDOW_MS, TICK_MS));

    uint32_t conducted_ms = 0U;
    for (uint32_t tick = 0U; tick < TICKS_PER_WINDOW; tick++) {
        /* Se abre al 20 % y a mitad de ventana se pide el 80 %. */
        const uint8_t demand = tick < (TICKS_PER_WINDOW / 2U) ? 20U : 80U;
        if (heater_modulator_step(&modulator, demand)) {
            conducted_ms += TICK_MS;
        }
    }
    assert(conducted_ms == 200U);  /* el 20 % con el que se abrió */
    /* La ventana siguiente sí obedece al 80 %. */
    assert(conduct(&modulator, 80U, 1U) == 800U);
}

/* Reiniciar olvida ventana y residuo, y conserva la configuración. */
static void test_the_reset_forgets_the_window_and_the_residue(void)
{
    heater_modulator_t modulator = {0};
    assert(heater_modulator_configure(&modulator, WINDOW_MS, TICK_MS));
    (void)conduct(&modulator, 23U, 1U);
    assert(heater_modulator_residue_ms(&modulator) == -10);

    heater_modulator_reset(&modulator);
    assert(heater_modulator_residue_ms(&modulator) == 0);
    assert(heater_modulator_windows(&modulator) == 0U);
    assert(heater_modulator_elapsed_ms(&modulator) == 0U);
    /* Sigue configurado: la primera ventana tras el reinicio conduce lo suyo. */
    assert(conduct(&modulator, 40U, 1U) == 400U);
}

static void test_the_quantizer_refuses_impossible_numbers(void)
{
    assert(heater_modulator_quantize(0.0F) == 0U);
    assert(heater_modulator_quantize(-5.0F) == 0U);
    assert(heater_modulator_quantize(23.4F) == 23U);
    assert(heater_modulator_quantize(23.6F) == 24U);
    assert(heater_modulator_quantize(100.0F) == 100U);
    assert(heater_modulator_quantize(140.0F) == 100U);
    assert(heater_modulator_quantize(NAN) == 0U);
    /* Un infinito es un cálculo roto, no una demanda enorme. */
    assert(heater_modulator_quantize(INFINITY) == 0U);
    assert(heater_modulator_quantize(-INFINITY) == 0U);
}

static void test_it_survives_a_null_modulator(void)
{
    assert(!heater_modulator_step(NULL, 50U));
    heater_modulator_reset(NULL);
    assert(heater_modulator_target_ms(NULL) == 0U);
    assert(heater_modulator_applied_ms(NULL) == 0U);
    assert(heater_modulator_residue_ms(NULL) == 0);
    assert(heater_modulator_windows(NULL) == 0U);
    assert(heater_modulator_elapsed_ms(NULL) == 0U);

    /* Sin configurar tampoco conduce: la estructura a ceros no autoriza nada. */
    heater_modulator_t sin_configurar = {0};
    assert(!heater_modulator_step(&sin_configurar, 100U));
}

int main(void)
{
    test_the_configuration_refuses_a_tick_that_splits_the_window();
    test_the_long_run_average_matches_the_demand_exactly();
    test_a_demand_between_ticks_alternates_windows();
    test_a_demand_on_the_tick_leaves_no_residue();
    test_the_extremes_behave();
    test_zero_demand_cuts_at_once_and_discards_the_residue();
    test_the_demand_is_read_when_the_window_opens();
    test_the_reset_forgets_the_window_and_the_residue();
    test_the_quantizer_refuses_impossible_numbers();
    test_it_survives_a_null_modulator();
    printf("test_heater_modulator: 10 pruebas correctas\n");
    return 0;
}
