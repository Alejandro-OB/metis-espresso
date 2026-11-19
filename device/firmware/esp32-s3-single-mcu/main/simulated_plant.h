#pragma once

/*
 * Modelo de la cafetera: describe cómo evolucionan temperatura y presión y
 * qué demandaría la máquina en cada estado. No decide nada sobre el proceso.
 *
 * La supervisión vivía aquí y se trasladó a `process_supervisor`, porque un
 * modelo que decide cuándo detener la máquina solo funciona mientras el
 * proceso sea ficticio, y además pone al gemelo digital dentro del lazo, que es
 * justo lo que RD-004 prohíbe.
 */

#include <stdbool.h>
#include <stdint.h>

#include "operational_fsm.h"

typedef enum {
    SIM_FAULT_NONE = 0,
    SIM_FAULT_TEMPERATURE_SENSOR_DISCONNECTED = UINT32_C(1) << 0,
    SIM_FAULT_PRESSURE_SENSOR_DISCONNECTED = UINT32_C(1) << 1,
    SIM_FAULT_FORCED_OVERTEMPERATURE = UINT32_C(1) << 2,
    SIM_FAULT_FORCED_OVERPRESSURE = UINT32_C(1) << 3,
    SIM_FAULT_ZERO_CROSS_LOST = UINT32_C(1) << 4,
} simulation_fault_t;

typedef struct {
    float ambient_temperature_c;
    float coffee_temperature_setpoint_c;
    float temperature_ready_tolerance_c;  /* histéresis del calentador modelado */
    /* Magnitudes a las que una falla inyectada lleva la lectura. No son límites
     * de seguridad: esos pertenecen al supervisor, no al modelo. */
    float forced_overtemperature_c;
    float extraction_pressure_setpoint_bar;
    float forced_overpressure_bar;
    float heater_rate_c_per_second;
    float passive_cooling_per_second;
    float pressure_time_constant_ms;
    float extraction_pump_duty;
    float purge_pump_duty;
} simulation_config_t;

typedef struct {
    uint64_t elapsed_ms;
    uint32_t state_elapsed_ms;
    operational_state_t operational_state;
    float temperature_c;
    float pressure_bar;
    bool temperature_valid;
    bool pressure_valid;
    bool zero_cross_available;
    bool simulated_heater_on;
    bool simulated_pump_on;
    float simulated_pump_duty;
    uint32_t injected_faults;
} simulation_snapshot_t;

typedef struct {
    simulation_config_t config;
    simulation_snapshot_t snapshot;
    operational_state_t previous_state;
    bool heater_latch;
    bool initialized;
} simulation_model_t;

simulation_config_t simulation_default_config(void);
bool simulation_model_init(simulation_model_t *model,
                           const simulation_config_t *config);
void simulation_model_set_faults(simulation_model_t *model,
                                 uint32_t fault_mask);
bool simulation_model_apply_extraction_profile(
    simulation_model_t *model, float temperature_setpoint_c,
    float pressure_setpoint_bar);
/* Avanza el modelo. No devuelve eventos: quien los decide es el supervisor. */
void simulation_model_step(simulation_model_t *model,
                           operational_state_t state,
                           uint32_t elapsed_ms);
const simulation_snapshot_t *simulation_model_snapshot(
    const simulation_model_t *model);
