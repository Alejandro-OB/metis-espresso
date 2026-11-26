#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "simulated_plant.h"

esp_err_t simulation_runtime_start(void);
bool simulation_runtime_set_faults(uint32_t fault_mask, TickType_t wait_ticks);
bool simulation_runtime_get_snapshot(simulation_snapshot_t *snapshot);
void simulation_runtime_set_external_snapshot(const simulation_snapshot_t *snapshot);
