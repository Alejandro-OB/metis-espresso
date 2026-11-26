#include "device_config.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

#define DEVICE_CONFIG_NAMESPACE "cafe-config"
#define DEVICE_CONFIG_KEY_URL "sync_url"
#define DEVICE_CONFIG_KEY_TOKEN "sync_token"
#define DEVICE_CONFIG_KEY_UPDATE_URL "update_url"

static const char *TAG = "device_config";

/*
 * Lee una cadena guardada. Devuelve false cuando no hay valor, cuando no cabe o
 * cuando está vacía: un valor vacío no es una configuración, y aceptarlo dejaría
 * al dispositivo sin destino creyendo que tiene uno.
 */
static bool read_string(nvs_handle_t handle, const char *key, char *buffer, size_t buffer_size)
{
    size_t length = buffer_size;
    if (nvs_get_str(handle, key, buffer, &length) != ESP_OK) {
        return false;
    }
    return buffer[0] != '\0';
}

static esp_err_t write_string(const char *key, const char *value, size_t maximum_bytes)
{
    if (value == NULL || value[0] == '\0' || strlen(value) >= maximum_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_str(handle, key, value);
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

void device_config_load(device_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    /* La puesta en servicio es el punto de partida; lo guardado puede desplazarla. */
    snprintf(config->sync_url, sizeof(config->sync_url), "%s", CONFIG_CAFE_SYNC_URL);
    snprintf(config->sync_token, sizeof(config->sync_token), "%s", CONFIG_CAFE_SYNC_TOKEN);
    config->url_origin = DEVICE_CONFIG_ORIGIN_COMPILED;
    config->token_origin = DEVICE_CONFIG_ORIGIN_COMPILED;
#if CONFIG_CAFE_FIRMWARE_UPDATE
    snprintf(config->update_url, sizeof(config->update_url), "%s",
             CONFIG_CAFE_FIRMWARE_UPDATE_URL);
#endif
    config->update_url_origin = DEVICE_CONFIG_ORIGIN_COMPILED;

    nvs_handle_t handle;
    if (nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;  /* sin configuración guardada todavía: no es un error */
    }
    char value[DEVICE_CONFIG_URL_BYTES];
    if (read_string(handle, DEVICE_CONFIG_KEY_URL, value, sizeof(value))) {
        snprintf(config->sync_url, sizeof(config->sync_url), "%s", value);
        config->url_origin = DEVICE_CONFIG_ORIGIN_STORED;
    }
    char token[DEVICE_CONFIG_TOKEN_BYTES];
    if (read_string(handle, DEVICE_CONFIG_KEY_TOKEN, token, sizeof(token))) {
        snprintf(config->sync_token, sizeof(config->sync_token), "%s", token);
        config->token_origin = DEVICE_CONFIG_ORIGIN_STORED;
    }
    if (read_string(handle, DEVICE_CONFIG_KEY_UPDATE_URL, value, sizeof(value))) {
        snprintf(config->update_url, sizeof(config->update_url), "%s", value);
        config->update_url_origin = DEVICE_CONFIG_ORIGIN_STORED;
    }
    nvs_close(handle);
}

esp_err_t device_config_set_sync_url(const char *url)
{
    const esp_err_t result = write_string(DEVICE_CONFIG_KEY_URL, url, DEVICE_CONFIG_URL_BYTES);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Dirección de la plataforma guardada: %s", url);
    }
    return result;
}

esp_err_t device_config_set_sync_token(const char *token)
{
    const esp_err_t result =
        write_string(DEVICE_CONFIG_KEY_TOKEN, token, DEVICE_CONFIG_TOKEN_BYTES);
    if (result == ESP_OK) {
        /* La credencial no se registra: saber que cambió es suficiente. */
        ESP_LOGI(TAG, "Credencial del dispositivo actualizada");
    }
    return result;
}

esp_err_t device_config_set_update_url(const char *url)
{
    const esp_err_t result =
        write_string(DEVICE_CONFIG_KEY_UPDATE_URL, url, DEVICE_CONFIG_URL_BYTES);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Dirección del firmware guardada: %s", url);
    }
    return result;
}

esp_err_t device_config_reset(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(DEVICE_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    const esp_err_t erased = nvs_erase_all(handle);
    result = erased == ESP_OK ? nvs_commit(handle) : erased;
    nvs_close(handle);
    return result;
}

const char *device_config_origin_name(device_config_origin_t origin)
{
    return origin == DEVICE_CONFIG_ORIGIN_STORED ? "guardada" : "compilada";
}
