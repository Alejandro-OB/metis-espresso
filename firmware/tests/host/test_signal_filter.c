/* Verificación en host de la mediana móvil sobre señal validada. */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "signal_filter.h"

static bool cerca(float a, float b) { return fabsf(a - b) < 1e-4F; }

static void test_the_window_fills_progressively(void)
{
    signal_filter_t filtro;
    signal_filter_reset(&filtro);
    assert(signal_filter_count(&filtro) == 0U);
    float valor = 0.0F;
    assert(!signal_filter_value(&filtro, &valor));
    /* Desde la primera muestra ya hay salida: la señal no se retiene. */
    assert(cerca(signal_filter_push(&filtro, 93.0F), 93.0F));
    assert(signal_filter_count(&filtro) == 1U);
    assert(signal_filter_value(&filtro, &valor) && cerca(valor, 93.0F));
}

static void test_a_single_spike_does_not_move_the_output(void)
{
    /* Es la razón de elegir mediana: un impulso no arrastra el valor. */
    signal_filter_t filtro;
    signal_filter_reset(&filtro);
    for (int i = 0; i < 5; ++i) {
        signal_filter_push(&filtro, 93.0F);
    }
    const float con_impulso = signal_filter_push(&filtro, 300.0F);
    assert(cerca(con_impulso, 93.0F));
    /* Una media de la misma ventana se habría desplazado más de 40 °C. */
    const float media = (93.0F * 4.0F + 300.0F) / 5.0F;
    assert(media > 130.0F);
}

static void test_the_output_follows_a_sustained_change(void)
{
    signal_filter_t filtro;
    signal_filter_reset(&filtro);
    for (int i = 0; i < 5; ++i) {
        signal_filter_push(&filtro, 20.0F);
    }
    /* Tres muestras nuevas bastan para que la mediana adopte el valor nuevo. */
    signal_filter_push(&filtro, 90.0F);
    signal_filter_push(&filtro, 90.0F);
    assert(cerca(signal_filter_push(&filtro, 90.0F), 90.0F));
}

static void test_the_median_of_a_known_window(void)
{
    signal_filter_t filtro;
    signal_filter_reset(&filtro);
    const float entradas[] = {9.1F, 8.7F, 9.4F, 8.9F, 9.0F};
    float salida = 0.0F;
    for (size_t i = 0U; i < 5U; ++i) {
        salida = signal_filter_push(&filtro, entradas[i]);
    }
    assert(cerca(salida, 9.0F));   /* ordenadas: 8,7 8,9 9,0 9,1 9,4 */
}

static void test_reset_discards_the_window(void)
{
    /* Tras una lectura no apta la ventana se reinicia: no se promedia a través
       de un hueco, porque eso inventaría continuidad. */
    signal_filter_t filtro;
    signal_filter_reset(&filtro);
    for (int i = 0; i < 5; ++i) {
        signal_filter_push(&filtro, 93.0F);
    }
    signal_filter_reset(&filtro);
    assert(signal_filter_count(&filtro) == 0U);
    float valor = 0.0F;
    assert(!signal_filter_value(&filtro, &valor));
    assert(cerca(signal_filter_push(&filtro, 20.0F), 20.0F));
}

static void test_null_filter_is_transparent(void)
{
    assert(cerca(signal_filter_push(NULL, 42.0F), 42.0F));
    assert(signal_filter_count(NULL) == 0U);
}

int main(void)
{
    test_the_window_fills_progressively();
    test_a_single_spike_does_not_move_the_output();
    test_the_output_follows_a_sustained_change();
    test_the_median_of_a_known_window();
    test_reset_discards_the_window();
    test_null_filter_is_transparent();
    printf("PASS: mediana móvil sobre señal validada\n");
    return 0;
}
