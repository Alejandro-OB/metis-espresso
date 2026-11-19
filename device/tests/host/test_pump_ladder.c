/*
 * Verificación en host de la escalera de la bomba en un solo binario
 * (enmienda a ADR-007 § 5 del 2026-10-10).
 *
 * Comprueba lo que la regla de «una secuencia por escalón» protegía: que no se
 * elige sin la anterior terminada, que no se salta un escalón, que se puede
 * repetir, que un aborto cierra la sesión y que rearmar no sube el tope.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "identification_mode.h"
#include "pump_ladder.h"

static void test_parses_only_its_own_lines(void)
{
    assert(pump_ladder_parse("A,show").kind == PUMP_LADDER_COMMAND_NONE);
    assert(pump_ladder_parse("B,show\n").kind == PUMP_LADDER_COMMAND_SHOW);
    const pump_ladder_command_t step = pump_ladder_parse("B,step,20\r\n");
    assert(step.kind == PUMP_LADDER_COMMAND_STEP && step.percent == 20U);
    assert(pump_ladder_parse("B,step,20%").kind == PUMP_LADDER_COMMAND_INVALID);
    assert(pump_ladder_parse("B,step,").kind == PUMP_LADDER_COMMAND_INVALID);
    assert(pump_ladder_parse("B,step,150").kind == PUMP_LADDER_COMMAND_INVALID);
    assert(pump_ladder_parse("B,jump,75").kind == PUMP_LADDER_COMMAND_INVALID);
}

static void test_climbs_one_step_at_a_time_and_may_repeat(void)
{
    pump_ladder_t ladder;
    pump_ladder_init(&ladder);
    stimulus_sequence_id_t id = STIMULUS_SEQUENCE_NONE;
    assert(pump_ladder_current_percent(&ladder) == 1U);
    assert(pump_ladder_select(&ladder, 1U, IDENTIFICATION_FINISHED, &id) == PUMP_LADDER_ACCEPTED);
    assert(id == STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1);
    assert(pump_ladder_select(&ladder, 3U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_SKIPS_A_STEP);
    assert(pump_ladder_select(&ladder, 2U, IDENTIFICATION_FINISHED, &id) == PUMP_LADDER_ACCEPTED);
    assert(id == STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1);
    assert(pump_ladder_select(&ladder, 75U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_SKIPS_A_STEP);
    assert(pump_ladder_select(&ladder, 4U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_NOT_IN_LADDER);
    /* Bajar tampoco: la escalera solo repite o sube. */
    assert(pump_ladder_select(&ladder, 1U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_SKIPS_A_STEP);
    assert(pump_ladder_current_percent(&ladder) == 2U);
}

/* La escalera térmica de la 7.9 sigue las mismas reglas con sus tres
 * escalones, y cada uno cabe en una extracción con margen: el perfil de la
 * placa corta a los 27 s, no a los 30 por omisión (2026-10-11). */
static void test_thermal_ladder_same_rules_and_fits_an_extraction(void)
{
    pump_ladder_t ladder;
    pump_ladder_init_variant(&ladder, PUMP_LADDER_THERMAL);
    assert(pump_ladder_variant_first_sequence(PUMP_LADDER_THERMAL) ==
           STIMULUS_SEQUENCE_PUMP_THERMAL_10PCT_V1);
    assert(pump_ladder_current_percent(&ladder) == 10U);
    stimulus_sequence_id_t id = STIMULUS_SEQUENCE_NONE;
    assert(pump_ladder_select(&ladder, 50U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_SKIPS_A_STEP);
    assert(pump_ladder_select(&ladder, 20U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_NOT_IN_LADDER);
    assert(pump_ladder_select(&ladder, 10U, IDENTIFICATION_FINISHED, &id) == PUMP_LADDER_ACCEPTED);
    assert(pump_ladder_select(&ladder, 30U, IDENTIFICATION_RUNNING, &id) ==
           PUMP_LADDER_REJECTED_NOT_FINISHED);
    assert(pump_ladder_select(&ladder, 30U, IDENTIFICATION_FINISHED, &id) == PUMP_LADDER_ACCEPTED);
    assert(id == STIMULUS_SEQUENCE_PUMP_THERMAL_30PCT_V1);
    assert(pump_ladder_select(&ladder, 50U, IDENTIFICATION_FINISHED, &id) == PUMP_LADDER_ACCEPTED);
    assert(id == STIMULUS_SEQUENCE_PUMP_THERMAL_50PCT_V1);
    assert(pump_ladder_current_percent(&ladder) == 50U);

    const stimulus_sequence_id_t thermal[] = {STIMULUS_SEQUENCE_PUMP_THERMAL_10PCT_V1,
                                              STIMULUS_SEQUENCE_PUMP_THERMAL_30PCT_V1,
                                              STIMULUS_SEQUENCE_PUMP_THERMAL_50PCT_V1};
    for (size_t i = 0U; i < sizeof(thermal) / sizeof(thermal[0]); ++i) {
        const stimulus_sequence_t *sequence = stimulus_sequence_find(thermal[i]);
        assert(sequence != NULL && stimulus_sequence_is_valid(sequence));
        assert(sequence->target == IDENTIFICATION_TARGET_PUMP);
        assert(stimulus_sequence_duration_ms(sequence) <= 25000U);
        /* Y termina en cero: la bomba no queda pidiendo al acabar. */
        assert(sequence->steps[sequence->step_count - 1U].demand_percent == 0U);
    }
    /* La hidráulica no cambia. */
    pump_ladder_t hydraulic;
    pump_ladder_init(&hydraulic);
    assert(pump_ladder_current_percent(&hydraulic) == 1U);
}

static void test_pressure_tuning_repeats_its_only_step(void)
{
    pump_ladder_t ladder;
    pump_ladder_init_variant(&ladder, PUMP_LADDER_PRESSURE_TUNING);
    assert(pump_ladder_variant_first_sequence(PUMP_LADDER_PRESSURE_TUNING) ==
           STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1);
    assert(pump_ladder_current_percent(&ladder) == 75U);
    stimulus_sequence_id_t id = STIMULUS_SEQUENCE_NONE;
    /* Cada prueba de sintonía es otra extracción con el mismo techo. */
    for (int shot = 0; shot < 3; ++shot) {
        assert(pump_ladder_select(&ladder, 75U, IDENTIFICATION_RUNNING, &id) ==
               PUMP_LADDER_REJECTED_NOT_FINISHED);
        id = STIMULUS_SEQUENCE_NONE;
        assert(pump_ladder_select(&ladder, 75U, IDENTIFICATION_FINISHED, &id) ==
               PUMP_LADDER_ACCEPTED);
        assert(id == STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1);
    }
    /* Ni otro techo, ni uno más alto. */
    assert(pump_ladder_select(&ladder, 50U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_NOT_IN_LADDER);
    assert(pump_ladder_select(&ladder, 100U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_NOT_IN_LADDER);

    const stimulus_sequence_t *sequence =
        stimulus_sequence_find(STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1);
    assert(sequence != NULL && stimulus_sequence_is_valid(sequence));
    assert(sequence->target == IDENTIFICATION_TARGET_PUMP);
    /* Cabe en los 27 s de extracción del perfil y termina en cero. */
    assert(stimulus_sequence_duration_ms(sequence) <= 25000U);
    assert(stimulus_sequence_peak_percent(sequence) == 75U);
    assert(sequence->steps[sequence->step_count - 1U].demand_percent == 0U);
}

static void test_never_chooses_without_a_finished_sequence(void)
{
    pump_ladder_t ladder;
    pump_ladder_init(&ladder);
    stimulus_sequence_id_t id = STIMULUS_SEQUENCE_NONE;
    const identification_phase_t not_finished[] = {
        IDENTIFICATION_DISABLED, IDENTIFICATION_AWAITING_CONFIRMATION, IDENTIFICATION_DECLINED,
        IDENTIFICATION_ARMED, IDENTIFICATION_RUNNING, IDENTIFICATION_ABORTED,
    };
    for (size_t index = 0U; index < sizeof(not_finished) / sizeof(not_finished[0]); index++) {
        assert(pump_ladder_select(&ladder, 2U, not_finished[index], &id) ==
               PUMP_LADDER_REJECTED_NOT_FINISHED);
    }
    assert(pump_ladder_current_percent(&ladder) == 1U);
    pump_ladder_t zeroed = {0};
    assert(pump_ladder_select(&zeroed, 1U, IDENTIFICATION_FINISHED, &id) ==
           PUMP_LADDER_REJECTED_NOT_CONFIGURED);
}

static void run_to_the_end(identification_mode_t *mode)
{
    const uint32_t total = stimulus_sequence_duration_ms(mode->sequence);
    for (uint32_t elapsed = 0U; elapsed <= total; elapsed += 50U) {
        (void)identification_mode_update(mode, ACTUATION_ALLOWED_IDENTIFICATION, 50U);
    }
    assert(mode->phase == IDENTIFICATION_FINISHED);
}

static void armed_mode(identification_mode_t *mode, float maximum)
{
    assert(identification_mode_configure(
               mode, stimulus_sequence_find(pump_ladder_first_sequence()), maximum, false) ==
           IDENTIFICATION_SETUP_ACCEPTED);
    mode->phase = IDENTIFICATION_ARMED;
}

static void test_rearming_needs_a_finished_sequence_and_keeps_the_ceiling(void)
{
    identification_mode_t mode;
    armed_mode(&mode, 75.0F);
    const stimulus_sequence_t *two = stimulus_sequence_find(STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1);
    assert(!identification_mode_rearm(&mode, two));  /* armada, no terminada */
    run_to_the_end(&mode);
    assert(identification_mode_rearm(&mode, two));
    assert(mode.phase == IDENTIFICATION_ARMED && mode.running_ms == 0U);
    assert(mode.sequence == two);

    /* El tope compilado manda: con 10 % el escalón del 20 % no se arma. */
    armed_mode(&mode, 10.0F);
    run_to_the_end(&mode);
    assert(!identification_mode_rearm(&mode,
                                      stimulus_sequence_find(STIMULUS_SEQUENCE_PUMP_STEP_20PCT_V1)));
    /* Ni otra salida: la escalera es de la bomba. */
    assert(!identification_mode_rearm(&mode,
                                      stimulus_sequence_find(STIMULUS_SEQUENCE_HEATER_STEP_1PCT_V1)));
}

static void test_arms_only_where_the_pump_waits_for_a_new_press(void)
{
    /* Una secuencia armada arranca en cuanto el guardián autoriza la bomba, y
     * la extracción dura más que la secuencia: armar con ella abierta
     * arrancaba el escalón sin pulsar café (2026-10-10, el 3 %). */
    assert(!pump_ladder_may_arm(OP_STATE_EXTRACTION));
    assert(!pump_ladder_may_arm(OP_STATE_MANUAL_PURGE));
    assert(!pump_ladder_may_arm(OP_STATE_ALARM));
    assert(pump_ladder_may_arm(OP_STATE_READY));
    assert(pump_ladder_may_arm(OP_STATE_COFFEE_HEATING));
    assert(pump_ladder_may_arm(OP_STATE_SAFE_INACTIVE));
    assert(!pump_ladder_may_arm((operational_state_t)99));

    /* La propiedad, con la FSM real: en ningún estado donde se pueda armar
     * está autorizada la bomba. */
    for (int state = OP_STATE_SAFE_INACTIVE; state <= OP_STATE_ALARM; state++) {
        operational_fsm_t fsm = {.state = (operational_state_t)state};
        const operational_authorization_t authorization = operational_fsm_authorization(&fsm);
        if (pump_ladder_may_arm((operational_state_t)state)) {
            assert(!authorization.extraction_pump_allowed);
            assert(!authorization.purge_pump_allowed);
        }
    }
}

static void test_an_abort_still_closes_the_session(void)
{
    identification_mode_t mode;
    armed_mode(&mode, 75.0F);
    (void)identification_mode_update(&mode, ACTUATION_ALLOWED_IDENTIFICATION, 50U);
    (void)identification_mode_update(&mode, ACTUATION_INHIBITED_NOT_AUTHORIZED, 50U);
    assert(mode.phase == IDENTIFICATION_ABORTED);
    assert(!identification_mode_rearm(&mode,
                                      stimulus_sequence_find(STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1)));
}

int main(void)
{
    test_parses_only_its_own_lines();
    test_climbs_one_step_at_a_time_and_may_repeat();
    test_thermal_ladder_same_rules_and_fits_an_extraction();
    test_pressure_tuning_repeats_its_only_step();
    test_never_chooses_without_a_finished_sequence();
    test_rearming_needs_a_finished_sequence_and_keeps_the_ceiling();
    test_arms_only_where_the_pump_waits_for_a_new_press();
    test_an_abort_still_closes_the_session();
    printf("PASS: escalera de la bomba en un solo binario\n");
    return 0;
}
