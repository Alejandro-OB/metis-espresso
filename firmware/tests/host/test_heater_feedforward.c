/*
 * Pruebas de la prealimentación del calentador `ff-v1` (ADR-009 § 3.4).
 *
 * Lo que importa no es la multiplicación, sino lo que no debe pasar: que
 * aporte con la bomba parada, que una ganancia imposible quede activa, que una
 * estructura sin configurar caliente o que siga calentando cuando no entra
 * agua.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "heater_feedforward.h"

/* Sin configurar, o con ganancia cero, no aporta nada: el calentador queda como
 * estaba antes de existir este módulo. */
static void test_unconfigured_or_zero_gain_adds_nothing(void)
{
    const heater_feedforward_t zeroed = {0};
    for (unsigned density = 0U; density <= 100U; ++density) {
        assert(heater_feedforward_output(&zeroed, (uint8_t)density, true) == 0.0F);
    }
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.0F));
    assert(heater_feedforward_output(&ff, 75U, true) == 0.0F);
    assert(heater_feedforward_output(NULL, 75U, true) == 0.0F);
}

/* Con la bomba sin autorizar o en cero, cero: la prealimentación no se adelanta
 * a la bomba ni se queda después de ella. */
static void test_nothing_without_an_authorised_pump(void)
{
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.1F));
    assert(heater_feedforward_output(&ff, 50U, false) == 0.0F);
    assert(heater_feedforward_output(&ff, 0U, true) == 0.0F);
}

/* Proporcional a la densidad, y la densidad nunca cuenta más que el 100 %. */
static void test_proportional_and_bounded_by_full_density(void)
{
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.08F));
    assert(fabsf(heater_feedforward_output(&ff, 10U, true) - 0.8F) < 1e-6F);
    assert(fabsf(heater_feedforward_output(&ff, 50U, true) - 4.0F) < 1e-6F);
    assert(heater_feedforward_output(&ff, 255U, true) == heater_feedforward_output(&ff, 100U, true));
}

/* Una ganancia que ninguna medida puede dar se rechaza y deja la
 * prealimentación apagada, también si antes había una válida. */
static void test_impossible_gains_are_refused_and_switch_it_off(void)
{
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.1F));
    const float imposibles[] = {-0.01F, NAN, INFINITY};
    for (size_t i = 0U; i < sizeof(imposibles) / sizeof(imposibles[0]); ++i) {
        assert(!heater_feedforward_configure(&ff, imposibles[i]));
        assert(heater_feedforward_output(&ff, 100U, true) == 0.0F);
        assert(heater_feedforward_configure(&ff, 0.1F));
    }
    /* Una ganancia medida puede pedir más del 100 % con la bomba a fondo
     * (7.9: 1,15 % por punto); recortarla es tarea de la saturación del PI. */
    assert(heater_feedforward_configure(&ff, 1.15F));
    assert(fabsf(heater_feedforward_output(&ff, 100U, true) - 115.0F) < 1e-3F);
}

/* Puerta 2: sin aportación, la salida del PI pasa intacta; con aportación,
 * se suma por encima de su tope y nunca pasa del 100 %. */
static void test_gate_two_adds_above_the_pi_ceiling_and_never_past_full(void)
{
    for (float pi = 0.0F; pi <= 10.0F; pi += 0.5F) {
        assert(heater_feedforward_apply(pi, 0.0F) == pi);
        assert(heater_feedforward_apply(pi, -3.0F) == pi);
        assert(heater_feedforward_apply(pi, NAN) == pi);
        assert(fabsf(heater_feedforward_apply(pi, 34.5F) - (pi + 34.5F)) < 1e-4F);
        assert(heater_feedforward_apply(pi, 115.0F) == 100.0F);
    }
    assert(heater_feedforward_apply(NAN, 20.0F) == 0.0F);
    assert(heater_feedforward_apply(-1.0F, 20.0F) == 0.0F);
    /* Al parar la bomba la puerta se cierra en el mismo ciclo. */
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 1.15F));
    assert(heater_feedforward_apply(10.0F, heater_feedforward_output(&ff, 30U, true)) > 40.0F);
    assert(heater_feedforward_apply(10.0F, heater_feedforward_output(&ff, 30U, false)) == 10.0F);
}


/* Tolerancia con que el supervisor declara «lista» (`process_limits_default`,
 * `ready_tolerance_c`), la que recibe el corte en la placa. */
#define BAND_C 0.5F

/* Metal filtrado, un valor por segundo, de dos bombeos registrados en
 * `registros/arrastre-20261011/maquina.csv`: el de 01:12:16 del 2026-10-17,
 * con el depósito vacío, y el café de 07:53:48 del mismo día, con densidad 75 %
 * en los dos. */
static const float DRY_RUN_C[] = {92.58F, 92.48F, 92.48F, 92.41F, 92.27F, 92.03F, 91.88F,
                                  91.66F, 91.45F, 91.28F, 91.16F, 91.12F, 91.10F, 91.17F,
                                  91.37F, 91.54F, 91.97F, 92.21F, 92.65F, 93.28F, 93.89F,
                                  94.40F, 95.19F, 95.91F, 96.67F, 97.44F, 98.25F, 98.69F};
static const float COFFEE_C[] = {92.39F, 92.38F, 92.46F, 92.29F, 92.08F, 91.60F, 91.04F,
                                 90.40F, 89.65F, 88.81F, 87.95F, 87.30F, 86.42F, 85.91F,
                                 85.37F, 84.85F, 84.39F, 84.03F, 83.67F, 83.38F, 83.08F,
                                 82.89F, 82.67F, 82.42F, 82.22F, 82.02F, 81.86F, 81.77F};

/* Con agua, el café entero recibe la aportación; en seco se corta en el
 * primer segundo que el metal pasa de 92,58 + 0,5 °C (93,28, a los 19 s) y no
 * vuelve aunque siga la bomba. */
static void test_recorded_dry_run_is_cut_and_coffee_is_not(void)
{
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.75F));
    for (size_t i = 0U; i < sizeof(COFFEE_C) / sizeof(COFFEE_C[0]); ++i) {
        assert(fabsf(heater_feedforward_step(&ff, 75U, true, COFFEE_C[i], BAND_C) - 56.25F) <
               1e-3F);
    }
    assert(!heater_feedforward_no_water(&ff));
    assert(heater_feedforward_step(&ff, 0U, false, 82.0F, BAND_C) == 0.0F);

    size_t cut_at = 0U;
    for (size_t i = 0U; i < sizeof(DRY_RUN_C) / sizeof(DRY_RUN_C[0]); ++i) {
        const float out = heater_feedforward_step(&ff, 75U, true, DRY_RUN_C[i], BAND_C);
        if (out == 0.0F && cut_at == 0U) {
            cut_at = i;
        }
        assert(cut_at == 0U ? out > 56.0F : out == 0.0F);
    }
    assert(cut_at == 19U);
    assert(heater_feedforward_no_water(&ff));
}

/* El corte dura lo que el bombeo: el siguiente arranque toma su propio valor
 * al pulsar y vuelve a aportar. */
static void test_cut_lasts_one_pumping_only(void)
{
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.75F));
    (void)heater_feedforward_step(&ff, 75U, true, 92.0F, BAND_C);
    assert(heater_feedforward_step(&ff, 75U, true, 92.7F, BAND_C) == 0.0F);
    assert(heater_feedforward_step(&ff, 75U, true, 91.0F, BAND_C) == 0.0F);
    assert(heater_feedforward_step(&ff, 75U, false, 95.0F, BAND_C) == 0.0F);
    assert(!heater_feedforward_no_water(&ff));
    assert(heater_feedforward_step(&ff, 75U, true, 95.0F, BAND_C) > 56.0F);
    assert(heater_feedforward_step(&ff, 75U, true, 95.5F, BAND_C) > 56.0F);
}

/* Una pausa de densidad cero no da aportación ni toma otra vez el valor al
 * pulsar: si lo hiciera, el metal ya caliente de la pausa aflojaría el corte. */
static void test_zero_density_pause_keeps_the_start_value(void)
{
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.75F));
    (void)heater_feedforward_step(&ff, 75U, true, 92.0F, BAND_C);
    assert(heater_feedforward_step(&ff, 0U, true, 92.5F, BAND_C) == 0.0F);
    assert(heater_feedforward_step(&ff, 55U, true, 92.5F, BAND_C) > 41.0F);
    assert(heater_feedforward_step(&ff, 55U, true, 92.61F, BAND_C) == 0.0F);
}

/* Lo que no se puede comprobar corta: una medida no finita o una tolerancia
 * sin sentido. Una estructura a cero sigue sin aportar. */
static void test_what_cannot_be_checked_cuts(void)
{
    heater_feedforward_t ff;
    assert(heater_feedforward_configure(&ff, 0.75F));
    assert(heater_feedforward_step(&ff, 75U, true, NAN, BAND_C) == 0.0F);
    assert(heater_feedforward_step(&ff, 75U, true, 92.0F, BAND_C) == 0.0F);
    heater_feedforward_reset(&ff);
    const float limits[] = {0.0F, -0.6F, NAN, INFINITY};
    for (size_t i = 0U; i < sizeof(limits) / sizeof(limits[0]); ++i) {
        heater_feedforward_reset(&ff);
        assert(heater_feedforward_step(&ff, 75U, true, 92.0F, limits[i]) == 0.0F);
    }
    heater_feedforward_t zeroed = {0};
    assert(heater_feedforward_step(&zeroed, 75U, true, 92.0F, BAND_C) == 0.0F);
    assert(heater_feedforward_step(NULL, 75U, true, 92.0F, BAND_C) == 0.0F);
    assert(!heater_feedforward_no_water(NULL));
}

/* La ganancia de cada ciclo (enmienda del 2026-10-27): la reforzada solo si
 * es legible y mayor que la del acta; si no, la del acta. */
static void test_reinforced_gain_only_raises(void)
{
    assert(heater_feedforward_gain_for(0.75F, 0.0F) == 0.75F);
    assert(heater_feedforward_gain_for(0.75F, 1.25F) == 1.25F);
    assert(heater_feedforward_gain_for(0.75F, 0.5F) == 0.75F);
    assert(heater_feedforward_gain_for(0.75F, NAN) == 0.75F);
    assert(heater_feedforward_gain_for(0.75F, INFINITY) == 0.75F);
    assert(heater_feedforward_gain_for(NAN, 1.25F) == 1.25F);
    assert(heater_feedforward_gain_for(-1.0F, 0.0F) == 0.0F);
    /* Con la reforzada, la aportación sigue limitada al 100 % por la suma. */
    heater_feedforward_t feedforward = {0};
    assert(heater_feedforward_configure(&feedforward, heater_feedforward_gain_for(0.75F, 1.25F)));
    assert(heater_feedforward_apply(10.0F, heater_feedforward_output(&feedforward, 100U, true)) ==
           100.0F);
}

int main(void)
{
    test_gate_two_adds_above_the_pi_ceiling_and_never_past_full();
    test_unconfigured_or_zero_gain_adds_nothing();
    test_nothing_without_an_authorised_pump();
    test_proportional_and_bounded_by_full_density();
    test_impossible_gains_are_refused_and_switch_it_off();
    test_recorded_dry_run_is_cut_and_coffee_is_not();
    test_cut_lasts_one_pumping_only();
    test_zero_density_pause_keeps_the_start_value();
    test_what_cannot_be_checked_cuts();
    test_reinforced_gain_only_raises();
    printf("PASS: prealimentación del calentador ff-v1\n");
    return 0;
}
