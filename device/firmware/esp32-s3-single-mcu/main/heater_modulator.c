#include "heater_modulator.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

bool heater_modulator_configure(heater_modulator_t *modulator,
                                uint32_t window_ms,
                                uint32_t tick_ms)
{
    if (modulator == NULL || window_ms == 0U || tick_ms == 0U || tick_ms > window_ms) {
        return false;
    }
    if ((window_ms % tick_ms) != 0U) {
        return false;
    }
    memset(modulator, 0, sizeof(*modulator));
    modulator->window_ms = window_ms;
    modulator->tick_ms = tick_ms;
    return true;
}

void heater_modulator_reset(heater_modulator_t *modulator)
{
    if (modulator == NULL) {
        return;
    }
    const uint32_t window_ms = modulator->window_ms;
    const uint32_t tick_ms = modulator->tick_ms;
    memset(modulator, 0, sizeof(*modulator));
    modulator->window_ms = window_ms;
    modulator->tick_ms = tick_ms;
}

uint8_t heater_modulator_quantize(float demand_percent)
{
    if (!isfinite(demand_percent) || demand_percent <= 0.0F) {
        return 0U;
    }
    if (demand_percent >= (float)HEATER_DEMAND_FULL_SCALE) {
        return (uint8_t)HEATER_DEMAND_FULL_SCALE;
    }
    return (uint8_t)(demand_percent + 0.5F);
}

static void open_window(heater_modulator_t *modulator, uint8_t demand)
{
    modulator->applied_ms = 0U;

    if (demand == 0U) {
        /* Cero significa apagado, no una deuda pendiente. */
        modulator->target_ms = 0U;
        modulator->residue_ms = 0;
        return;
    }

    const uint32_t capped = demand > HEATER_DEMAND_FULL_SCALE
                                ? (uint32_t)HEATER_DEMAND_FULL_SCALE
                                : (uint32_t)demand;
    const int64_t desired =
        (int64_t)((uint64_t)capped * (uint64_t)modulator->window_ms /
                  (uint64_t)HEATER_DEMAND_FULL_SCALE);
    int64_t target = desired + (int64_t)modulator->residue_ms;
    if (target < 0) {
        target = 0;
    } else if (target > (int64_t)modulator->window_ms) {
        target = (int64_t)modulator->window_ms;
    }
    modulator->target_ms = (uint32_t)target;
}

static void close_window(heater_modulator_t *modulator)
{
    /* Con signo a propósito: el paso puede hacer que se conduzca de más, y esa
     * deuda al revés debe restarse de la ventana siguiente igual que la otra se
     * suma. Queda acotada a una ventana para que una racha de inhibiciones o de
     * saturación no la deje crecer sin límite. */
    int64_t residue = modulator->target_ms == 0U
                          ? 0
                          : (int64_t)modulator->target_ms - (int64_t)modulator->applied_ms;
    const int64_t bound = (int64_t)modulator->window_ms;
    if (residue > bound) {
        residue = bound;
    } else if (residue < -bound) {
        residue = -bound;
    }
    modulator->residue_ms = (int32_t)residue;
    modulator->elapsed_ms = 0U;
    if (modulator->windows < UINT32_MAX) {
        modulator->windows++;
    }
}

bool heater_modulator_step(heater_modulator_t *modulator, uint8_t demand)
{
    if (modulator == NULL || modulator->window_ms == 0U || modulator->tick_ms == 0U) {
        return false;
    }
    if (modulator->elapsed_ms == 0U) {
        open_window(modulator, demand);
    }
    if (demand == 0U) {
        /* Cero corta de inmediato, sin esperar al final de la ventana, y
         * descarta el residuo. `applied_ms` no se toca: lo que ya se condujo en
         * esta ventana ocurrió, y el registro no debe decir lo contrario. */
        modulator->target_ms = 0U;
        modulator->residue_ms = 0;
    }

    const bool conduct = modulator->applied_ms < modulator->target_ms;
    if (conduct) {
        modulator->applied_ms += modulator->tick_ms;
    }
    modulator->elapsed_ms += modulator->tick_ms;
    if (modulator->elapsed_ms >= modulator->window_ms) {
        close_window(modulator);
    }
    return conduct;
}

uint32_t heater_modulator_target_ms(const heater_modulator_t *modulator)
{
    return modulator == NULL ? 0U : modulator->target_ms;
}

uint32_t heater_modulator_applied_ms(const heater_modulator_t *modulator)
{
    return modulator == NULL ? 0U : modulator->applied_ms;
}

uint32_t heater_modulator_elapsed_ms(const heater_modulator_t *modulator)
{
    return modulator == NULL ? 0U : modulator->elapsed_ms;
}

uint32_t heater_modulator_windows(const heater_modulator_t *modulator)
{
    return modulator == NULL ? 0U : modulator->windows;
}

int32_t heater_modulator_residue_ms(const heater_modulator_t *modulator)
{
    return modulator == NULL ? 0 : modulator->residue_ms;
}
