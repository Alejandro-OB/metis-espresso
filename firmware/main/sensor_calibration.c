#include "sensor_calibration.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

sensor_calibration_t sensor_calibration_nominal(void)
{
    sensor_calibration_t calibration = {
        .version = 1U,
        .temperature = sensor_ntc_default(),
        .pressure = sensor_pressure_default(),
        .scale_counts_per_gram = 0.0F,
    };
    snprintf(calibration.id, sizeof(calibration.id), "nominal-v1");
    return calibration;
}

bool sensor_calibration_id_is_usable(const char *id)
{
    if (id == NULL || id[0] == '\0') {
        return false;
    }
    for (size_t index = 0U; index < SENSOR_CALIBRATION_ID_LENGTH; index++) {
        const char c = id[index];
        if (c == '\0') {
            return true;
        }
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!allowed) {
            return false;
        }
    }
    return false;  /* sin terminador dentro del tamaño */
}

bool sensor_calibration_is_valid(const sensor_calibration_t *calibration)
{
    if (calibration == NULL || !sensor_calibration_id_is_usable(calibration->id) ||
        calibration->version == 0U) {
        return false;
    }
    const ntc_scaling_t *ntc = &calibration->temperature;
    const pressure_scaling_t *pressure = &calibration->pressure;
    const bool ntc_ok = isfinite(ntc->supply_v) && ntc->supply_v > 1.0F && ntc->supply_v < 6.0F &&
                        isfinite(ntc->series_ohms) && ntc->series_ohms > 100.0F &&
                        ntc->series_ohms < 1e6F && isfinite(ntc->reference_ohms) &&
                        ntc->reference_ohms > 100.0F && ntc->reference_ohms < 1e7F &&
                        isfinite(ntc->reference_c) && ntc->reference_c > -50.0F &&
                        ntc->reference_c < 100.0F && isfinite(ntc->beta_k) &&
                        ntc->beta_k > 1000.0F && ntc->beta_k < 10000.0F;
    const bool pressure_ok = isfinite(pressure->gain_bar_per_v) &&
                             pressure->gain_bar_per_v > 0.1F &&
                             pressure->gain_bar_per_v < 100.0F &&
                             isfinite(pressure->offset_bar) && pressure->offset_bar > -20.0F &&
                             pressure->offset_bar < 20.0F;
    /*
     * La báscula admite cero, que es «sin calibrar». Un factor negativo no es una
     * calibración sino una célula cableada al revés, y eso se corrige en el
     * cable: aceptarlo convertiría cada carga en una masa negativa que
     * `scale_reading` trataría además como báscula sin calibrar.
     */
    const float scale = calibration->scale_counts_per_gram;
    const bool scale_ok = isfinite(scale) && scale >= 0.0F && scale < 1e6F;
    return ntc_ok && pressure_ok && scale_ok;
}
