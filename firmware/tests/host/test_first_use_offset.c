/*
 * Pruebas de la compensación del primer uso (ADR-009, enmienda del 2026-10-19).
 *
 * Lo que no debe pasar: subir la caldera sin máximo medido, recortar una
 * petición a lo que cabe en vez de rechazarla, dejar la referencia sin la banda
 * de «lista» bajo el límite duro, compensar más de una preparación, que se la
 * lleve un café que ya estaba en curso al adoptarla, o que la máquina siga
 * «lista» con la caldera por debajo de la referencia recién subida.
 *
 * Los números son de prueba, con la forma de los del acta v6 (caldera ~101,9 °C,
 * límite duro 120 °C); ninguno es el máximo que mida el ensayo.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "first_use_offset.h"
#include "process_supervisor.h"

static const first_use_offset_bounds_t BOUNDS = {
    .maximum_c = 4.0F, .hard_limit_c = 120.0F, .margin_c = 0.5F};
static const float BOILER_C = 101.9F;

/* Una estructura a cero no aplica nada, y sin petición tampoco. */
static void test_zero_is_no_offset(void)
{
    first_use_offset_t state = {0};
    assert(state.phase == FIRST_USE_OFFSET_IDLE);
    assert(first_use_offset_applied_c(&state) == 0.0F);
    assert(first_use_offset_check(0.0F, &BOUNDS, BOILER_C) == FIRST_USE_OFFSET_NONE);
    assert(first_use_offset_check_allows(FIRST_USE_OFFSET_NONE));
    assert(!first_use_offset_check_allows(FIRST_USE_OFFSET_UNCHECKED));
    assert(first_use_offset_adopt(&state, 0.0F, &BOUNDS, BOILER_C) == FIRST_USE_OFFSET_NONE);
    assert(state.phase == FIRST_USE_OFFSET_IDLE);
    assert(first_use_offset_applied_c(&state) == 0.0F);
    assert(first_use_offset_check(1.0F, NULL, BOILER_C) == FIRST_USE_OFFSET_UNCHECKED);
}

/* Sin máximo —acta sin la sección— cualquier desplazamiento se rechaza, y un
 * máximo ilegible cuenta como ese cero. */
static void test_without_maximum_everything_is_refused(void)
{
    const float maxima[] = {0.0F, -1.0F, NAN, INFINITY};
    for (size_t i = 0U; i < sizeof(maxima) / sizeof(maxima[0]); ++i) {
        const first_use_offset_bounds_t bounds = {
            .maximum_c = maxima[i], .hard_limit_c = 120.0F, .margin_c = 0.5F};
        assert(first_use_offset_check(0.1F, &bounds, BOILER_C) ==
               FIRST_USE_OFFSET_ABOVE_MAXIMUM);
        assert(first_use_offset_check(0.0F, &bounds, BOILER_C) == FIRST_USE_OFFSET_NONE);
    }
}

/* Cada rechazo tiene su causa, y ninguno toca un estado ya armado de otra
 * forma que dejándolo inactivo: el perfil nuevo manda. */
static void test_refusals_have_their_cause(void)
{
    assert(first_use_offset_check(-0.5F, &BOUNDS, BOILER_C) == FIRST_USE_OFFSET_NEGATIVE);
    assert(first_use_offset_check(NAN, &BOUNDS, BOILER_C) == FIRST_USE_OFFSET_NOT_FINITE);
    assert(first_use_offset_check(INFINITY, &BOUNDS, BOILER_C) ==
           FIRST_USE_OFFSET_NOT_FINITE);
    assert(first_use_offset_check(4.01F, &BOUNDS, BOILER_C) ==
           FIRST_USE_OFFSET_ABOVE_MAXIMUM);
    assert(first_use_offset_check(4.0F, &BOUNDS, BOILER_C) == FIRST_USE_OFFSET_ACCEPTED);

    first_use_offset_t state = {0};
    assert(first_use_offset_adopt(&state, 3.0F, &BOUNDS, BOILER_C) ==
           FIRST_USE_OFFSET_ACCEPTED);
    assert(first_use_offset_applied_c(&state) == 3.0F);
    assert(first_use_offset_adopt(&state, 9.0F, &BOUNDS, BOILER_C) ==
           FIRST_USE_OFFSET_ABOVE_MAXIMUM);
    /* No se recorta a 4: se queda sin desplazamiento. */
    assert(state.phase == FIRST_USE_OFFSET_IDLE);
    assert(first_use_offset_applied_c(&state) == 0.0F);
}

/* La referencia subida conserva la desigualdad estricta de
 * `process_limits_are_valid`: lo que aquí se admite, el supervisor lo admite. */
static void test_hard_limit_keeps_the_ready_band(void)
{
    const first_use_offset_bounds_t wide = {
        .maximum_c = 10.0F, .hard_limit_c = 105.0F, .margin_c = 0.5F};
    /* 101,9 + 2,6 + 0,5 = 105: no queda por debajo. */
    assert(first_use_offset_check(2.6F, &wide, BOILER_C) ==
           FIRST_USE_OFFSET_ABOVE_HARD_LIMIT);
    assert(first_use_offset_check(2.5F, &wide, BOILER_C) == FIRST_USE_OFFSET_ACCEPTED);

    process_limits_t limits = process_limits_default();
    limits.hard_overtemperature_c = wide.hard_limit_c;
    limits.ready_tolerance_c = wide.margin_c;
    assert(process_limits_apply_profile(&limits, BOILER_C + 2.5F, 30000U));
    assert(!process_limits_apply_profile(&limits, BOILER_C + 2.6F, 30000U));

    const first_use_offset_bounds_t broken = {
        .maximum_c = 10.0F, .hard_limit_c = NAN, .margin_c = 0.5F};
    assert(first_use_offset_check(1.0F, &broken, BOILER_C) ==
           FIRST_USE_OFFSET_ABOVE_HARD_LIMIT);
}

/* Un solo uso: rige desde que se adopta hasta que termina la preparación
 * siguiente, y entonces vuelve a cero aunque nadie lo retire. */
static void test_it_lasts_exactly_one_preparation(void)
{
    first_use_offset_t state = {0};
    assert(first_use_offset_adopt(&state, 3.0F, &BOUNDS, BOILER_C) ==
           FIRST_USE_OFFSET_ACCEPTED);
    const operational_state_t waiting[] = {
        OP_STATE_READY, OP_STATE_COFFEE_HEATING, OP_STATE_MANUAL_PURGE,
        OP_STATE_SAFE_INACTIVE, OP_STATE_READY};
    for (size_t i = 0U; i < sizeof(waiting) / sizeof(waiting[0]); ++i) {
        /* Ni los purgados ni apagar y encender lo consumen. */
        assert(!first_use_offset_observe(&state, waiting[i]));
        assert(first_use_offset_applied_c(&state) == 3.0F);
    }
    assert(!first_use_offset_observe(&state, OP_STATE_EXTRACTION));
    assert(state.phase == FIRST_USE_OFFSET_BREWING);
    for (int i = 0; i < 100; ++i) {
        assert(!first_use_offset_observe(&state, OP_STATE_EXTRACTION));
        assert(first_use_offset_applied_c(&state) == 3.0F);
    }
    /* Termina: vuelve a cero en ese ciclo, y solo lo avisa una vez. */
    assert(first_use_offset_observe(&state, OP_STATE_READY));
    assert(first_use_offset_applied_c(&state) == 0.0F);
    assert(!first_use_offset_observe(&state, OP_STATE_READY));
    /* El café siguiente ya no lo lleva. */
    assert(!first_use_offset_observe(&state, OP_STATE_EXTRACTION));
    assert(first_use_offset_applied_c(&state) == 0.0F);
    assert(!first_use_offset_observe(&state, OP_STATE_READY));
}

/* Una preparación cortada por una protección también lo consume. */
static void test_an_interrupted_preparation_also_consumes_it(void)
{
    first_use_offset_t state = {0};
    (void)first_use_offset_adopt(&state, 2.0F, &BOUNDS, BOILER_C);
    (void)first_use_offset_observe(&state, OP_STATE_READY);
    (void)first_use_offset_observe(&state, OP_STATE_EXTRACTION);
    assert(first_use_offset_observe(&state, OP_STATE_ALARM));
    assert(first_use_offset_applied_c(&state) == 0.0F);
}

/* Un café que ya estaba en curso al adoptarlo no se lo lleva: lo consume el
 * siguiente que empiece. */
static void test_a_preparation_already_running_does_not_take_it(void)
{
    first_use_offset_t state = {0};
    (void)first_use_offset_adopt(&state, 2.0F, &BOUNDS, BOILER_C);
    assert(!first_use_offset_observe(&state, OP_STATE_EXTRACTION));
    assert(state.phase == FIRST_USE_OFFSET_ARMED);
    assert(!first_use_offset_observe(&state, OP_STATE_READY));
    assert(first_use_offset_applied_c(&state) == 2.0F);
    assert(!first_use_offset_observe(&state, OP_STATE_EXTRACTION));
    assert(state.phase == FIRST_USE_OFFSET_BREWING);
    assert(first_use_offset_observe(&state, OP_STATE_READY));
}

/* Una adopción nueva sustituye a la pendiente, también en mitad de una
 * preparación: el perfil nuevo es el que manda. */
static void test_a_new_adoption_replaces_the_pending_one(void)
{
    first_use_offset_t state = {0};
    (void)first_use_offset_adopt(&state, 2.0F, &BOUNDS, BOILER_C);
    (void)first_use_offset_observe(&state, OP_STATE_READY);
    assert(first_use_offset_adopt(&state, 3.5F, &BOUNDS, BOILER_C) ==
           FIRST_USE_OFFSET_ACCEPTED);
    assert(first_use_offset_applied_c(&state) == 3.5F);
    assert(first_use_offset_adopt(&state, 0.0F, &BOUNDS, BOILER_C) == FIRST_USE_OFFSET_NONE);
    assert(first_use_offset_applied_c(&state) == 0.0F);
}

/* «Lista» se declara contra la referencia subida: con la caldera por debajo
 * de referencia − banda, una máquina lista deja de estarlo; ya arriba, no. */
static void test_ready_is_withdrawn_until_the_boiler_arrives(void)
{
    process_limits_t limits = process_limits_default();
    limits.hard_overtemperature_c = 120.0F;
    assert(process_limits_apply_profile(&limits, BOILER_C + 1.5F, 30000U));
    process_observation_t observation = {
        .temperature_c = BOILER_C, .temperature_usable = true};
    /* Con 1,5 °C de subida la histéresis de «lista» (2 °C) no lo vería. */
    assert(observation.temperature_c >= limits.coffee_setpoint_c - limits.lost_delta_c);
    assert(process_supervisor_ready_withdrawn(&limits, &observation, OP_STATE_READY));
    /* Solo en «lista»: calentando ya espera a la referencia nueva. */
    assert(!process_supervisor_ready_withdrawn(&limits, &observation,
                                               OP_STATE_COFFEE_HEATING));
    assert(!process_supervisor_ready_withdrawn(&limits, &observation, OP_STATE_EXTRACTION));
    /* Sin medida apta no decide: eso es de las protecciones. */
    observation.temperature_usable = false;
    assert(!process_supervisor_ready_withdrawn(&limits, &observation, OP_STATE_READY));
    observation.temperature_usable = true;
    /* Con la caldera ya en la banda sigue lista. */
    observation.temperature_c = limits.coffee_setpoint_c - limits.ready_tolerance_c;
    assert(!process_supervisor_ready_withdrawn(&limits, &observation, OP_STATE_READY));
    assert(!process_supervisor_ready_withdrawn(NULL, &observation, OP_STATE_READY));
    assert(!process_supervisor_ready_withdrawn(&limits, NULL, OP_STATE_READY));
}

/* El refuerzo (enmienda del 2026-10-27): números de prueba con la forma del
 * acta, ganancia 0,75 y máximo 1,25; ninguno es el que firme el acta. */
static const first_use_offset_bounds_t REINFORCED = {
    .maximum_c = 4.0F, .hard_limit_c = 120.0F, .margin_c = 0.5F,
    .feedforward_base_gain = 0.75F, .feedforward_maximum_gain = 1.25F};

/* Sin petición, o con la ganancia del acta, no hay nada que reforzar; sin
 * máximo en el acta, ninguna ganancia mayor se admite. */
static void test_feedforward_without_request_or_maximum(void)
{
    assert(first_use_feedforward_check(0.0F, &REINFORCED) == FIRST_USE_FEEDFORWARD_NONE);
    assert(first_use_feedforward_check(0.75F, &REINFORCED) == FIRST_USE_FEEDFORWARD_NONE);
    assert(first_use_feedforward_check(1.0F, NULL) == FIRST_USE_FEEDFORWARD_UNCHECKED);
    assert(!first_use_feedforward_check_allows(FIRST_USE_FEEDFORWARD_UNCHECKED));
    /* Las pruebas de la subida no declaran máximo de refuerzo: nada pasa. */
    assert(first_use_feedforward_check(1.0F, &BOUNDS) == FIRST_USE_FEEDFORWARD_ABOVE_MAXIMUM);
    const float maxima[] = {0.0F, 0.75F, 0.5F, NAN, INFINITY};
    for (size_t i = 0U; i < sizeof(maxima) / sizeof(maxima[0]); ++i) {
        first_use_offset_bounds_t bounds = REINFORCED;
        bounds.feedforward_maximum_gain = maxima[i];
        assert(first_use_feedforward_check(0.8F, &bounds) ==
               FIRST_USE_FEEDFORWARD_ABOVE_MAXIMUM);
    }
}

/* Cada rechazo con su causa, y sin recortar: 1,3 no se vuelve 1,25. */
static void test_feedforward_refusals_have_their_cause(void)
{
    assert(first_use_feedforward_check(1.25F, &REINFORCED) == FIRST_USE_FEEDFORWARD_ACCEPTED);
    assert(first_use_feedforward_check(1.3F, &REINFORCED) ==
           FIRST_USE_FEEDFORWARD_ABOVE_MAXIMUM);
    assert(first_use_feedforward_check(0.5F, &REINFORCED) == FIRST_USE_FEEDFORWARD_BELOW_BASE);
    assert(first_use_feedforward_check(-1.0F, &REINFORCED) == FIRST_USE_FEEDFORWARD_BELOW_BASE);
    assert(first_use_feedforward_check(NAN, &REINFORCED) == FIRST_USE_FEEDFORWARD_NOT_FINITE);
    assert(first_use_feedforward_check(INFINITY, &REINFORCED) ==
           FIRST_USE_FEEDFORWARD_NOT_FINITE);
}

/* El refuerzo solo, sin subida, arma y dura una preparación, como la subida;
 * la referencia no cambia. */
static void test_feedforward_alone_lasts_one_preparation(void)
{
    first_use_offset_t state = {0};
    first_use_feedforward_check_t gain = FIRST_USE_FEEDFORWARD_UNCHECKED;
    assert(first_use_offset_adopt_request(&state, 0.0F, 1.25F, &REINFORCED, BOILER_C, &gain) ==
           FIRST_USE_OFFSET_NONE);
    assert(gain == FIRST_USE_FEEDFORWARD_ACCEPTED);
    assert(state.phase == FIRST_USE_OFFSET_ARMED);
    assert(first_use_offset_applied_c(&state) == 0.0F);
    assert(first_use_offset_feedforward_gain(&state) == 1.25F);
    assert(!first_use_offset_observe(&state, OP_STATE_MANUAL_PURGE));
    assert(!first_use_offset_observe(&state, OP_STATE_READY));
    assert(!first_use_offset_observe(&state, OP_STATE_EXTRACTION));
    assert(first_use_offset_feedforward_gain(&state) == 1.25F);
    assert(first_use_offset_observe(&state, OP_STATE_READY));
    assert(first_use_offset_feedforward_gain(&state) == 0.0F);
    /* El café siguiente vuelve a la del acta. */
    assert(!first_use_offset_observe(&state, OP_STATE_EXTRACTION));
    assert(first_use_offset_feedforward_gain(&state) == 0.0F);
}

/* Los dos juntos se adoptan juntos; si uno no se admite, ninguno queda. */
static void test_offset_and_feedforward_are_all_or_nothing(void)
{
    first_use_offset_t state = {0};
    assert(first_use_offset_adopt_request(&state, 2.0F, 1.25F, &REINFORCED, BOILER_C, NULL) ==
           FIRST_USE_OFFSET_ACCEPTED);
    assert(first_use_offset_applied_c(&state) == 2.0F);
    assert(first_use_offset_feedforward_gain(&state) == 1.25F);

    first_use_feedforward_check_t gain = FIRST_USE_FEEDFORWARD_UNCHECKED;
    assert(first_use_offset_adopt_request(&state, 2.0F, 1.3F, &REINFORCED, BOILER_C, &gain) ==
           FIRST_USE_OFFSET_ACCEPTED);
    assert(gain == FIRST_USE_FEEDFORWARD_ABOVE_MAXIMUM);
    assert(state.phase == FIRST_USE_OFFSET_IDLE);
    assert(first_use_offset_applied_c(&state) == 0.0F);
    assert(first_use_offset_feedforward_gain(&state) == 0.0F);

    assert(first_use_offset_adopt_request(&state, 9.0F, 1.25F, &REINFORCED, BOILER_C, &gain) ==
           FIRST_USE_OFFSET_ABOVE_MAXIMUM);
    assert(gain == FIRST_USE_FEEDFORWARD_ACCEPTED);
    assert(state.phase == FIRST_USE_OFFSET_IDLE);
    assert(first_use_offset_feedforward_gain(&state) == 0.0F);

    /* La adopción de antes, sin ganancia, no arma ningún refuerzo. */
    assert(first_use_offset_adopt(&state, 2.0F, &REINFORCED, BOILER_C) ==
           FIRST_USE_OFFSET_ACCEPTED);
    assert(first_use_offset_feedforward_gain(&state) == 0.0F);
}

static void test_names(void)
{
    assert(first_use_offset_check_name(FIRST_USE_OFFSET_ABOVE_MAXIMUM)[0] != '\0');
    assert(first_use_offset_phase_name(FIRST_USE_OFFSET_BREWING)[0] != '\0');
    assert(first_use_feedforward_check_name(FIRST_USE_FEEDFORWARD_BELOW_BASE)[0] != '\0');
}

int main(void)
{
    test_zero_is_no_offset();
    test_without_maximum_everything_is_refused();
    test_refusals_have_their_cause();
    test_hard_limit_keeps_the_ready_band();
    test_it_lasts_exactly_one_preparation();
    test_an_interrupted_preparation_also_consumes_it();
    test_a_preparation_already_running_does_not_take_it();
    test_a_new_adoption_replaces_the_pending_one();
    test_ready_is_withdrawn_until_the_boiler_arrives();
    test_feedforward_without_request_or_maximum();
    test_feedforward_refusals_have_their_cause();
    test_feedforward_alone_lasts_one_preparation();
    test_offset_and_feedforward_are_all_or_nothing();
    test_names();
    printf("first_use_offset: 14 casos correctos\n");
    return 0;
}
