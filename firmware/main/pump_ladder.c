#include "pump_ladder.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t percent;
    stimulus_sequence_id_t sequence;
} ladder_step_t;

static const ladder_step_t s_steps[PUMP_LADDER_STEPS] = {
    {1U, STIMULUS_SEQUENCE_PUMP_STEP_1PCT_V1},
    {2U, STIMULUS_SEQUENCE_PUMP_STEP_2PCT_V1},
    {3U, STIMULUS_SEQUENCE_PUMP_STEP_3PCT_V1},
    {5U, STIMULUS_SEQUENCE_PUMP_STEP_5PCT_V1},
    {10U, STIMULUS_SEQUENCE_PUMP_STEP_10PCT_V1},
    {20U, STIMULUS_SEQUENCE_PUMP_STEP_20PCT_V1},
    {30U, STIMULUS_SEQUENCE_PUMP_STEP_30PCT_V1},
    {50U, STIMULUS_SEQUENCE_PUMP_STEP_50PCT_V1},
    {75U, STIMULUS_SEQUENCE_PUMP_STEP_75PCT_V1},
};

static const ladder_step_t s_thermal_steps[] = {
    {10U, STIMULUS_SEQUENCE_PUMP_THERMAL_10PCT_V1},
    {30U, STIMULUS_SEQUENCE_PUMP_THERMAL_30PCT_V1},
    {50U, STIMULUS_SEQUENCE_PUMP_THERMAL_50PCT_V1},
};

static const ladder_step_t s_pressure_tuning_steps[] = {
    {75U, STIMULUS_SEQUENCE_PUMP_TUNING_75PCT_V1},
};

static const ladder_step_t *steps_of(pump_ladder_variant_t variant, uint32_t *count)
{
    if (variant == PUMP_LADDER_PRESSURE_TUNING) {
        *count = sizeof(s_pressure_tuning_steps) / sizeof(s_pressure_tuning_steps[0]);
        return s_pressure_tuning_steps;
    }
    if (variant == PUMP_LADDER_THERMAL) {
        *count = sizeof(s_thermal_steps) / sizeof(s_thermal_steps[0]);
        return s_thermal_steps;
    }
    *count = PUMP_LADDER_STEPS;
    return s_steps;
}

pump_ladder_command_t pump_ladder_parse(const char *line)
{
    pump_ladder_command_t command = {0};
    if (line == NULL || strncmp(line, "B,", 2U) != 0) {
        return command;
    }
    char text[32];
    const size_t length = strcspn(line + 2, "\r\n");
    if (length >= sizeof(text)) {
        command.kind = PUMP_LADDER_COMMAND_INVALID;
        return command;
    }
    memcpy(text, line + 2, length);
    text[length] = '\0';
    if (strcmp(text, "show") == 0) {
        command.kind = PUMP_LADDER_COMMAND_SHOW;
        return command;
    }
    if (strncmp(text, "step,", 5U) == 0) {
        char *end = NULL;
        const long value = strtol(text + 5, &end, 10);
        /* Toda la línea tiene que ser el número: «20%» no es 20. */
        if (end != text + 5 && *end == '\0' && value >= 0L && value <= 100L) {
            command.kind = PUMP_LADDER_COMMAND_STEP;
            command.percent = (uint8_t)value;
            return command;
        }
    }
    command.kind = PUMP_LADDER_COMMAND_INVALID;
    return command;
}

void pump_ladder_init(pump_ladder_t *ladder)
{
    pump_ladder_init_variant(ladder, PUMP_LADDER_HYDRAULIC);
}

void pump_ladder_init_variant(pump_ladder_t *ladder, pump_ladder_variant_t variant)
{
    if (ladder != NULL) {
        *ladder = (pump_ladder_t){.configured = true, .variant = variant, .current = 0U};
    }
}

pump_ladder_result_t pump_ladder_select(pump_ladder_t *ladder, uint8_t percent,
                                        identification_phase_t phase,
                                        stimulus_sequence_id_t *sequence)
{
    if (ladder == NULL || sequence == NULL || !ladder->configured) {
        return PUMP_LADDER_REJECTED_NOT_CONFIGURED;
    }
    uint32_t count = 0U;
    const ladder_step_t *steps = steps_of(ladder->variant, &count);
    if (ladder->current >= count) {
        return PUMP_LADDER_REJECTED_NOT_CONFIGURED;
    }
    if (phase != IDENTIFICATION_FINISHED) {
        return PUMP_LADDER_REJECTED_NOT_FINISHED;
    }
    uint32_t index = 0U;
    while (index < count && steps[index].percent != percent) {
        index++;
    }
    if (index == count) {
        return PUMP_LADDER_REJECTED_NOT_IN_LADDER;
    }
    if (index != ladder->current && index != (uint32_t)ladder->current + 1U) {
        return PUMP_LADDER_REJECTED_SKIPS_A_STEP;
    }
    ladder->current = (uint8_t)index;
    *sequence = steps[index].sequence;
    return PUMP_LADDER_ACCEPTED;
}

bool pump_ladder_may_arm(operational_state_t state)
{
    switch (state) {
    case OP_STATE_SAFE_INACTIVE:
    case OP_STATE_COFFEE_HEATING:
    case OP_STATE_READY:
        return true;
    case OP_STATE_EXTRACTION:
    case OP_STATE_MANUAL_PURGE:
    case OP_STATE_ALARM:
    default:
        return false;
    }
}

uint8_t pump_ladder_current_percent(const pump_ladder_t *ladder)
{
    if (ladder == NULL || !ladder->configured) {
        return 0U;
    }
    uint32_t count = 0U;
    const ladder_step_t *steps = steps_of(ladder->variant, &count);
    return ladder->current < count ? steps[ladder->current].percent : 0U;
}

stimulus_sequence_id_t pump_ladder_first_sequence(void)
{
    return pump_ladder_variant_first_sequence(PUMP_LADDER_HYDRAULIC);
}

stimulus_sequence_id_t pump_ladder_variant_first_sequence(pump_ladder_variant_t variant)
{
    uint32_t count = 0U;
    return steps_of(variant, &count)[0].sequence;
}

const char *pump_ladder_result_name(pump_ladder_result_t result)
{
    switch (result) {
    case PUMP_LADDER_ACCEPTED:
        return "ok";
    case PUMP_LADDER_REJECTED_NOT_FINISHED:
        return "previous-not-finished";
    case PUMP_LADDER_REJECTED_NOT_IN_LADDER:
        return "not-in-ladder";
    case PUMP_LADDER_REJECTED_SKIPS_A_STEP:
        return "skips-a-step";
    case PUMP_LADDER_REJECTED_NOT_CONFIGURED:
    default:
        return "not-configured";
    }
}
