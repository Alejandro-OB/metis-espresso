#include "extraction_profile.h"

#include <stddef.h>
#include <string.h>

static bool terminated_string(const char *value, size_t capacity)
{
    return value != NULL && memchr(value, '\0', capacity) != NULL;
}

extraction_profile_t extraction_simulation_default_profile(void)
{
    return (extraction_profile_t) {
        .id = "simulation-default",
        .name = "Predeterminado de simulación",
        .version = 1U,
        .temperature_setpoint_c = 93.0F,
        .temperature_tolerance_c = 0.5F,
        .pressure_setpoint_bar = 9.0F,
        .pressure_tolerance_bar = 0.5F,
        .maximum_time_ms = 30000U,
        .expected_beverage_mass_defined = false,
        .expected_beverage_mass_g = 0.0F,
        .purge_maximum_defined = false,
        .purge_maximum_ms = 0U,
        .boiler_offset_c = 0.0F,
    };
}

extraction_profile_limits_t extraction_simulation_limits(void)
{
    /* Software-test envelope only; not commissioned machine limits. */
    return (extraction_profile_limits_t) {
        .minimum_temperature_c = 80.0F,
        .maximum_temperature_c = 100.0F,
        .minimum_pressure_bar = 1.0F,
        .maximum_pressure_bar = 11.0F,
        .minimum_temperature_tolerance_c = 0.1F,
        .maximum_temperature_tolerance_c = 5.0F,
        .minimum_pressure_tolerance_bar = 0.1F,
        .maximum_pressure_tolerance_bar = 3.0F,
        .minimum_time_ms = 5000U,
        .maximum_time_ms = 120000U,
        .minimum_expected_mass_g = 1.0F,
        .maximum_expected_mass_g = 200.0F,
        /* Los mismos topes que process_limits_set_purge vuelve a exigir al
         * aplicarlo: rechazar aquí da la causa en el perfil, no en el
         * supervisor, y el valor fuera de rango no se recorta. */
        .minimum_purge_ms = 1000U,
        .maximum_purge_ms = 30000U,
    };
}

extraction_profile_validation_t extraction_profile_validate(
    const extraction_profile_t *profile,
    const extraction_profile_limits_t *limits)
{
    if (profile == NULL || limits == NULL ||
        limits->minimum_temperature_c > limits->maximum_temperature_c ||
        limits->minimum_pressure_bar > limits->maximum_pressure_bar ||
        limits->minimum_temperature_tolerance_c >
            limits->maximum_temperature_tolerance_c ||
        limits->minimum_pressure_tolerance_bar >
            limits->maximum_pressure_tolerance_bar ||
        limits->minimum_time_ms > limits->maximum_time_ms ||
        limits->minimum_expected_mass_g > limits->maximum_expected_mass_g ||
        limits->minimum_purge_ms > limits->maximum_purge_ms) {
        return PROFILE_INVALID_ARGUMENT;
    }
    if (!terminated_string(profile->id, sizeof(profile->id)) ||
        profile->id[0] == '\0') {
        return PROFILE_INVALID_ID;
    }
    if (!terminated_string(profile->name, sizeof(profile->name)) ||
        profile->name[0] == '\0') {
        return PROFILE_INVALID_NAME;
    }
    if (profile->version == 0U) {
        return PROFILE_INVALID_VERSION;
    }
    if (profile->temperature_setpoint_c < limits->minimum_temperature_c ||
        profile->temperature_setpoint_c > limits->maximum_temperature_c) {
        return PROFILE_TEMPERATURE_OUT_OF_RANGE;
    }
    if (profile->temperature_tolerance_c <
            limits->minimum_temperature_tolerance_c ||
        profile->temperature_tolerance_c >
            limits->maximum_temperature_tolerance_c) {
        return PROFILE_TEMPERATURE_TOLERANCE_OUT_OF_RANGE;
    }
    if (profile->pressure_setpoint_bar < limits->minimum_pressure_bar ||
        profile->pressure_setpoint_bar > limits->maximum_pressure_bar) {
        return PROFILE_PRESSURE_OUT_OF_RANGE;
    }
    if (profile->pressure_tolerance_bar <
            limits->minimum_pressure_tolerance_bar ||
        profile->pressure_tolerance_bar >
            limits->maximum_pressure_tolerance_bar) {
        return PROFILE_PRESSURE_TOLERANCE_OUT_OF_RANGE;
    }
    if (profile->maximum_time_ms < limits->minimum_time_ms ||
        profile->maximum_time_ms > limits->maximum_time_ms) {
        return PROFILE_TIME_OUT_OF_RANGE;
    }
    if (profile->expected_beverage_mass_defined &&
        (profile->expected_beverage_mass_g < limits->minimum_expected_mass_g ||
         profile->expected_beverage_mass_g > limits->maximum_expected_mass_g)) {
        return PROFILE_EXPECTED_MASS_OUT_OF_RANGE;
    }
    if (profile->purge_maximum_defined &&
        (profile->purge_maximum_ms < limits->minimum_purge_ms ||
         profile->purge_maximum_ms > limits->maximum_purge_ms)) {
        return PROFILE_PURGE_OUT_OF_RANGE;
    }
    return PROFILE_VALID;
}

const char *extraction_profile_validation_name(
    extraction_profile_validation_t result)
{
    static const char *const names[] = {
        [PROFILE_VALID] = "valid",
        [PROFILE_INVALID_ARGUMENT] = "invalid-argument",
        [PROFILE_INVALID_ID] = "invalid-id",
        [PROFILE_INVALID_NAME] = "invalid-name",
        [PROFILE_INVALID_VERSION] = "invalid-version",
        [PROFILE_TEMPERATURE_OUT_OF_RANGE] = "temperature-out-of-range",
        [PROFILE_TEMPERATURE_TOLERANCE_OUT_OF_RANGE] =
            "temperature-tolerance-out-of-range",
        [PROFILE_PRESSURE_OUT_OF_RANGE] = "pressure-out-of-range",
        [PROFILE_PRESSURE_TOLERANCE_OUT_OF_RANGE] =
            "pressure-tolerance-out-of-range",
        [PROFILE_TIME_OUT_OF_RANGE] = "time-out-of-range",
        [PROFILE_EXPECTED_MASS_OUT_OF_RANGE] = "expected-mass-out-of-range",
        [PROFILE_PURGE_OUT_OF_RANGE] = "purge-out-of-range",
        [PROFILE_BOILER_OFFSET_OUT_OF_RANGE] = "boiler-offset-out-of-range",
        [PROFILE_FIRST_USE_FEEDFORWARD_OUT_OF_RANGE] = "first-use-feedforward-out-of-range",
    };
    return result >= 0 && (size_t)result < sizeof(names) / sizeof(names[0])
               ? names[result]
               : "invalid-validation-result";
}
