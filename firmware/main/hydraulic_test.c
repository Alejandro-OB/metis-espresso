#include "hydraulic_test.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

bool hydraulic_test_configure(hydraulic_test_t *test,
                              const uint8_t orders[HYDRAULIC_TEST_ORDERS],
                              float ceiling_percent)
{
    if (test == NULL) {
        return false;
    }
    *test = (hydraulic_test_t){0};
    if (orders == NULL || !(ceiling_percent > 0.0F && ceiling_percent <= 100.0F)) {
        return false;
    }
    for (uint32_t i = 0U; i < HYDRAULIC_TEST_ORDERS; i++) {
        if (orders[i] == 0U || (float)orders[i] > ceiling_percent) {
            return false;
        }
        for (uint32_t j = 0U; j < i; j++) {
            if (orders[j] == orders[i]) {
                return false;
            }
        }
    }
    memcpy(test->orders, orders, sizeof(test->orders));
    test->configured = true;
    return true;
}

hydraulic_test_command_t hydraulic_test_parse(const char *line)
{
    hydraulic_test_command_t command = {0};
    if (line == NULL || strncmp(line, "H,", 2U) != 0) {
        return command;
    }
    char text[32];
    const size_t length = strcspn(line + 2, "\r\n");
    if (length >= sizeof(text)) {
        command.kind = HYDRAULIC_TEST_COMMAND_INVALID;
        return command;
    }
    memcpy(text, line + 2, length);
    text[length] = '\0';
    if (strcmp(text, "show") == 0) {
        command.kind = HYDRAULIC_TEST_COMMAND_SHOW;
        return command;
    }
    if (strncmp(text, "order,", 6U) == 0) {
        char *end = NULL;
        const long value = strtol(text + 6, &end, 10);
        /* Toda la línea tiene que ser el número: «20%» no es 20. */
        if (end != text + 6 && *end == '\0' && value >= 0L && value <= 100L) {
            command.kind = HYDRAULIC_TEST_COMMAND_ORDER;
            command.percent = (uint8_t)value;
            return command;
        }
    }
    command.kind = HYDRAULIC_TEST_COMMAND_INVALID;
    return command;
}

/* Los estados que no autorizan la bomba: elegir ahí no cambia ninguna corrida
 * en curso. */
static bool pump_idle(operational_state_t state)
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

hydraulic_test_result_t hydraulic_test_select(hydraulic_test_t *test, uint8_t percent,
                                              operational_state_t state)
{
    if (test == NULL || !test->configured) {
        return HYDRAULIC_TEST_REJECTED_NOT_CONFIGURED;
    }
    uint32_t index = 0U;
    while (index < HYDRAULIC_TEST_ORDERS && test->orders[index] != percent) {
        index++;
    }
    if (index == HYDRAULIC_TEST_ORDERS) {
        return HYDRAULIC_TEST_REJECTED_NOT_DECLARED;
    }
    if (!pump_idle(state)) {
        return HYDRAULIC_TEST_REJECTED_PUMP_RUNNING;
    }
    test->selected = percent;
    return HYDRAULIC_TEST_ACCEPTED;
}

uint8_t hydraulic_test_step(hydraulic_test_t *test, operational_state_t state)
{
    if (test == NULL || !test->configured) {
        return 0U;
    }
    const bool extracting = state == OP_STATE_EXTRACTION;
    if (extracting && !test->extracting) {
        test->applied = test->selected;
    }
    test->extracting = extracting;
    if (!extracting) {
        test->applied = 0U;
    }
    return test->applied;
}

const char *hydraulic_test_result_name(hydraulic_test_result_t result)
{
    switch (result) {
    case HYDRAULIC_TEST_ACCEPTED:
        return "accepted";
    case HYDRAULIC_TEST_REJECTED_NOT_DECLARED:
        return "not-declared";
    case HYDRAULIC_TEST_REJECTED_PUMP_RUNNING:
        return "pump-running";
    case HYDRAULIC_TEST_REJECTED_COMMAND:
        return "command";
    case HYDRAULIC_TEST_REJECTED_NOT_CONFIGURED:
    default:
        return "not-configured";
    }
}
