#include "firmware_update_runtime.h"

#include <inttypes.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "device_config.h"
#include "operational_runtime.h"
#include "supervisor_runtime.h"
#if CONFIG_CAFE_SYNC
#include "sync_runtime.h"
#endif

/*
 * Sin vuelta atrás en el cargador, una imagen que arranca y no alcanza la red
 * dejaría la cafetera sellada sin forma de recibir otra. Cada `sdkconfig` lleva
 * la suya y no hereda `sdkconfig.defaults`: se para aquí y no en la placa.
 */
#if CONFIG_CAFE_FIRMWARE_UPDATE && !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "CAFE_FIRMWARE_UPDATE exige CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y en este sdkconfig (ADR-021)"
#endif

static const char *TAG = "firmware";

/* Baja prioridad: nada de esto compite con el lazo ni con la adquisición. */
#define FIRMWARE_UPDATE_TASK_PRIORITY 2U
#define FIRMWARE_UPDATE_TASK_STACK_BYTES 6144U
#define FIRMWARE_UPDATE_HTTP_TIMEOUT_MS 10000
#define FIRMWARE_UPDATE_MANIFEST_BYTES 512U
#define FIRMWARE_UPDATE_CHUNK_BYTES 4096U
/* Ciclos del supervisor (50 ms) que se esperan a ver el corte publicado. */
#define FIRMWARE_UPDATE_GUARD_WAIT_MS 1000U
#define FIRMWARE_UPDATE_POLL_MS 50U

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static firmware_update_status_t s_status;
static bool s_core_started;
/* Fuera de la pila: la tarea es pequeña y el bloque es de 4 KB. */
static char s_chunk[FIRMWARE_UPDATE_CHUNK_BYTES];
static uint8_t s_running_digest[FIRMWARE_UPDATE_DIGEST_BYTES];
static bool s_running_digest_known;

static void set_phase(firmware_update_phase_t phase)
{
    portENTER_CRITICAL(&s_lock);
    s_status.phase = phase;
    portEXIT_CRITICAL(&s_lock);
}

static void refuse(void)
{
    portENTER_CRITICAL(&s_lock);
    s_status.phase = FIRMWARE_UPDATE_IDLE;
    s_status.last_refused_us = (uint64_t)esp_timer_get_time();
    portEXIT_CRITICAL(&s_lock);
}

static bool network_ready(void)
{
#if CONFIG_CAFE_SYNC
    sync_runtime_status_t sync = {0};
    return sync_runtime_get_status(&sync) && sync.wifi_connected && sync.address[0] != '\0';
#else
    return false;
#endif
}

static void hex(const uint8_t *digest, char out[FIRMWARE_UPDATE_DIGEST_BYTES * 2U + 1U])
{
    static const char DIGITS[] = "0123456789abcdef";
    for (size_t index = 0U; index < FIRMWARE_UPDATE_DIGEST_BYTES; index++) {
        out[2U * index] = DIGITS[digest[index] >> 4];
        out[2U * index + 1U] = DIGITS[digest[index] & 0x0FU];
    }
    out[FIRMWARE_UPDATE_DIGEST_BYTES * 2U] = '\0';
}

/*
 * Declara válida la imagen que corre, o vuelve a la anterior.
 *
 * Solo hace algo si el cargador la marcó «a prueba», es decir, si llegó por
 * esta vía. Una imagen cargada por USB arranca sin ese estado y no hay nada que
 * confirmar.
 */
static void confirm_running_image(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running == NULL || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    set_phase(FIRMWARE_UPDATE_CONFIRMING);
    ESP_LOGW(TAG, "Imagen nueva a prueba en %s: se confirma si la supervisión corre y "
                  "hay red en %" PRIu32 " s",
             running->label, FIRMWARE_UPDATE_CONFIRM_TIMEOUT_MS / 1000U);

    const int64_t deadline_us =
        esp_timer_get_time() + (int64_t)FIRMWARE_UPDATE_CONFIRM_TIMEOUT_MS * 1000;
    bool guard_decided = false;
    bool network = false;
    while (esp_timer_get_time() < deadline_us) {
        supervisor_status_t supervisor = {0};
        guard_decided = s_core_started && supervisor_runtime_get_status(&supervisor) &&
                        supervisor.actuation.pump != ACTUATION_UNDECIDED &&
                        supervisor.actuation.heater != ACTUATION_UNDECIDED;
        network = network_ready();
        if (guard_decided && network) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500U));
    }

    if (guard_decided && network) {
        const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGW(TAG, "Imagen nueva confirmada: %s", esp_err_to_name(result));
        set_phase(FIRMWARE_UPDATE_IDLE);
        return;
    }
    ESP_LOGE(TAG, "Imagen nueva NO confirmada (supervisión %s, red %s): vuelta a la anterior",
             guard_decided ? "sí" : "no", network ? "sí" : "no");
    vTaskDelay(pdMS_TO_TICKS(100U));
    (void)esp_ota_mark_app_invalid_rollback_and_reboot();
    /* Solo vuelve si no había imagen anterior válida: se sigue con esta. */
    ESP_LOGE(TAG, "No hay imagen anterior a la que volver; sigue la actual");
    set_phase(FIRMWARE_UPDATE_IDLE);
}

static firmware_update_conditions_t gather_conditions(const device_config_t *config)
{
    firmware_update_conditions_t conditions = {
        /* Si no se puede consultar al supervisor, nada indica que la máquina
         * esté apagada: se declara un estado que el módulo rechaza. */
        .state = OP_STATE_ALARM,
        .pump_allowed = true,
        .heater_allowed = true,
    };
    supervisor_status_t supervisor = {0};
    if (supervisor_runtime_get_status(&supervisor)) {
        conditions.state = supervisor.state;
        conditions.pump_allowed = actuation_verdict_allows(supervisor.actuation.pump);
        conditions.heater_allowed = actuation_verdict_allows(supervisor.actuation.heater);
        conditions.identification_active =
            supervisor.identification.phase == IDENTIFICATION_ARMED ||
            supervisor.identification.phase == IDENTIFICATION_RUNNING;
    }
    conditions.network_ready = network_ready();
    conditions.source_configured = config->update_url[0] != '\0';
    return conditions;
}

/* Baja el manifiesto entero a `buffer`. */
static bool fetch_manifest(const char *url, char *buffer, size_t capacity)
{
    const esp_http_client_config_t configuration = {
        .url = url,
        .timeout_ms = FIRMWARE_UPDATE_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&configuration);
    if (client == NULL) {
        return false;
    }
    bool ok = false;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        int total = 0;
        while (status == 200 && (size_t)total < capacity - 1U) {
            const int read =
                esp_http_client_read(client, buffer + total, (int)(capacity - 1U) - total);
            if (read <= 0) {
                break;
            }
            total += read;
        }
        buffer[total] = '\0';
        ok = status == 200 && total > 0;
        if (!ok) {
            ESP_LOGE(TAG, "Manifiesto no disponible: HTTP %d", status);
        }
    } else {
        ESP_LOGE(TAG, "Sin respuesta del computador en %s", url);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/*
 * La imagen está junto al manifiesto: misma carpeta, `firmware.bin`. Un solo
 * dato que configurar, y no dos que puedan quedar desparejados.
 */
static bool image_url(const char *manifest_url, char *out, size_t capacity)
{
    const char *slash = strrchr(manifest_url, '/');
    if (slash == NULL) {
        return false;
    }
    const size_t base = (size_t)(slash - manifest_url) + 1U;
    const int written = snprintf(out, capacity, "%.*sfirmware.bin", (int)base, manifest_url);
    return written > 0 && (size_t)written < capacity;
}

/* Espera a ver publicado el corte de las dos salidas por actualización. */
static bool guard_holds_outputs(void)
{
    for (uint32_t waited = 0U; waited <= FIRMWARE_UPDATE_GUARD_WAIT_MS;
         waited += FIRMWARE_UPDATE_POLL_MS) {
        supervisor_status_t supervisor = {0};
        if (supervisor_runtime_get_status(&supervisor) &&
            supervisor.actuation.pump == ACTUATION_INHIBITED_STARTING_UP &&
            supervisor.actuation.heater == ACTUATION_INHIBITED_STARTING_UP) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(FIRMWARE_UPDATE_POLL_MS));
    }
    return false;
}

/* Baja la imagen y la escribe en `target`. Devuelve true si quedó entera. */
static bool write_image(const char *url, const esp_partition_t *target,
                        const firmware_manifest_t *manifest, esp_ota_handle_t *handle)
{
    const esp_http_client_config_t configuration = {
        .url = url,
        .timeout_ms = FIRMWARE_UPDATE_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&configuration);
    if (client == NULL) {
        return false;
    }
    bool ok = false;
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Sin respuesta del computador en %s", url);
        goto done;
    }
    const int64_t length = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status != 200 || length != (int64_t)manifest->size_bytes) {
        ESP_LOGE(TAG, "Imagen no disponible o de otro tamaño: HTTP %d, %" PRId64
                      " bytes frente a %" PRIu32 " del manifiesto",
                 status, length, manifest->size_bytes);
        goto done;
    }
    /* Escritura secuencial: borra cada sector al llegar a él, en vez de toda la
     * partición de golpe, que pararía la caché durante segundos. */
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, handle) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo preparar %s", target->label);
        goto done;
    }
    uint32_t written = 0U;
    while (written < manifest->size_bytes) {
        const operational_state_t state = operational_runtime_state();
        if (!firmware_update_may_continue(state)) {
            ESP_LOGW(TAG, "La máquina salió del reposo (%s): se abandona la actualización",
                     operational_state_name(state));
            goto done;
        }
        const int read = esp_http_client_read(client, s_chunk, sizeof(s_chunk));
        if (read <= 0) {
            ESP_LOGE(TAG, "Descarga cortada en %" PRIu32 " de %" PRIu32 " bytes", written,
                     manifest->size_bytes);
            goto done;
        }
        if (esp_ota_write(*handle, s_chunk, (size_t)read) != ESP_OK) {
            ESP_LOGE(TAG, "Escritura rechazada en %" PRIu32 " bytes", written);
            goto done;
        }
        written += (uint32_t)read;
        portENTER_CRITICAL(&s_lock);
        s_status.written_bytes = written;
        portEXIT_CRITICAL(&s_lock);
    }
    ok = written == manifest->size_bytes;

done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

/* Comprueba lo escrito contra lo publicado y contra lo que corre. */
static bool image_is_the_published_one(const esp_partition_t *target,
                                       const firmware_manifest_t *manifest)
{
    esp_app_desc_t written = {0};
    if (esp_ota_get_partition_description(target, &written) != ESP_OK ||
        strncmp(written.project_name, esp_app_get_description()->project_name,
                sizeof(written.project_name)) != 0) {
        ESP_LOGE(TAG, "La imagen escrita no es de este proyecto");
        return false;
    }
    uint8_t digest[FIRMWARE_UPDATE_DIGEST_BYTES];
    if (esp_partition_get_sha256(target, digest) != ESP_OK ||
        !firmware_manifest_digest_matches(manifest, digest)) {
        char text[FIRMWARE_UPDATE_DIGEST_BYTES * 2U + 1U];
        hex(digest, text);
        ESP_LOGE(TAG, "La huella escrita (%s) no es la del manifiesto", text);
        return false;
    }
    return true;
}

static void run_update(void)
{
    device_config_t config;
    device_config_load(&config);

    firmware_update_conditions_t conditions = gather_conditions(&config);
    portENTER_CRITICAL(&s_lock);
    conditions.in_progress = s_status.phase != FIRMWARE_UPDATE_IDLE;
    portEXIT_CRITICAL(&s_lock);
    const firmware_update_verdict_t verdict = firmware_update_decide(&conditions);
    portENTER_CRITICAL(&s_lock);
    s_status.last_verdict = verdict;
    portEXIT_CRITICAL(&s_lock);
    if (verdict != FIRMWARE_UPDATE_ALLOWED) {
        ESP_LOGW(TAG, "Firmware nuevo pedido desde el mando y rechazado: %s",
                 firmware_update_verdict_name(verdict));
        /* Mientras se confirma una imagen no se pisa su fase. */
        if (verdict == FIRMWARE_UPDATE_REFUSED_BUSY) {
            portENTER_CRITICAL(&s_lock);
            s_status.last_refused_us = (uint64_t)esp_timer_get_time();
            portEXIT_CRITICAL(&s_lock);
        } else {
            refuse();
        }
        return;
    }

    set_phase(FIRMWARE_UPDATE_CHECKING);
    ESP_LOGW(TAG, "Firmware nuevo pedido desde el mando; manifiesto en %s", config.update_url);
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    static char manifest_text[FIRMWARE_UPDATE_MANIFEST_BYTES];
    char url[DEVICE_CONFIG_URL_BYTES + 16U];
    if (target == NULL || !image_url(config.update_url, url, sizeof(url))) {
        ESP_LOGE(TAG, "Sin partición libre o dirección inutilizable: nada que hacer");
        refuse();
        return;
    }
    if (!fetch_manifest(config.update_url, manifest_text, sizeof(manifest_text))) {
        refuse();
        return;
    }
    firmware_manifest_t manifest = {0};
    const firmware_manifest_status_t read = firmware_manifest_parse(
        manifest_text, esp_app_get_description()->project_name,
        s_running_digest_known ? s_running_digest : NULL, (uint32_t)target->size, &manifest);
    portENTER_CRITICAL(&s_lock);
    s_status.last_manifest = read;
    s_status.expected_bytes = manifest.size_bytes;
    s_status.written_bytes = 0U;
    portEXIT_CRITICAL(&s_lock);
    if (read != FIRMWARE_MANIFEST_OK) {
        ESP_LOGW(TAG, "Manifiesto %s: no se instala nada", firmware_manifest_status_name(read));
        refuse();
        return;
    }
    ESP_LOGW(TAG, "Instalando %s (%" PRIu32 " bytes) en %s; la que corre es %s",
             manifest.version, manifest.size_bytes, target->label,
             esp_app_get_description()->version);

    /* A partir de aquí el guardián corta las dos salidas, y no se escribe nada
     * hasta verlo publicado. */
    supervisor_runtime_set_updating(true);
    if (!guard_holds_outputs()) {
        ESP_LOGE(TAG, "El guardián no publicó el corte: no se escribe");
        supervisor_runtime_set_updating(false);
        refuse();
        return;
    }
    set_phase(FIRMWARE_UPDATE_WRITING);
    esp_ota_handle_t handle = 0;
    const bool written = write_image(url, target, &manifest, &handle);
    if (!written) {
        if (handle != 0) {
            (void)esp_ota_abort(handle);
        }
        supervisor_runtime_set_updating(false);
        refuse();
        return;
    }
    /* `esp_ota_end` comprueba que la imagen está entera y es de este chip. */
    const esp_err_t ended = esp_ota_end(handle);
    if (ended != ESP_OK || !image_is_the_published_one(target, &manifest)) {
        ESP_LOGE(TAG, "Imagen rechazada (%s): sigue la que corre", esp_err_to_name(ended));
        supervisor_runtime_set_updating(false);
        refuse();
        return;
    }
    const esp_err_t boot = esp_ota_set_boot_partition(target);
    if (boot != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo elegir %s para arrancar: %s", target->label,
                 esp_err_to_name(boot));
        supervisor_runtime_set_updating(false);
        refuse();
        return;
    }
    set_phase(FIRMWARE_UPDATE_RESTARTING);
    ESP_LOGW(TAG, "Imagen %s instalada en %s; reinicio para arrancarla a prueba",
             manifest.version, target->label);
    vTaskDelay(pdMS_TO_TICKS(200U));
    esp_restart();
}

static void firmware_update_task(void *argument)
{
    (void)argument;
    const esp_partition_t *running = esp_ota_get_running_partition();
    s_running_digest_known =
        running != NULL && esp_partition_get_sha256(running, s_running_digest) == ESP_OK;
    char text[FIRMWARE_UPDATE_DIGEST_BYTES * 2U + 1U] = "desconocida";
    if (s_running_digest_known) {
        hex(s_running_digest, text);
    }
    ESP_LOGI(TAG, "Firmware %s en %s; huella %s",
             esp_app_get_description()->version, running != NULL ? running->label : "?", text);

    confirm_running_image();

    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#if CONFIG_CAFE_FIRMWARE_UPDATE
        run_update();
#else
        ESP_LOGW(TAG, "Firmware nuevo pedido desde el mando, pero este binario no lo admite");
        refuse();
#endif
    }
}

esp_err_t firmware_update_runtime_start(bool core_started)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_core_started = core_started;
    const BaseType_t created =
        xTaskCreate(firmware_update_task, "firmware", FIRMWARE_UPDATE_TASK_STACK_BYTES, NULL,
                    FIRMWARE_UPDATE_TASK_PRIORITY, &s_task);
    if (created != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void firmware_update_runtime_request(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

bool firmware_update_runtime_get_status(firmware_update_status_t *status)
{
    if (status == NULL || s_task == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

bool firmware_update_runtime_busy(void)
{
    portENTER_CRITICAL(&s_lock);
    const firmware_update_phase_t phase = s_status.phase;
    portEXIT_CRITICAL(&s_lock);
    return phase == FIRMWARE_UPDATE_CHECKING || phase == FIRMWARE_UPDATE_WRITING ||
           phase == FIRMWARE_UPDATE_RESTARTING;
}

uint64_t firmware_update_runtime_last_refused_us(void)
{
    portENTER_CRITICAL(&s_lock);
    const uint64_t refused = s_status.last_refused_us;
    portEXIT_CRITICAL(&s_lock);
    return refused;
}
