#include "sensor_calibration_store.h"

#include <stddef.h>

#include "esp_log.h"
#include "nvs.h"

#define CALIBRATION_NAMESPACE "eko_calib"
#define CALIBRATION_KEY "sensors"
/*
 * El formato 2 añade la báscula. Un registro del formato 1 tiene otro tamaño y
 * se descarta, con lo que rige la nominal; no se intenta convertir porque nunca
 * llegó a guardarse ninguno: hasta ADR-020 nadie llamaba a
 * `sensor_calibration_store`.
 */
#define CALIBRATION_FORMAT UINT32_C(2)

typedef struct {
    uint32_t format;
    sensor_calibration_t calibration;
} stored_calibration_t;

static const char *TAG = "sensor_calib";

esp_err_t sensor_calibration_load(sensor_calibration_t *calibration, bool *persisted)
{
    if (calibration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *calibration = sensor_calibration_nominal();
    if (persisted != NULL) {
        *persisted = false;
    }
    nvs_handle_t handle;
    if (nvs_open(CALIBRATION_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return ESP_OK;   /* sin calibración guardada: rige la nominal */
    }
    stored_calibration_t stored = {0};
    size_t length = sizeof(stored);
    const esp_err_t result = nvs_get_blob(handle, CALIBRATION_KEY, &stored, &length);
    nvs_close(handle);
    if (result != ESP_OK || length != sizeof(stored) || stored.format != CALIBRATION_FORMAT) {
        return ESP_OK;
    }
    if (!sensor_calibration_is_valid(&stored.calibration)) {
        ESP_LOGW(TAG, "Calibración persistida descartada por no ser plausible");
        return ESP_OK;
    }
    *calibration = stored.calibration;
    if (persisted != NULL) {
        *persisted = true;
    }
    ESP_LOGI(TAG, "Calibración recuperada: %s v%u", calibration->id,
             (unsigned)calibration->version);
    return ESP_OK;
}

esp_err_t sensor_calibration_store(const sensor_calibration_t *calibration)
{
    if (!sensor_calibration_is_valid(calibration)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(CALIBRATION_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    const stored_calibration_t stored = {.format = CALIBRATION_FORMAT,
                                         .calibration = *calibration};
    result = nvs_set_blob(handle, CALIBRATION_KEY, &stored, sizeof(stored));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

esp_err_t sensor_calibration_read_stored(sensor_calibration_t *calibration)
{
    if (calibration == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    if (nvs_open(CALIBRATION_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    stored_calibration_t stored = {0};
    size_t length = sizeof(stored);
    const esp_err_t result = nvs_get_blob(handle, CALIBRATION_KEY, &stored, &length);
    nvs_close(handle);
    if (result != ESP_OK || length != sizeof(stored) || stored.format != CALIBRATION_FORMAT ||
        !sensor_calibration_is_valid(&stored.calibration)) {
        return ESP_ERR_NOT_FOUND;
    }
    *calibration = stored.calibration;
    return ESP_OK;
}

esp_err_t sensor_calibration_clear(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(CALIBRATION_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_erase_key(handle, CALIBRATION_KEY);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;  /* no había nada: ya está como se pidió */
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}
