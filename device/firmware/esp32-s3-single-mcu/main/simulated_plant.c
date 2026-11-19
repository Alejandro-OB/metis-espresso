#include "simulated_plant.h"

#include <float.h>
#include <stddef.h>

static uint32_t saturating_add_u32(uint32_t value, uint32_t increment)
{
    return UINT32_MAX - value < increment ? UINT32_MAX : value + increment;
}

static float clampf(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static bool config_is_valid(const simulation_config_t *config)
{
    return config != NULL &&
           config->ambient_temperature_c > -FLT_MAX &&
           config->coffee_temperature_setpoint_c >
               config->ambient_temperature_c &&
           config->forced_overtemperature_c >
               config->coffee_temperature_setpoint_c &&
           config->temperature_ready_tolerance_c > 0.0F &&
           config->extraction_pressure_setpoint_bar > 0.0F &&
           config->forced_overpressure_bar >
               config->extraction_pressure_setpoint_bar &&
           config->heater_rate_c_per_second > 0.0F &&
           config->passive_cooling_per_second > 0.0F &&
           config->pressure_time_constant_ms > 0.0F &&
           config->extraction_pump_duty > 0.0F &&
           config->extraction_pump_duty <= 1.0F &&
           config->purge_pump_duty > 0.0F &&
           config->purge_pump_duty <= 1.0F;
}

simulation_config_t simulation_default_config(void)
{
    /*
     * Valores que hacen corta y observable la simulación en banco y en la placa.
     * No son referencias puestas en servicio ni límites de la máquina.
     */
    return (simulation_config_t) {
        .ambient_temperature_c = 22.0F,
        .coffee_temperature_setpoint_c = 93.0F,
        .temperature_ready_tolerance_c = 0.5F,
        .forced_overtemperature_c = 150.0F,
        .extraction_pressure_setpoint_bar = 9.0F,
        .forced_overpressure_bar = 12.0F,
        .heater_rate_c_per_second = 8.0F,
        .passive_cooling_per_second = 0.04F,
        .pressure_time_constant_ms = 400.0F,
        .extraction_pump_duty = 0.75F,
        .purge_pump_duty = 0.25F,
    };
}

bool simulation_model_init(simulation_model_t *model,
                           const simulation_config_t *config)
{
    if (model == NULL || !config_is_valid(config)) {
        return false;
    }

    *model = (simulation_model_t) {
        .config = *config,
        .snapshot = {
            .operational_state = OP_STATE_SAFE_INACTIVE,
            .temperature_c = config->ambient_temperature_c,
            .pressure_bar = 0.0F,
            .temperature_valid = true,
            .pressure_valid = true,
            .zero_cross_available = true,
        },
        .previous_state = OP_STATE_SAFE_INACTIVE,
        .initialized = true,
    };
    return true;
}

void simulation_model_set_faults(simulation_model_t *model,
                                 uint32_t fault_mask)
{
    if (model != NULL && model->initialized) {
        model->snapshot.injected_faults = fault_mask;
    }
}

bool simulation_model_apply_extraction_profile(
    simulation_model_t *model, float temperature_setpoint_c,
    float pressure_setpoint_bar)
{
    if (model == NULL || !model->initialized ||
        temperature_setpoint_c <= model->config.ambient_temperature_c ||
        temperature_setpoint_c >= model->config.forced_overtemperature_c ||
        pressure_setpoint_bar <= 0.0F ||
        pressure_setpoint_bar >= model->config.forced_overpressure_bar) {
        return false;
    }
    model->config.coffee_temperature_setpoint_c = temperature_setpoint_c;
    model->config.extraction_pressure_setpoint_bar = pressure_setpoint_bar;
    return true;
}

static bool state_allows_heating(operational_state_t state)
{
    return state == OP_STATE_COFFEE_HEATING || state == OP_STATE_READY ||
           state == OP_STATE_EXTRACTION || state == OP_STATE_MANUAL_PURGE;
}

static bool state_requests_extraction_pump(operational_state_t state)
{
    return state == OP_STATE_EXTRACTION;
}

static bool state_requests_purge_pump(operational_state_t state)
{
    return state == OP_STATE_MANUAL_PURGE;
}

static void update_state_timers(simulation_model_t *model,
                                operational_state_t state,
                                uint32_t elapsed_ms)
{
    if (state != model->previous_state) {
        model->snapshot.state_elapsed_ms = 0U;
        model->previous_state = state;
    }

    model->snapshot.state_elapsed_ms = saturating_add_u32(
        model->snapshot.state_elapsed_ms, elapsed_ms);
    model->snapshot.elapsed_ms += elapsed_ms;
    model->snapshot.operational_state = state;
}

static void update_sensor_health(simulation_model_t *model)
{
    const uint32_t faults = model->snapshot.injected_faults;
    model->snapshot.temperature_valid =
        (faults & SIM_FAULT_TEMPERATURE_SENSOR_DISCONNECTED) == 0U;
    model->snapshot.pressure_valid =
        (faults & SIM_FAULT_PRESSURE_SENSOR_DISCONNECTED) == 0U;
    model->snapshot.zero_cross_available =
        (faults & SIM_FAULT_ZERO_CROSS_LOST) == 0U;
}

static void update_actuator_demands(simulation_model_t *model,
                                    operational_state_t state)
{
    const bool heating_allowed = state_allows_heating(state) &&
                                 model->snapshot.temperature_valid;
    if (!heating_allowed) {
        model->heater_latch = false;
    } else {
        const float target = model->config.coffee_temperature_setpoint_c;
        const float hysteresis = model->config.temperature_ready_tolerance_c;
        if (model->snapshot.temperature_c <= target - hysteresis) {
            model->heater_latch = true;
        } else if (model->snapshot.temperature_c >= target + hysteresis) {
            model->heater_latch = false;
        }
    }
    model->snapshot.simulated_heater_on = model->heater_latch;

    float pump_duty = 0.0F;
    if (state_requests_extraction_pump(state)) {
        pump_duty = model->config.extraction_pump_duty;
    } else if (state_requests_purge_pump(state)) {
        pump_duty = model->config.purge_pump_duty;
    }
    if (!model->snapshot.pressure_valid ||
        !model->snapshot.zero_cross_available) {
        pump_duty = 0.0F;
    }
    model->snapshot.simulated_pump_duty = pump_duty;
    model->snapshot.simulated_pump_on = pump_duty > 0.0F;
}

static void update_dynamics(simulation_model_t *model, uint32_t elapsed_ms)
{
    const float seconds = (float)elapsed_ms / 1000.0F;
    const float thermal_loss = model->config.passive_cooling_per_second *
                               (model->snapshot.temperature_c -
                                model->config.ambient_temperature_c);
    const float thermal_input = model->snapshot.simulated_heater_on
                                    ? model->config.heater_rate_c_per_second
                                    : 0.0F;
    model->snapshot.temperature_c +=
        (thermal_input - thermal_loss) * seconds;
    if (model->snapshot.temperature_c < model->config.ambient_temperature_c) {
        model->snapshot.temperature_c = model->config.ambient_temperature_c;
    }

    float pressure_target = 0.0F;
    if (model->snapshot.operational_state == OP_STATE_EXTRACTION) {
        pressure_target = model->config.extraction_pressure_setpoint_bar;
    } else if (model->snapshot.operational_state == OP_STATE_MANUAL_PURGE) {
        pressure_target = model->snapshot.simulated_pump_duty * 12.0F;
    }
    const float pressure_fraction = clampf(
        (float)elapsed_ms / model->config.pressure_time_constant_ms, 0.0F, 1.0F);
    model->snapshot.pressure_bar +=
        (pressure_target - model->snapshot.pressure_bar) * pressure_fraction;
    if (model->snapshot.pressure_bar < 0.0F) {
        model->snapshot.pressure_bar = 0.0F;
    }

    if ((model->snapshot.injected_faults &
         SIM_FAULT_FORCED_OVERTEMPERATURE) != 0U) {
        model->snapshot.temperature_c =
            model->config.forced_overtemperature_c + 1.0F;
    }
    if ((model->snapshot.injected_faults &
         SIM_FAULT_FORCED_OVERPRESSURE) != 0U) {
        model->snapshot.pressure_bar =
            model->config.forced_overpressure_bar + 0.5F;
    }
}

void simulation_model_step(simulation_model_t *model,
                           operational_state_t state,
                           uint32_t elapsed_ms)
{
    if (model == NULL || !model->initialized || elapsed_ms == 0U) {
        return;
    }

    update_state_timers(model, state, elapsed_ms);
    update_sensor_health(model);
    update_actuator_demands(model, state);
    update_dynamics(model, elapsed_ms);
}

const simulation_snapshot_t *simulation_model_snapshot(
    const simulation_model_t *model)
{
    return model != NULL && model->initialized ? &model->snapshot : NULL;
}
