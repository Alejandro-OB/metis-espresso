/*
 * Pruebas del filtro de primer orden del lazo (H-03).
 *
 * El filtro es la pieza que da al controlador un ancho de banda declarable. Lo
 * que se comprueba aquí no son valores concretos de una implementación, sino
 * las propiedades de las que depende una sintonía: ganancia unitaria en
 * continua, respuesta a escalón con la forma esperada, coeficiente acotado para
 * cualquier periodo y ausencia de transitorio falso al arrancar.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "control_filter.h"

static bool close_enough(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

/*
 * El coeficiente es el que derivan las decisiones, no uno cualquiera.
 *
 * ADR-007 § 3.3 deja escrito `α ≈ 0,167` para la presión con `τf = 50 ms` y
 * `Ts = 10 ms`. Comprobarlo aquí ata la implementación a la decisión: si
 * alguien cambiara la fórmula de discretización, esta prueba lo diría.
 */
static void test_alpha_matches_the_decided_values(void)
{
    control_filter_t presion = {0};
    assert(control_filter_configure(&presion, 0.050F, 0.010F));
    assert(close_enough(control_filter_alpha(&presion), 0.1667F, 0.0005F));

    /* ADR-009 § 3.6: temperatura con τf = 1 s y el PI cada 200 ms. */
    control_filter_t temperatura = {0};
    assert(control_filter_configure(&temperatura, 1.0F, 0.200F));
    assert(close_enough(control_filter_alpha(&temperatura), 0.1667F, 0.0005F));
}

/*
 * La primera muestra se adopta tal cual.
 *
 * Si el filtro arrancara en cero, un lazo que empieza con la caldera a 90 °C
 * vería una rampa desde 0 °C que no ocurrió. El controlador no puede distinguir
 * ese transitorio de una caída real, de modo que actuaría sobre una mentira.
 */
static void test_the_first_sample_is_adopted_without_transient(void)
{
    control_filter_t filtro = {0};
    assert(control_filter_configure(&filtro, 0.050F, 0.010F));

    float valor = -1.0F;
    assert(!control_filter_value(&filtro, &valor));  /* nada antes de la primera */

    assert(close_enough(control_filter_push(&filtro, 90.0F), 90.0F, 1e-6F));
    assert(control_filter_value(&filtro, &valor));
    assert(close_enough(valor, 90.0F, 1e-6F));
}

/* Una entrada constante converge exactamente a ella: el filtro no desplaza el
 * valor de régimen, solo limita con qué rapidez se llega. */
static void test_unity_gain_at_steady_state(void)
{
    control_filter_t filtro = {0};
    assert(control_filter_configure(&filtro, 0.050F, 0.010F));
    control_filter_push(&filtro, 0.0F);
    for (int i = 0; i < 500; ++i) {
        control_filter_push(&filtro, 9.0F);
    }
    float valor = 0.0F;
    assert(control_filter_value(&filtro, &valor));
    assert(close_enough(valor, 9.0F, 1e-3F));
}

/* La respuesta a un escalón unitario sigue `1 − (1 − α)^n`. */
static void test_step_response_follows_the_closed_form(void)
{
    control_filter_t filtro = {0};
    assert(control_filter_configure(&filtro, 0.050F, 0.010F));
    const float alpha = control_filter_alpha(&filtro);
    control_filter_push(&filtro, 0.0F);

    float esperado = 0.0F;
    for (int n = 1; n <= 20; ++n) {
        const float obtenido = control_filter_push(&filtro, 1.0F);
        esperado = 1.0F - powf(1.0F - alpha, (float)n);
        assert(close_enough(obtenido, esperado, 1e-5F));
    }
}

/*
 * Al cabo de una constante de tiempo, la salida tiende a `1 − 1/e`.
 *
 * La discretización no alcanza ese valor exactamente —con `Ts` comparable a
 * `τf` se queda corta—, pero converge a él conforme el periodo se acorta. Esa
 * convergencia es lo que justifica llamar «constante de tiempo» al parámetro, y
 * es la propiedad que se comprueba en lugar de un número fijo.
 */
static void test_it_approaches_the_continuous_time_constant(void)
{
    const float objetivo = 1.0F - expf(-1.0F);  /* ≈ 0,6321 */
    const float tau = 1.0F;
    float error_anterior = 1.0F;

    const int divisiones[] = {10, 100, 1000};
    for (unsigned d = 0U; d < sizeof(divisiones) / sizeof(divisiones[0]); ++d) {
        const int pasos = divisiones[d];
        const float ts = tau / (float)pasos;
        control_filter_t filtro = {0};
        assert(control_filter_configure(&filtro, tau, ts));
        control_filter_push(&filtro, 0.0F);
        for (int n = 0; n < pasos; ++n) {
            control_filter_push(&filtro, 1.0F);
        }
        float valor = 0.0F;
        assert(control_filter_value(&filtro, &valor));
        const float error = fabsf(valor - objetivo);
        assert(error < error_anterior);  /* cada refinamiento se acerca más */
        error_anterior = error;
    }
    assert(error_anterior < 0.001F);
}

/*
 * El coeficiente nunca supera la unidad, ni siquiera con un periodo mayor que
 * la constante de tiempo.
 *
 * Es la razón de usar Euler hacia atrás. Con la forma ingenua `α = Ts/τf`, un
 * periodo mayor que la constante daría `α > 1` y el filtro amplificaría y
 * oscilaría en lugar de suavizar. Como el periodo del lazo es un parámetro de
 * puesta en marcha, esa situación es alcanzable por configuración.
 */
static void test_alpha_stays_bounded_for_any_period(void)
{
    const float periodos[] = {0.001F, 0.010F, 0.050F, 0.500F, 5.0F, 100.0F};
    for (unsigned i = 0U; i < sizeof(periodos) / sizeof(periodos[0]); ++i) {
        control_filter_t filtro = {0};
        assert(control_filter_configure(&filtro, 0.050F, periodos[i]));
        const float alpha = control_filter_alpha(&filtro);
        assert(alpha > 0.0F && alpha <= 1.0F);
    }

    /* Con un periodo muy grande frente a la constante, el filtro deja pasar la
     * señal casi intacta, que es el comportamiento correcto: no hay banda que
     * limitar por debajo de la frecuencia de muestreo. */
    control_filter_t rapido = {0};
    assert(control_filter_configure(&rapido, 0.001F, 1.0F));
    control_filter_push(&rapido, 0.0F);
    assert(close_enough(control_filter_push(&rapido, 1.0F), 1.0F, 0.01F));
}

/* Sin constante de tiempo no hay filtrado, y se dice explícitamente en lugar de
 * dividir por cero. */
static void test_a_zero_time_constant_disables_filtering(void)
{
    control_filter_t filtro = {0};
    assert(control_filter_configure(&filtro, 0.0F, 0.010F));
    assert(close_enough(control_filter_alpha(&filtro), 1.0F, 1e-6F));
    control_filter_push(&filtro, 5.0F);
    assert(close_enough(control_filter_push(&filtro, 7.0F), 7.0F, 1e-6F));
}

/* Un periodo no utilizable se rechaza sin dejar la estructura a medias. */
static void test_an_unusable_configuration_is_refused(void)
{
    control_filter_t filtro = {0};
    assert(!control_filter_configure(&filtro, 0.050F, 0.0F));
    assert(!control_filter_configure(&filtro, 0.050F, -0.010F));
    assert(!control_filter_configure(&filtro, NAN, 0.010F));
    assert(!control_filter_configure(NULL, 0.050F, 0.010F));
}

/*
 * Reset olvida el estado y conserva el coeficiente.
 *
 * Es lo que ocurre cuando la señal deja de ser apta: al volver, el filtro se
 * ceba con la primera muestra buena en lugar de arrastrar un valor separado del
 * presente por un hueco de duración desconocida.
 */
static void test_reset_forgets_the_state_and_keeps_the_coefficient(void)
{
    control_filter_t filtro = {0};
    assert(control_filter_configure(&filtro, 0.050F, 0.010F));
    const float alpha = control_filter_alpha(&filtro);
    for (int i = 0; i < 50; ++i) {
        control_filter_push(&filtro, 9.0F);
    }
    control_filter_reset(&filtro);

    float valor = 0.0F;
    assert(!control_filter_value(&filtro, &valor));
    assert(close_enough(control_filter_alpha(&filtro), alpha, 1e-6F));
    assert(close_enough(control_filter_push(&filtro, 2.0F), 2.0F, 1e-6F));
}

/*
 * Una muestra no finita no corrompe el estado.
 *
 * `sensor_scaling` puede producir un no-número ante una lectura imposible. Si
 * ese valor entrara en la recursión, el filtro quedaría permanentemente
 * inutilizable y ningún dato posterior lo recuperaría. Se devuelve tal cual
 * —para que lo califique quien evalúa la calidad— y el estado no se toca.
 */
static void test_a_non_finite_sample_does_not_poison_the_filter(void)
{
    control_filter_t filtro = {0};
    assert(control_filter_configure(&filtro, 0.050F, 0.010F));
    for (int i = 0; i < 50; ++i) {
        control_filter_push(&filtro, 9.0F);
    }
    float antes = 0.0F;
    assert(control_filter_value(&filtro, &antes));

    assert(isnan(control_filter_push(&filtro, NAN)));
    assert(isinf(control_filter_push(&filtro, INFINITY)));

    float despues = 0.0F;
    assert(control_filter_value(&filtro, &despues));
    assert(close_enough(antes, despues, 1e-6F));
}

/* Lo que el filtro existe para hacer: una alternancia rápida se atenúa mucho
 * más que un cambio lento de la misma amplitud. */
static void test_it_attenuates_fast_alternation_more_than_slow_change(void)
{
    control_filter_t rapida = {0};
    assert(control_filter_configure(&rapida, 0.050F, 0.010F));
    control_filter_push(&rapida, 0.0F);
    float maximo = 0.0F;
    for (int i = 0; i < 100; ++i) {
        const float salida = control_filter_push(&rapida, (i % 2 == 0) ? 1.0F : -1.0F);
        if (fabsf(salida) > maximo) {
            maximo = fabsf(salida);
        }
    }
    /* La alternancia de ±1 en cada muestra queda muy por debajo de su amplitud. */
    assert(maximo < 0.2F);
}

int main(void)
{
    test_alpha_matches_the_decided_values();
    test_the_first_sample_is_adopted_without_transient();
    test_unity_gain_at_steady_state();
    test_step_response_follows_the_closed_form();
    test_it_approaches_the_continuous_time_constant();
    test_alpha_stays_bounded_for_any_period();
    test_a_zero_time_constant_disables_filtering();
    test_an_unusable_configuration_is_refused();
    test_reset_forgets_the_state_and_keeps_the_coefficient();
    test_a_non_finite_sample_does_not_poison_the_filter();
    test_it_attenuates_fast_alternation_more_than_slow_change();
    printf("test_control_filter: todas las comprobaciones pasaron\n");
    return 0;
}
