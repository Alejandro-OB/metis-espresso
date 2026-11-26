#include "psm_scheduler.h"

#include <math.h>
#include <string.h>

void psm_scheduler_reset(psm_scheduler_t *scheduler)
{
    if (scheduler == NULL) {
        return;
    }
    memset(scheduler, 0, sizeof(*scheduler));
}

uint8_t psm_scheduler_quantize(float demand_percent)
{
    if (!isfinite(demand_percent) || demand_percent <= 0.0F) {
        return 0U;
    }
    if (demand_percent >= (float)PSM_DENSITY_FULL_SCALE) {
        return (uint8_t)PSM_DENSITY_FULL_SCALE;
    }
    return (uint8_t)lroundf(demand_percent);
}

bool psm_scheduler_step(psm_scheduler_t *scheduler, uint8_t density)
{
    if (scheduler == NULL) {
        return false;
    }
    scheduler->opportunities++;

    uint32_t requested = density;
    if (requested > PSM_DENSITY_FULL_SCALE) {
        requested = PSM_DENSITY_FULL_SCALE;
    }
    /* Una densidad nula no actúa ni acumula. Dejar que el acumulador conservara
     * un residuo permitiría que una solicitud de 0 % emitiera la actuación que
     * quedó pendiente de una demanda anterior, y 0 % significa exactamente que
     * no se actúa. */
    if (requested == 0U) {
        scheduler->accumulator = 0U;
        return false;
    }

    scheduler->accumulator += requested;
    if (scheduler->accumulator < PSM_DENSITY_FULL_SCALE) {
        return false;
    }
    scheduler->accumulator -= PSM_DENSITY_FULL_SCALE;
    scheduler->emitted++;
    return true;
}

bool psm_scheduler_half_cycle(psm_scheduler_t *scheduler, uint8_t density)
{
    if (scheduler == NULL) {
        return false;
    }
    const bool first_half = !scheduler->second_half;
    scheduler->second_half = first_half;
    if (first_half) {
        scheduler->cycle_emits = psm_scheduler_step(scheduler, density);
        return scheduler->cycle_emits;
    }
    /* La pareja se cuenta igual con demanda nula, para que las oportunidades
     * sigan siendo ciclos; lo que no se hace es terminar un ciclo empezado. */
    if (density == 0U) {
        scheduler->cycle_emits = false;
        scheduler->accumulator = 0U;
    }
    return scheduler->cycle_emits;
}

uint32_t psm_scheduler_emitted(const psm_scheduler_t *scheduler)
{
    return scheduler == NULL ? 0U : scheduler->emitted;
}

uint32_t psm_scheduler_opportunities(const psm_scheduler_t *scheduler)
{
    return scheduler == NULL ? 0U : scheduler->opportunities;
}

uint32_t psm_scheduler_residue(const psm_scheduler_t *scheduler)
{
    return scheduler == NULL ? 0U : scheduler->accumulator;
}
