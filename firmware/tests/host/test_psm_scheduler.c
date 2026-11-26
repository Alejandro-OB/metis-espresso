/*
 * Pruebas del planificador por densidad de pulsos (H-09).
 *
 * Las propiedades que importan no son «emite a veces», sino que la densidad
 * realizada iguale exactamente a la solicitada sin deriva, que los extremos
 * signifiquen lo que dicen y que las actuaciones queden repartidas en lugar de
 * agrupadas en ráfagas.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "psm_scheduler.h"

/*
 * Una solicitud de 0 % no genera ningún disparo.
 *
 * ADR-007 § 3.4 lo deja cerrado y es una condición de seguridad, no una
 * preferencia: el guardián ordena densidad cero para detener la bomba, y una
 * sola actuación emitida «por redondeo» sería una actuación que nadie autorizó.
 */
static void test_zero_never_fires(void)
{
    psm_scheduler_t planificador = {0};
    psm_scheduler_reset(&planificador);
    for (int i = 0; i < 10000; ++i) {
        assert(!psm_scheduler_step(&planificador, 0U));
    }
    assert(psm_scheduler_emitted(&planificador) == 0U);
}

/* Y una de 100 % aprovecha todas las oportunidades. */
static void test_full_scale_uses_every_opportunity(void)
{
    psm_scheduler_t planificador = {0};
    psm_scheduler_reset(&planificador);
    for (int i = 0; i < 10000; ++i) {
        assert(psm_scheduler_step(&planificador, 100U));
    }
    assert(psm_scheduler_emitted(&planificador) == 10000U);
}

/*
 * La densidad realizada iguala a la solicitada, exactamente y sin deriva.
 *
 * Se comprueba para las 101 densidades posibles: en cada bloque de 100
 * oportunidades deben emitirse tantas actuaciones como por ciento se pidió, ni
 * una más ni una menos. Es la propiedad que justifica el acumulador entero: con
 * coma flotante, el residuo arrastrado durante cien bloques desplazaría la
 * cuenta y ese sesgo sería indistinguible de un error de calibración.
 */
static void test_the_realised_density_matches_the_request_without_drift(void)
{
    for (unsigned densidad = 0U; densidad <= 100U; ++densidad) {
        psm_scheduler_t planificador = {0};
        psm_scheduler_reset(&planificador);
        for (int bloque = 0; bloque < 100; ++bloque) {
            const uint32_t antes = psm_scheduler_emitted(&planificador);
            for (int i = 0; i < 100; ++i) {
                (void)psm_scheduler_step(&planificador, (uint8_t)densidad);
            }
            const uint32_t en_el_bloque = psm_scheduler_emitted(&planificador) - antes;
            assert(en_el_bloque == densidad);
        }
        assert(psm_scheduler_emitted(&planificador) == densidad * 100U);
        assert(psm_scheduler_opportunities(&planificador) == 10000U);
    }
}

/*
 * Las actuaciones quedan repartidas, no agrupadas.
 *
 * Con la misma densidad media, una ráfaga produce mucho más rizado de presión
 * que un reparto uniforme. La propiedad concreta del acumulador es que el hueco
 * entre actuaciones consecutivas toma como mucho dos valores, y difieren en uno:
 * al 30 %, huecos de tres y cuatro oportunidades, nunca de uno y de diez.
 */
static void test_actuations_are_spread_not_bursted(void)
{
    for (unsigned densidad = 1U; densidad < 100U; ++densidad) {
        psm_scheduler_t planificador = {0};
        psm_scheduler_reset(&planificador);

        int hueco = 0;
        int minimo = 1000000;
        int maximo = 0;
        bool primera = true;
        for (int i = 0; i < 5000; ++i) {
            ++hueco;
            if (psm_scheduler_step(&planificador, (uint8_t)densidad)) {
                /* El primer hueco depende del estado inicial y no describe el
                 * régimen permanente, de modo que no entra en la comparación. */
                if (!primera) {
                    if (hueco < minimo) {
                        minimo = hueco;
                    }
                    if (hueco > maximo) {
                        maximo = hueco;
                    }
                }
                primera = false;
                hueco = 0;
            }
        }
        assert(maximo - minimo <= 1);
    }
}

/*
 * La cuantización respeta la resolución decidida y trata lo imposible como cero.
 *
 * Una demanda no finita solo puede venir de un cálculo roto aguas arriba. La
 * respuesta defendible es no actuar, nunca actuar al máximo.
 */
static void test_quantisation_rounds_clamps_and_refuses_the_impossible(void)
{
    assert(psm_scheduler_quantize(0.0F) == 0U);
    assert(psm_scheduler_quantize(100.0F) == 100U);
    assert(psm_scheduler_quantize(49.4F) == 49U);
    assert(psm_scheduler_quantize(49.6F) == 50U);

    /* Fuera de rango se acota, en ambos sentidos. */
    assert(psm_scheduler_quantize(-30.0F) == 0U);
    assert(psm_scheduler_quantize(140.0F) == 100U);

    /* Y lo imposible no actúa, tampoco el infinito positivo. Podría parecer
     * que un valor «muy grande» debería saturar al máximo, pero un infinito no
     * es una demanda grande: es la huella de un cálculo roto. Traducirlo a
     * 100 % ordenaría la bomba al máximo precisamente cuando el número que la
     * gobierna ha dejado de significar algo. */
    assert(psm_scheduler_quantize(NAN) == 0U);
    assert(psm_scheduler_quantize(INFINITY) == 0U);
    assert(psm_scheduler_quantize(-INFINITY) == 0U);
}

/* Una densidad por encima del fondo de escala no puede producir más que el
 * máximo: se trata como 100 %, no como un acumulador que avanza al doble. */
static void test_an_out_of_scale_density_cannot_exceed_full_scale(void)
{
    psm_scheduler_t planificador = {0};
    psm_scheduler_reset(&planificador);
    for (int i = 0; i < 1000; ++i) {
        assert(psm_scheduler_step(&planificador, 250U));
    }
    assert(psm_scheduler_emitted(&planificador) == 1000U);
    assert(psm_scheduler_residue(&planificador) == 0U);
}

/*
 * Al pedir cero, el residuo pendiente se descarta.
 *
 * Si se conservara, la siguiente oportunidad con demanda distinta de cero podría
 * emitir de inmediato una actuación heredada de una demanda anterior. Cuando el
 * guardián corta, corta: lo pendiente deja de existir.
 */
static void test_asking_for_zero_discards_the_pending_residue(void)
{
    psm_scheduler_t planificador = {0};
    psm_scheduler_reset(&planificador);
    for (int i = 0; i < 3; ++i) {
        (void)psm_scheduler_step(&planificador, 30U);
    }
    assert(psm_scheduler_residue(&planificador) > 0U);

    assert(!psm_scheduler_step(&planificador, 0U));
    assert(psm_scheduler_residue(&planificador) == 0U);

    /* Y al volver, la cuenta arranca limpia. */
    assert(!psm_scheduler_step(&planificador, 30U));
}

/*
 * Un cambio de demanda se sigue sin perder la cuenta.
 *
 * El controlador cambia la densidad en cada ejecución del lazo, de modo que el
 * caso normal es una demanda variable, no constante. Tras el cambio, la densidad
 * realizada debe corresponder a la nueva.
 */
static void test_a_changing_demand_is_followed(void)
{
    psm_scheduler_t planificador = {0};
    psm_scheduler_reset(&planificador);

    for (int i = 0; i < 1000; ++i) {
        (void)psm_scheduler_step(&planificador, 20U);
    }
    const uint32_t tras_veinte = psm_scheduler_emitted(&planificador);
    assert(tras_veinte == 200U);

    for (int i = 0; i < 1000; ++i) {
        (void)psm_scheduler_step(&planificador, 75U);
    }
    const uint32_t en_setenta_y_cinco = psm_scheduler_emitted(&planificador) - tras_veinte;
    /* El arrastre del residuo puede desplazar una sola actuación en la
     * transición; más allá de eso la densidad es la pedida. */
    assert(en_setenta_y_cinco >= 749U && en_setenta_y_cinco <= 751U);
}

/*
 * Cada actuación lleva las dos polaridades, empiece donde empiece.
 *
 * La bomba conduce solo en una polaridad. Decidiendo por semiciclo, las
 * densidades de periodo par dejaban todas las actuaciones en la misma, y la
 * bomba callaba un escalón entero según el instante de arranque (2026-10-10).
 * Aquí se recorre cada densidad con los dos desfases posibles respecto a la
 * polaridad y se exige que ambas reciban exactamente la mitad de los
 * semiciclos energizados: la que conduce nunca se queda sin ellos.
 */
static void test_every_actuation_covers_both_polarities(void)
{
    for (unsigned densidad = 1U; densidad <= 100U; ++densidad) {
        for (int desfase = 0; desfase < 2; ++desfase) {
            psm_scheduler_t planificador = {0};
            psm_scheduler_reset(&planificador);
            unsigned por_polaridad[2] = {0U, 0U};
            for (int semiciclo = 0; semiciclo < 2000; ++semiciclo) {
                if (psm_scheduler_half_cycle(&planificador, (uint8_t)densidad)) {
                    por_polaridad[(semiciclo + desfase) % 2]++;
                }
            }
            assert(por_polaridad[0] == por_polaridad[1]);
            /* Y la densidad, contada en ciclos, es la pedida sin deriva. */
            assert(por_polaridad[0] == densidad * 10U);
            assert(psm_scheduler_opportunities(&planificador) == 1000U);
        }
    }
}

/* Una densidad nula corta también a mitad de ciclo: no se termina lo empezado. */
static void test_zero_cuts_in_the_middle_of_a_cycle(void)
{
    psm_scheduler_t planificador = {0};
    psm_scheduler_reset(&planificador);
    assert(psm_scheduler_half_cycle(&planificador, 100U));
    assert(!psm_scheduler_half_cycle(&planificador, 0U));
    for (int i = 0; i < 1000; ++i) {
        assert(!psm_scheduler_half_cycle(&planificador, 0U));
    }
    assert(psm_scheduler_residue(&planificador) == 0U);
}

/* Reset deja el planificador como recién creado. */
static void test_reset_clears_every_counter(void)
{
    psm_scheduler_t planificador = {0};
    psm_scheduler_reset(&planificador);
    for (int i = 0; i < 500; ++i) {
        (void)psm_scheduler_step(&planificador, 37U);
    }
    assert(psm_scheduler_emitted(&planificador) > 0U);

    psm_scheduler_reset(&planificador);
    assert(psm_scheduler_emitted(&planificador) == 0U);
    assert(psm_scheduler_opportunities(&planificador) == 0U);
    assert(psm_scheduler_residue(&planificador) == 0U);
}

int main(void)
{
    test_zero_never_fires();
    test_full_scale_uses_every_opportunity();
    test_the_realised_density_matches_the_request_without_drift();
    test_actuations_are_spread_not_bursted();
    test_quantisation_rounds_clamps_and_refuses_the_impossible();
    test_an_out_of_scale_density_cannot_exceed_full_scale();
    test_asking_for_zero_discards_the_pending_residue();
    test_a_changing_demand_is_followed();
    test_every_actuation_covers_both_polarities();
    test_zero_cuts_in_the_middle_of_a_cycle();
    test_reset_clears_every_counter();
    printf("test_psm_scheduler: todas las comprobaciones pasaron\n");
    return 0;
}
