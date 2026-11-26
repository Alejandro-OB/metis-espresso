#include "process_variables.h"

#include <stddef.h>

#include "acquisition_runtime.h"
#include "simulation_runtime.h"
#include "sdkconfig.h"

static process_source_t s_last_source;
/*
 * Una vez que los sensores han funcionado, su fallo no puede quedar tapado por
 * la simulación: regular contra una planta ficticia creyéndola la real sería
 * exactamente la confusión que el sistema existe para evitar.
 */
static bool s_measured_seen;

const char *process_source_name(process_source_t source)
{
    switch (source) {
    case PROCESS_SOURCE_MEASURED:
        return "measured";
    case PROCESS_SOURCE_SIMULATED:
        return "simulation";
    case PROCESS_SOURCE_NONE:
    default:
        return "none";
    }
}

/*
 * La medida real solo desplaza a la simulación cuando **ambos** canales son
 * aptos. Combinar una temperatura medida con una presión simulada produciría un
 * estado que no corresponde a ninguna máquina real ni virtual.
 */
static bool measured_snapshot(process_snapshot_t *snapshot)
{
    measurement_t temperature = {0};
    measurement_t pressure = {0};
    if (!acquisition_runtime_get_temperature(&temperature) ||
        !acquisition_runtime_get_pressure(&pressure)) {
        return false;
    }
    const bool usable = signal_quality_is_usable(temperature.quality) &&
                        signal_quality_is_usable(pressure.quality);
    if (!usable && !s_measured_seen) {
        /* Todavía no ha habido medida real: la simulación puede ocupar su lugar,
         * declarándose como lo que es. */
        return false;
    }
    if (usable) {
        s_measured_seen = true;
    }
    /* Si ya hubo medida real, se entrega aunque no sea apta: el supervisor debe
     * ver la avería y detener la máquina, no recibir una simulación en su lugar. */
    snapshot->temperature_c = temperature.value;
    snapshot->pressure_bar = pressure.value;
    snapshot->temperature_quality = temperature.quality;
    snapshot->pressure_quality = pressure.quality;
    snapshot->temperature_at_ms = temperature.acquired_ms;
    snapshot->pressure_at_ms = pressure.acquired_ms;
    snapshot->calibration_id = temperature.calibration_id;
    snapshot->source = PROCESS_SOURCE_MEASURED;
    return true;
}

static bool simulated_snapshot(process_snapshot_t *snapshot)
{
#if CONFIG_CAFE_SIMULATION
    simulation_snapshot_t plant = {0};
    if (!simulation_runtime_get_snapshot(&plant)) {
        return false;
    }
    snapshot->temperature_c = plant.temperature_c;
    snapshot->pressure_bar = plant.pressure_bar;
    snapshot->temperature_quality = plant.temperature_valid ? SIGNAL_VALID : SIGNAL_OUT_OF_RANGE;
    snapshot->pressure_quality = plant.pressure_valid ? SIGNAL_VALID : SIGNAL_OUT_OF_RANGE;
    snapshot->temperature_at_ms = plant.elapsed_ms;
    snapshot->pressure_at_ms = plant.elapsed_ms;
    snapshot->calibration_id = NULL;
    snapshot->source = PROCESS_SOURCE_SIMULATED;
    return true;
#else
    (void)snapshot;
    return false;
#endif
}

bool process_variables_get(process_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    *snapshot = (process_snapshot_t){.source = PROCESS_SOURCE_NONE};
    if (measured_snapshot(snapshot) || simulated_snapshot(snapshot)) {
        s_last_source = snapshot->source;
        return true;
    }
    s_last_source = PROCESS_SOURCE_NONE;
    return false;
}

bool process_variables_measured(void)
{
    return s_last_source == PROCESS_SOURCE_MEASURED;
}
