#pragma once

#include <stdbool.h>
#include <stdint.h>

#define EXTRACTION_PROFILE_ID_LENGTH 40U
#define EXTRACTION_PROFILE_NAME_LENGTH 64U

typedef struct {
    char id[EXTRACTION_PROFILE_ID_LENGTH];
    char name[EXTRACTION_PROFILE_NAME_LENGTH];
    uint32_t version;
    float temperature_setpoint_c;
    float temperature_tolerance_c;
    float pressure_setpoint_bar;
    float pressure_tolerance_bar;
    uint32_t maximum_time_ms;
    bool expected_beverage_mass_defined;
    float expected_beverage_mass_g;
    /* Corte del purgado manual. Es opcional en el contrato: sin él rige el
     * valor compilado (CAFE_PURGE_MAXIMUM_MS). En cero, «no declarado». */
    bool purge_maximum_defined;
    uint32_t purge_maximum_ms;
    /* Compensación del primer uso (ADR-009, enmienda del 2026-10-19): cuánto
     * pide el gemelo subir la referencia de caldera para la próxima
     * preparación. Opcional en el contrato (`boilerOffsetC`); en cero, que es
     * también su ausencia, no se sube nada. Es una petición, no lo aplicado:
     * lo valida `profile_runtime` y lo aplica una sola vez `first_use_offset`. */
    float boiler_offset_c;
    /* Refuerzo de la prealimentación para la misma preparación (ADR-009,
     * enmienda del 2026-10-27), en % de calentador por % de bomba. Opcional
     * (`firstUseFeedforwardGain`); en cero, que es también su ausencia, rige la
     * ganancia del acta. Como la subida, es una petición: la juzga
     * `profile_runtime` y la aplica una sola vez `first_use_offset`. */
    float first_use_feedforward_gain;
} extraction_profile_t;

typedef struct {
    float minimum_temperature_c;
    float maximum_temperature_c;
    float minimum_pressure_bar;
    float maximum_pressure_bar;
    float minimum_temperature_tolerance_c;
    float maximum_temperature_tolerance_c;
    float minimum_pressure_tolerance_bar;
    float maximum_pressure_tolerance_bar;
    uint32_t minimum_time_ms;
    uint32_t maximum_time_ms;
    float minimum_expected_mass_g;
    float maximum_expected_mass_g;
    uint32_t minimum_purge_ms;
    uint32_t maximum_purge_ms;
} extraction_profile_limits_t;

typedef enum {
    PROFILE_VALID = 0,
    PROFILE_INVALID_ARGUMENT,
    PROFILE_INVALID_ID,
    PROFILE_INVALID_NAME,
    PROFILE_INVALID_VERSION,
    PROFILE_TEMPERATURE_OUT_OF_RANGE,
    PROFILE_TEMPERATURE_TOLERANCE_OUT_OF_RANGE,
    PROFILE_PRESSURE_OUT_OF_RANGE,
    PROFILE_PRESSURE_TOLERANCE_OUT_OF_RANGE,
    PROFILE_TIME_OUT_OF_RANGE,
    PROFILE_EXPECTED_MASS_OUT_OF_RANGE,
    PROFILE_PURGE_OUT_OF_RANGE,
    /* El desplazamiento de caldera pedido no lo admite esta máquina (ver
     * `first_use_offset_check`). No lo juzga `extraction_profile_validate`,
     * que no conoce el acta: lo da `profile_runtime` al activar. */
    PROFILE_BOILER_OFFSET_OUT_OF_RANGE,
    /* El refuerzo de la prealimentación pedido no lo admite esta máquina (ver
     * `first_use_feedforward_check`); también lo da `profile_runtime`. */
    PROFILE_FIRST_USE_FEEDFORWARD_OUT_OF_RANGE,
} extraction_profile_validation_t;

extraction_profile_t extraction_simulation_default_profile(void);
extraction_profile_limits_t extraction_simulation_limits(void);
extraction_profile_validation_t extraction_profile_validate(
    const extraction_profile_t *profile,
    const extraction_profile_limits_t *limits);
const char *extraction_profile_validation_name(
    extraction_profile_validation_t result);
