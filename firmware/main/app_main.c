#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "pin_map.h"
#include "device_config.h"
#include "extraction_runtime.h"
#include "firmware_update_runtime.h"
#include "nvs_outbox.h"
#include "operational_fsm.h"
#include "operational_runtime.h"
#include "profile_runtime.h"
#include "simulation_runtime.h"
#include "external_hil_runtime.h"
#include "acquisition_runtime.h"
#include "process_variables.h"
#include "control_log_runtime.h"
#include "control_runtime.h"
#include "indicator_runtime.h"
#include "hx711_runtime.h"
#include "panel_runtime.h"
#include "supervisor_runtime.h"
#include "zero_cross_runtime.h"
#include "ads1115.h"
#include "sync_runtime.h"

#define MB_BYTES (1024U * 1024U)
#define WIFI_SCAN_RESULT_LIMIT 10U
#define HEARTBEAT_PERIOD_MS 5000U

/*
 * Kconfig no define el símbolo de una opción booleana desactivada, de modo que
 * usarlo en una expresión solo compila mientras esté activo. El lazo de banco
 * venía activo por omisión y nadie lo había notado; el binario de
 * identificación lo exige desactivado.
 */
#if CONFIG_CAFE_CONTROL_BENCH_LOOP
#define BENCH_LOOP_NAME "sí"
#else
#define BENCH_LOOP_NAME "no"
#endif

static const char *TAG = "board_diag";
static nvs_outbox_t s_outbox;

static const gpio_num_t s_led_pins[] = {
    PIN_LED_SAFE,
    PIN_LED_READY,
    PIN_LED_BREW,
    PIN_LED_ALARM,
};

static void log_check(const char *name, bool passed, const char *detail)
{
    if (passed) {
        ESP_LOGI(TAG, "[PASS] %s: %s", name, detail);
    } else {
        ESP_LOGE(TAG, "[FAIL] %s: %s", name, detail);
    }
}

/*
 * Cuál de los dos binarios corre (ADR-007 § 5, enmienda del 2026-09-21).
 *
 * Se declara siempre, también en el de operación, para que una captura diga por
 * sí sola en qué condiciones se tomó. En el de identificación, una configuración
 * rechazada se marca como fallo: el binario se comporta entonces como el de
 * operación, y quien lo compiló para ensayar debe enterarse antes de la sesión y
 * no al final.
 *
 * Va en función propia por la pila de la tarea principal: el texto ocupa un
 * cuarto de kilobyte y así se libera al volver, en vez de sumarse al pico del
 * diagnóstico.
 */
static void log_binary(const identification_status_t *identification)
{
#if CONFIG_CAFE_IDENTIFICATION
    char detail[224];
    snprintf(detail, sizeof(detail),
             "IDENTIFICACIÓN; secuencia=%s; tope=%.0f%% (provisional); configuración=%s; "
             "confirmar con café y tara %" PRIu32 "s, antes de encender y dentro de %" PRIu32
             "s",
             stimulus_sequence_name(identification->sequence),
             (double)identification->maximum_percent,
             identification_setup_name(identification->setup),
             IDENTIFICATION_CONFIRMATION_HOLD_MS / 1000U,
             IDENTIFICATION_CONFIRMATION_WINDOW_MS / 1000U);
    log_check("binario", identification->setup == IDENTIFICATION_SETUP_ACCEPTED, detail);
#else
    (void)identification;
    log_check("binario", true, "operación; el modo restringido no está compilado");
#endif
}

/*
 * Sintonía de cada lazo y si está puesto en servicio (ADR-020 § 4.2).
 *
 * Se declara por lazo porque las dos marcas son independientes: el de
 * temperatura se pone en servicio antes que el de presión. El identificador es
 * el del acta cuando el firmware lleva planta identificada, y `bench-v1` cuando
 * no. En función propia por la pila de la tarea principal.
 */
static void log_control_tuning(esp_err_t control_result)
{
    if (control_result != ESP_OK) {
        log_check("lazos de control", false, esp_err_to_name(control_result));
        return;
    }
    const control_tuning_t loops[] = {control_runtime_pressure_tuning(),
                                      control_runtime_temperature_tuning()};
    const char *names[] = {"presión", "temperatura"};
    for (size_t index = 0U; index < sizeof(loops) / sizeof(loops[0]); index++) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                 "%s: sintonía=%s; Kp=%.3f Ki=%.4f cada %.0fms; puesta en servicio=%s; "
                 "lazo de banco=%s",
                 names[index], loops[index].identifier,
                 (double)loops[index].pi.proportional_gain,
                 (double)loops[index].pi.integral_gain,
                 (double)(loops[index].pi.sample_period_s * 1000.0F),
                 loops[index].commissioned ? "SÍ, planta identificada"
                                           : "no; salida inhibida",
                 BENCH_LOOP_NAME);
        log_check("lazos de control", true, detail);
    }
}

static bool diagnose_memory(void)
{
    esp_chip_info_t chip_info = {0};
    uint32_t flash_size = 0;
    size_t psram_size = 0;
    char detail[128];
    bool all_ok = true;

    esp_chip_info(&chip_info);
    const bool chip_ok = chip_info.model == CHIP_ESP32S3 && chip_info.cores == 2;
    snprintf(detail, sizeof(detail), "modelo=%" PRIu32 ", núcleos=%u, revisión=%u.%u",
             (uint32_t)chip_info.model, chip_info.cores,
             chip_info.revision / 100U, chip_info.revision % 100U);
    log_check("ESP32-S3 de doble núcleo", chip_ok, detail);
    all_ok &= chip_ok;

    const esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size);
    const uint32_t expected_flash = CONFIG_CAFE_EXPECTED_FLASH_MB * MB_BYTES;
    const bool flash_ok = flash_result == ESP_OK && flash_size == expected_flash;
    snprintf(detail, sizeof(detail), "detectada=%" PRIu32 " MB, esperada=%d MB",
             flash_size / MB_BYTES, CONFIG_CAFE_EXPECTED_FLASH_MB);
    log_check("memoria flash", flash_ok, detail);
    all_ok &= flash_ok;

    const bool psram_initialized = esp_psram_is_initialized();
    if (psram_initialized) {
        psram_size = esp_psram_get_size();
    }
    const size_t expected_psram = (size_t)CONFIG_CAFE_EXPECTED_PSRAM_MB * MB_BYTES;
    const bool psram_ok = psram_initialized && psram_size == expected_psram;
    snprintf(detail, sizeof(detail), "detectada=%zu MB, esperada=%d MB, inicializada=%s",
             psram_size / MB_BYTES, CONFIG_CAFE_EXPECTED_PSRAM_MB,
             psram_initialized ? "sí" : "no");
    log_check("PSRAM", psram_ok, detail);
    all_ok &= psram_ok;

    ESP_LOGI(TAG, "heap interno libre=%" PRIu32 " bytes; PSRAM libre=%zu bytes",
             esp_get_free_internal_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    return all_ok;
}

static bool configure_gpio_safely(void)
{
    const gpio_config_t actuator_config = {
        .pin_bit_mask = (1ULL << PIN_PUMP_CMD) | (1ULL << PIN_HEATER_CMD),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&actuator_config));
    ESP_ERROR_CHECK(gpio_set_level(PIN_PUMP_CMD, 0));
    ESP_ERROR_CHECK(gpio_set_level(PIN_HEATER_CMD, 0));

    const gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << PIN_LED_SAFE) | (1ULL << PIN_LED_READY) |
                        (1ULL << PIN_LED_BREW) | (1ULL << PIN_LED_ALARM),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_config));
    for (size_t index = 0; index < sizeof(s_led_pins) / sizeof(s_led_pins[0]); ++index) {
        ESP_ERROR_CHECK(gpio_set_level(s_led_pins[index], 0));
    }

    const gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << PIN_BTN_POWER_N) | (1ULL << PIN_BTN_BREW_N) |
                        (1ULL << PIN_BTN_STEAM_N) | (1ULL << PIN_BTN_PURGE_N),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));

    /*
     * `PUMP_ZC` reposa en **alto** y marca cada cruce con un pulso bajo de unos
     * 100 us, según la documentación del módulo adquirido. El *pull-up* es por
     * tanto el que acompaña a la señal, no el *pull-down* que se supuso antes de
     * conocerla: con la salida en colector abierto, un *pull-down* impediría ver
     * el nivel de reposo y la señal parecería un cero permanente.
     *
     * La inhibición ante desconexión no depende de esta elección: se declara por
     * **ausencia de flancos**, de modo que un pin clavado en cualquier nivel
     * detiene la bomba igual.
     */
    const gpio_config_t zc_config = {
        .pin_bit_mask = 1ULL << PIN_PUMP_ZC,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&zc_config));

    const gpio_config_t i2c_idle_config = {
        .pin_bit_mask = (1ULL << PIN_I2C_SDA) | (1ULL << PIN_I2C_SCL),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&i2c_idle_config));

    const bool safe = gpio_get_level(PIN_PUMP_CMD) == 0 &&
                      gpio_get_level(PIN_HEATER_CMD) == 0;
    log_check("salidas de potencia en estado seguro", safe,
              "PUMP_CMD=0 y HEATER_CMD=0; no se ejecuta una prueba de activación");
    return safe;
}

static bool initialize_persistent_storage(void)
{
    const esp_err_t result = nvs_flash_init();
    const bool passed = result == ESP_OK;
    log_check("almacenamiento NVS", passed,
              passed ? "partición disponible; no se borra ante errores"
                     : esp_err_to_name(result));
    return passed;
}

static bool diagnose_persistent_outbox(void)
{
    nvs_outbox_t diagnostic = {0};
    bool first_inserted = false;
    bool duplicate_inserted = true;
    bool second_inserted = false;
    size_t count = 0U;
    size_t batch_count = 0U;
    size_t removed = 0U;
    bool reboot_persistence_confirmed = false;
    nvs_outbox_item_t *batch = calloc(2U, sizeof(*batch));
    const char *acknowledged[] = {"diag-message-0001"};

    esp_err_t result = batch == NULL ? ESP_ERR_NO_MEM
                                     : nvs_outbox_initialize(&diagnostic, "eko_diagq");
    if (result == ESP_OK) {
        result = nvs_outbox_count(&diagnostic, &count);
    }
    if (result == ESP_OK && count > 0U) {
        size_t previous_count = 0U;
        result = nvs_outbox_peek_batch(&diagnostic, batch, 1U, &previous_count);
        reboot_persistence_confirmed = result == ESP_OK && previous_count == 1U &&
            strcmp(batch[0].message_id, "diag-message-0002") == 0;
    }
    if (result == ESP_OK) {
        result = nvs_outbox_clear(&diagnostic);
    }
    if (result == ESP_OK) {
        result = nvs_outbox_enqueue(&diagnostic, "diag-message-0001",
                                    "{\"messageId\":\"diag-message-0001\"}",
                                    &first_inserted);
    }
    if (result == ESP_OK) {
        result = nvs_outbox_enqueue(&diagnostic, "diag-message-0001",
                                    "{\"messageId\":\"diag-message-0001\"}",
                                    &duplicate_inserted);
    }
    if (result == ESP_OK) {
        result = nvs_outbox_enqueue(&diagnostic, "diag-message-0002",
                                    "{\"messageId\":\"diag-message-0002\"}",
                                    &second_inserted);
    }
    if (result == ESP_OK) {
        result = nvs_outbox_count(&diagnostic, &count);
    }
    if (result == ESP_OK) {
        result = nvs_outbox_peek_batch(&diagnostic, batch, 2U, &batch_count);
    }
    if (result == ESP_OK) {
        result = nvs_outbox_acknowledge(&diagnostic, acknowledged, 1U, &removed);
    }
    if (result == ESP_OK) {
        result = nvs_outbox_count(&diagnostic, &count);
    }

    const bool passed = result == ESP_OK && first_inserted && !duplicate_inserted &&
                        second_inserted && batch_count == 2U && removed == 1U &&
                        count == 1U && batch != NULL &&
                        strcmp(batch[0].message_id, "diag-message-0001") == 0 &&
                        strcmp(batch[1].message_id, "diag-message-0002") == 0;
    if (diagnostic.initialized) {
        /* Se conserva el segundo mensaje para verificar persistencia al reiniciar. */
        nvs_outbox_deinitialize(&diagnostic);
    }
    free(batch);

    esp_err_t production_result = result;
    if (passed) {
        production_result = nvs_outbox_initialize(&s_outbox, "eko_outbox");
        if (production_result == ESP_OK) {
            production_result = nvs_outbox_count(&s_outbox, &count);
        }
    }
    const bool ready = passed && production_result == ESP_OK;
    char detail[128];
    if (ready) {
        snprintf(detail, sizeof(detail),
                 "orden/ACK aprobados; reinicio=%s; pendientes reales=%zu/%u",
                 reboot_persistence_confirmed ? "confirmado" : "por repetir",
                 count, NVS_OUTBOX_CAPACITY);
    } else {
        snprintf(detail, sizeof(detail), "error=%s",
                 esp_err_to_name(production_result != ESP_OK ? production_result : result));
    }
    log_check("cola persistente de sincronización", ready, detail);
    return ready;
}

static void run_led_gpio_test(void)
{
#if CONFIG_CAFE_LED_TEST
    ESP_LOGI(TAG, "Prueba secuencial de LED: seguro, listo, café y alarma, en ese orden");
    for (size_t index = 0; index < sizeof(s_led_pins) / sizeof(s_led_pins[0]); ++index) {
        ESP_ERROR_CHECK(gpio_set_level(s_led_pins[index], 1));
        vTaskDelay(pdMS_TO_TICKS(250));
        ESP_ERROR_CHECK(gpio_set_level(s_led_pins[index], 0));
    }
    log_check("GPIO de indicadores", true, "secuencia terminada; confirmar visualmente con PCB-MANDO");
#else
    ESP_LOGW(TAG, "Prueba visible de GPIO de LED deshabilitada por configuración");
#endif
}

static bool run_wifi_scan(void)
{
#if CONFIG_CAFE_WIFI_SCAN
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        log_check("radio Wi-Fi", false, esp_err_to_name(result));
        return false;
    }

    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        log_check("radio Wi-Fi", false, esp_err_to_name(result));
        return false;
    }

    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (station == NULL) {
        log_check("radio Wi-Fi", false, "no se pudo crear la interfaz STA");
        return false;
    }

    const wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    if ((result = esp_wifi_init(&wifi_config)) != ESP_OK ||
        (result = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (result = esp_wifi_start()) != ESP_OK ||
        (result = esp_wifi_scan_start(NULL, true)) != ESP_OK) {
        log_check("radio Wi-Fi", false, esp_err_to_name(result));
        return false;
    }

    uint16_t total = 0;
    uint16_t returned = WIFI_SCAN_RESULT_LIMIT;
    wifi_ap_record_t records[WIFI_SCAN_RESULT_LIMIT] = {0};
    result = esp_wifi_scan_get_ap_num(&total);
    if (result == ESP_OK) {
        result = esp_wifi_scan_get_ap_records(&returned, records);
    }

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Escaneo Wi-Fi completado: %u redes detectadas", total);
        for (uint16_t index = 0; index < returned; ++index) {
            ESP_LOGI(TAG, "  AP[%u] canal=%u RSSI=%d dBm SSID=%s",
                     index, records[index].primary, records[index].rssi,
                     (const char *)records[index].ssid);
        }
    }

    const esp_err_t stop_result = esp_wifi_stop();
    const esp_err_t deinit_result = esp_wifi_deinit();
    esp_netif_destroy_default_wifi(station);

    const bool wifi_ok = result == ESP_OK && stop_result == ESP_OK && deinit_result == ESP_OK;
    log_check("radio Wi-Fi", wifi_ok,
              wifi_ok ? "escaneo ejecutado sin guardar credenciales" : esp_err_to_name(result));
    return wifi_ok;
#else
    ESP_LOGW(TAG, "Escaneo Wi-Fi deshabilitado por configuración");
    return true;
#endif
}

/*
 * Arranca la configuración efectiva y declara cuál rige.
 *
 * Igual que la comprobación del enlace, vive fuera de app_main para no dejar su
 * texto ocupando el marco de la tarea principal durante el resto del
 * diagnóstico: ahí la pila va justa y se mide al final.
 *
 * Antes decía siempre «perfil predeterminado de simulación», lo que dejó de ser
 * cierto en cuanto una configuración guardada pudo desplazar a la compilada. Un
 * diagnóstico que afirma lo que no ha mirado es peor que uno que calla.
 */
__attribute__((noinline)) static bool log_effective_profile(void)
{
    const esp_err_t result = profile_runtime_start();
    if (result != ESP_OK) {
        log_check("configuración efectiva", false, esp_err_to_name(result));
        return false;
    }
    char detail[96];
    extraction_profile_t effective = {0};
    if (profile_runtime_get_effective(&effective)) {
        snprintf(detail, sizeof(detail), "rige %s v%" PRIu32, effective.id, effective.version);
    } else {
        snprintf(detail, sizeof(detail), "iniciada, sin configuración legible");
    }
    log_check("configuración efectiva", true, detail);
    return true;
}

#if CONFIG_CAFE_SYNC
/* La configuración guardada puede desplazar a la compilada, de modo que mirar la
 * configuración de compilación ya no responde a dónde apunta este dispositivo:
 * el diagnóstico lo declara. La credencial no se imprime.
 *
 * Vive en su propia función y no dentro de app_main a propósito. La copia leída
 * y el texto compuesto suman casi medio kilobyte, y declarados en app_main
 * seguirían ocupando su marco durante todo lo que viene después —incluido el
 * escaneo Wi-Fi, que es la llamada más profunda del diagnóstico—. Así desbordó
 * la pila de la tarea principal la primera vez que se grabó esta comprobación,
 * que son 3584 bytes fijados por el arranque de ESP-IDF y que este proyecto no
 * dimensiona. Aquí ese espacio existe solo mientras se usa. `noinline` impide
 * que el compilador deshaga la separación y devuelva el problema. */
__attribute__((noinline)) static void log_link_configuration(void)
{
    device_config_t link_config;
    device_config_load(&link_config);
    char link_detail[DEVICE_CONFIG_URL_BYTES + 96U];
    snprintf(link_detail, sizeof(link_detail), "%s (dirección %s, credencial %s)",
             link_config.sync_url, device_config_origin_name(link_config.url_origin),
             device_config_origin_name(link_config.token_origin));
    log_check("configuración del enlace", true, link_detail);
    snprintf(link_detail, sizeof(link_detail), "%s (%s)",
             link_config.update_url[0] != '\0' ? link_config.update_url : "ninguno",
             device_config_origin_name(link_config.update_url_origin));
    log_check("manifiesto del firmware", true, link_detail);
}
#endif

static void log_input_levels(void)
{
    ESP_LOGI(TAG,
             "Entradas: POWER_N=%d BREW_N=%d STEAM_N=%d PURGE_N=%d PUMP_ZC=%d SDA=%d SCL=%d",
             gpio_get_level(PIN_BTN_POWER_N), gpio_get_level(PIN_BTN_BREW_N),
             gpio_get_level(PIN_BTN_STEAM_N), gpio_get_level(PIN_BTN_PURGE_N),
             gpio_get_level(PIN_PUMP_ZC), gpio_get_level(PIN_I2C_SDA),
             gpio_get_level(PIN_I2C_SCL));
}

static bool expect_runtime_transition(uint32_t pending_events,
                                      bool expected_accepted,
                                      operational_state_t expected_state,
                                      operational_transition_t *observed)
{
    operational_transition_t transition = {0};
    const bool posted = operational_runtime_post_events(
        pending_events, pdMS_TO_TICKS(100U));
    const bool received = posted && operational_runtime_receive_transition(
        &transition, pdMS_TO_TICKS(1000U));
    const bool passed = received &&
                        transition.accepted == expected_accepted &&
                        transition.to == expected_state &&
                        operational_runtime_state() == expected_state;
    if (observed != NULL) {
        *observed = transition;
    }
    return passed;
}

static bool diagnose_operational_state_machine(void)
{
    bool passed = operational_runtime_state() == OP_STATE_SAFE_INACTIVE;

    /* Un inicio fuera de preparada debe rechazarse. */
    passed &= expect_runtime_transition(
        OP_EVENT_MASK(OP_EVENT_BREW_BUTTON_PRESSED), false,
        OP_STATE_SAFE_INACTIVE, NULL);

    /* Recorrido de extracción y parada normal. */
    passed &= expect_runtime_transition(
        OP_EVENT_MASK(OP_EVENT_LOGICAL_POWER_TOGGLE), true,
        OP_STATE_COFFEE_HEATING, NULL);
    passed &= expect_runtime_transition(
        OP_EVENT_MASK(OP_EVENT_COFFEE_TEMPERATURE_READY), true,
        OP_STATE_READY, NULL);
    passed &= expect_runtime_transition(
        OP_EVENT_MASK(OP_EVENT_BREW_BUTTON_PRESSED), true,
        OP_STATE_EXTRACTION, NULL);
    operational_transition_t stop = {0};
    passed &= expect_runtime_transition(
        OP_EVENT_MASK(OP_EVENT_BREW_BUTTON_PRESSED), true,
        OP_STATE_READY, &stop);
    passed &= stop.extraction_finished &&
              stop.extraction_termination == OP_TERMINATION_MANUAL_NORMAL_STOP;

    /* Una protección simultánea prevalece sobre parada y tiempo máximo. */
    passed &= expect_runtime_transition(
        OP_EVENT_MASK(OP_EVENT_BREW_BUTTON_PRESSED), true,
        OP_STATE_EXTRACTION, NULL);
    const uint32_t simultaneous_termination =
        OP_EVENT_MASK(OP_EVENT_SAFETY_TRIP) |
        OP_EVENT_MASK(OP_EVENT_BREW_BUTTON_PRESSED) |
        OP_EVENT_MASK(OP_EVENT_EXTRACTION_TIME_LIMIT);
    operational_transition_t trip = {0};
    passed &= expect_runtime_transition(simultaneous_termination, true,
                                        OP_STATE_ALARM, &trip);
    passed &= trip.event == OP_EVENT_SAFETY_TRIP &&
              trip.extraction_termination == OP_TERMINATION_SAFETY_PROTECTION_TRIP;
    passed &= expect_runtime_transition(
        OP_EVENT_MASK(OP_EVENT_SAFETY_CLEARED), true,
        OP_STATE_SAFE_INACTIVE, NULL);

    log_check("máquina de estados operativa", passed,
              "tarea FreeRTOS y cola verificadas; ninguna autorización se aplicó a los GPIO");
    return passed;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inicio del diagnóstico ESP32-S3-N16R8");
    ESP_LOGI(TAG, "ESP-IDF %s", esp_get_idf_version());

    const bool memory_ok = diagnose_memory();
    const bool gpio_ok = configure_gpio_safely();
    const bool storage_ok = initialize_persistent_storage();
    const bool outbox_ok = storage_ok && diagnose_persistent_outbox();
    const bool profile_ok = log_effective_profile();
    const esp_err_t runtime_result = operational_runtime_start();
    const bool runtime_ok = runtime_result == ESP_OK;
    log_check("tarea FreeRTOS operativa", runtime_ok,
              runtime_ok ? "cola y tarea creadas" : esp_err_to_name(runtime_result));
    const bool fsm_ok = runtime_ok && diagnose_operational_state_machine();
#if CONFIG_CAFE_SIMULATION
    const esp_err_t simulation_result = fsm_ok
                                            ? simulation_runtime_start()
                                            : ESP_ERR_INVALID_STATE;
    const bool simulation_ok = simulation_result == ESP_OK;
    log_check("tarea FreeRTOS de simulación", simulation_ok,
              simulation_ok ? "planta virtual iniciada; GPIO de potencia aislados"
                            : esp_err_to_name(simulation_result));
#else
    const bool simulation_ok = true;
    ESP_LOGW(TAG, "Planta simulada deshabilitada por configuración");
#endif
    const esp_err_t extraction_result = simulation_ok
                                            ? extraction_runtime_start(outbox_ok ? &s_outbox : NULL)
                                            : ESP_ERR_INVALID_STATE;
    const bool extraction_ok = extraction_result == ESP_OK;
    log_check("registrador de extracciones", extraction_ok,
              extraction_ok ? "sesiones y muestras preparadas"
                            : esp_err_to_name(extraction_result));
    /* La adquisición se inicia aunque no haya sensores: publicará medidas sin
     * adquirir, que es información honesta, en lugar de callar o inventar. */
    const esp_err_t acquisition_result = acquisition_runtime_start();
    acquisition_status_t acquisition = {0};
    (void)acquisition_runtime_get_status(&acquisition);
    char acquisition_detail[128];
    process_snapshot_t process = {0};
    (void)process_variables_get(&process);
    snprintf(acquisition_detail, sizeof(acquisition_detail),
             "convertidor %s en 0x%02x; calidad T=%s P=%s; origen vigente=%s; calibración=%s",
             acquisition.converter_present ? "presente" : "ausente",
             ADS1115_DEFAULT_ADDRESS,
             signal_quality_name(acquisition.temperature.quality),
             signal_quality_name(acquisition.pressure.quality),
             process_source_name(process.source),
             acquisition.calibration_persisted ? "persistida" : "nominal");
    log_check("adquisición de temperatura y presión",
              acquisition_result == ESP_OK, acquisition_detail);

    /*
     * El registro del lazo se inicia antes que cualquier control porque es su
     * condición: ADR-007 § 3.6 y ADR-009 § 3.9 exigen que cada prueba del lazo
     * quede conservada, y un lazo que corriera sin registro produciría ensayos
     * irrepetibles. Hoy no recibe muestras —los lazos llegan en el paso 7—,
     * pero emite sus cabeceras, de modo que una captura de banco ya muestra el
     * formato exacto que tendrán los datos.
     */
    const esp_err_t control_log_result =
        control_log_runtime_start(CONTROL_LOG_DEFAULT_CAPACITY);
    char control_log_detail[128];
    snprintf(control_log_detail, sizeof(control_log_detail),
             "%" PRIu32 " registros en PSRAM; cabeceras emitidas; consola a %d baudios",
             control_log_runtime_capacity(), CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    log_check("registro del lazo", control_log_result == ESP_OK,
              control_log_result == ESP_OK ? control_log_detail
                                           : esp_err_to_name(control_log_result));

    /*
     * La báscula es instrumentación de ensayo (ADR-006) y no una función de la
     * cafetera: no entra en el guardián, no inhibe nada y su ausencia no afecta
     * a ningún otro componente. Si no está conectada, la tarea publica lecturas
     * no adquiridas y el resto del firmware se comporta igual.
     */
    const esp_err_t scale_result = hx711_runtime_start();
    log_check("báscula de ensayo", scale_result == ESP_OK,
              scale_result == ESP_OK
                  ? "HX711 en GPIO10/GPIO11; gramos solo con factor en la calibración guardada"
                  : esp_err_to_name(scale_result));

    /* La vigilancia del cruce por cero se inicia antes que el supervisor porque
     * este la consulta. No actúa sobre nada: solo cuenta flancos y decide si la
     * señal podría autorizar la bomba, de modo que arrancarla sin el lazo
     * escrito es seguro y permite verificarla en banco. */
    const esp_err_t zero_cross_result = zero_cross_runtime_start(NULL);
    /*
     * Aquí solo puede comprobarse que la interrupción quedó instalada.
     *
     * Medir la señal en este punto no diría nada: a 120 Hz hacen falta unos
     * 42 ms para encadenar los cinco periodos que la declaran apta, y han
     * pasado microsegundos. Un recuento de cero flancos sería cierto y
     * engañoso a la vez, porque parecería un fallo del módulo cuando solo
     * describe que el diagnóstico llegó antes que el primer cruce. La medida
     * real se publica en el latido, que se repite cada cinco segundos.
     */
    log_check("vigilancia de cruce por cero", zero_cross_result == ESP_OK,
              zero_cross_result == ESP_OK
                  ? "interrupción instalada en PUMP_ZC; la medida aparece en el latido"
                  : esp_err_to_name(zero_cross_result));

    /* La supervisión se inicia después de la adquisición porque se alimenta de
     * ella, y publica los eventos que antes producía la planta simulada. */
    const esp_err_t supervisor_result = supervisor_runtime_start();
    supervisor_status_t supervisor = {0};
    (void)supervisor_runtime_get_status(&supervisor);
    char supervisor_detail[128];
    snprintf(supervisor_detail, sizeof(supervisor_detail),
             "periodo=50ms; referencia=%.1fC; tiempo máximo=%" PRIu32
             "ms; límite duro=%.1fC/%.1fbar; bomba=%s; calentador=%s",
             (double)supervisor.limits.coffee_setpoint_c,
             supervisor.limits.maximum_extraction_ms,
             (double)supervisor.limits.hard_overtemperature_c,
             (double)supervisor.limits.hard_overpressure_bar,
             actuation_verdict_name(supervisor.actuation.pump),
             actuation_verdict_name(supervisor.actuation.heater));
    log_check("supervisión del proceso", supervisor_result == ESP_OK,
              supervisor_result == ESP_OK ? supervisor_detail
                                          : esp_err_to_name(supervisor_result));

    log_binary(&supervisor.identification);

    /*
     * Los lazos se inician los últimos de la cadena de control porque dependen
     * de todo lo anterior: leen la adquisición, preguntan el veredicto al
     * supervisor y escriben en el registro. Sin planta identificada ninguna
     * salida puede energizarse —`commissioned` es falso y el guardián inhibe
     * por esa causa—, de modo que arrancarlos aquí es seguro y permite observar
     * el control antes de que exista una máquina que gobernar.
     */
    const esp_err_t control_result = supervisor_result == ESP_OK
                                         ? control_runtime_start()
                                         : ESP_ERR_INVALID_STATE;
    log_control_tuning(control_result);

    run_led_gpio_test();

    /*
     * Los indicadores arrancan **después** de la prueba visual, o las dos cosas
     * se disputarían los mismos pines y la secuencia de comprobación no se
     * vería. A partir de aquí los LED dejan de ser una prueba y pasan a informar
     * del estado de la máquina, que es lo que exige RF-011.
     */
    const esp_err_t indicator_result = indicator_runtime_start();
    log_check("indicadores del mando", indicator_result == ESP_OK,
              indicator_result == ESP_OK
                  ? "estado y averías reflejados en SAFE, READY, BREW y ALARM"
                  : esp_err_to_name(indicator_result));

    /*
     * El mando se lee el último de todo, y a propósito: a partir de aquí el
     * barista puede ordenar, y conviene que la máquina esté entera antes de
     * aceptar una orden. Un pulsador ya apretado en este instante no produce
     * nada, porque la primera muestra de cada botón solo fija la referencia.
     */
    const esp_err_t panel_result = panel_runtime_start();
    char panel_detail[128];
    snprintf(panel_detail, sizeof(panel_detail),
             "cuatro pulsadores activos a nivel bajo; %ums de antirrebote; el canal "
             "de banco sigue disponible",
             (unsigned)(PANEL_SAMPLE_PERIOD_MS * PANEL_REQUIRED_SAMPLES));
    log_check("mando físico del barista", panel_result == ESP_OK,
              panel_result == ESP_OK ? panel_detail : esp_err_to_name(panel_result));

    log_input_levels();
    const bool wifi_ok = storage_ok && run_wifi_scan();

#if CONFIG_CAFE_SYNC
    /* El enlace solo entrega registros y obtiene la hora: no abre ninguna vía
     * capaz de iniciar una preparación ni de accionar salidas (RD-002). */
    const esp_err_t sync_result = outbox_ok ? sync_runtime_start(&s_outbox)
                                            : ESP_ERR_INVALID_STATE;
    log_check("enlace con la plataforma", sync_result == ESP_OK,
              sync_result == ESP_OK ? "Wi-Fi y entrega de registros iniciados"
                                    : esp_err_to_name(sync_result));

    log_link_configuration();
#endif

    /*
     * Después del enlace, porque para declarar válida una imagen recién
     * instalada hace falta ver la red (ADR-021). Se inicia aunque el binario no
     * admita actualizar por Wi-Fi: una imagen que llegó así y no se confirma
     * vuelve atrás en el siguiente reinicio.
     */
    const esp_err_t update_result = firmware_update_runtime_start(
        acquisition_result == ESP_OK && supervisor_result == ESP_OK);
    log_check("actualización del firmware", update_result == ESP_OK,
              update_result == ESP_OK
#if CONFIG_CAFE_FIRMWARE_UPDATE
                  ? "tara y purgado 3 s con la máquina apagada; dos copias y vuelta atrás"
#else
                  ? "no admitida en este binario; se confirma la imagen si llegó por Wi-Fi"
#endif
                  : esp_err_to_name(update_result));

    const esp_err_t hil_result = external_hil_runtime_start();
    log_check("canal HIL externo", hil_result == ESP_OK,
              hil_result == ESP_OK ? "protocolo serie I/O activo; GPIO aislados"
                                    : esp_err_to_name(hil_result));

    /* El diagnóstico corre en la tarea principal, cuya pila la fija el arranque
     * de ESP-IDF. Una comprobación que reserve de más la desborda sin aviso
     * previo, y eso ya ocurrió: se informa del margen para que el siguiente que
     * añada algo aquí sepa cuánto le queda antes de que reviente. */
    ESP_LOGI(TAG, "margen de pila de la tarea principal=%u bytes de %d",
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             CONFIG_ESP_MAIN_TASK_STACK_SIZE);

    const bool overall_ok = memory_ok && gpio_ok && storage_ok && outbox_ok &&
                            profile_ok && fsm_ok && simulation_ok &&
                            extraction_ok && wifi_ok &&
                            supervisor_result == ESP_OK;
    log_check("RESULTADO GENERAL", overall_ok,
              overall_ok ? "diagnóstico automático aprobado" : "revisar los fallos anteriores");

    while (true) {
        log_input_levels();
        supervisor_status_t vigilance = {0};
        if (supervisor_runtime_get_status(&vigilance)) {
            ESP_LOGI(TAG,
                     "guardián: bomba=%s calentador=%s; estado=%s; origen=%s; "
                     "ciclos con inhibición=%" PRIu32,
                     actuation_verdict_name(vigilance.actuation.pump),
                     actuation_verdict_name(vigilance.actuation.heater),
                     operational_state_name(vigilance.state),
                     process_source_name(vigilance.source),
                     vigilance.inhibition_cycles);
        }
#if CONFIG_CAFE_IDENTIFICATION
        if (supervisor_runtime_get_status(&vigilance)) {
            ESP_LOGW(TAG,
                     "modo restringido: %s; secuencia=%s; recorrido=%" PRIu32
                     "ms; demanda admitida=%.0f%%; interrumpida por=%s",
                     identification_phase_name(vigilance.identification.phase),
                     stimulus_sequence_name(vigilance.identification.sequence),
                     vigilance.identification.running_ms,
                     (double)vigilance.identification.demand_percent,
                     actuation_verdict_name(vigilance.identification.stop_verdict));
        }
#endif
        /* La medida real se imprime aparte del lazo: mientras falte uno de los
         * dos canales, el lazo trabaja con la simulación y sus series no
         * muestran lo que lee el convertidor, que es justo lo que la puesta en
         * servicio necesita ver sin la red (lista de comprobación, C7 y C8). */
        acquisition_status_t acquisition = {0};
        measurement_t measured_temperature = {0};
        measurement_t measured_pressure = {0};
        if (acquisition_runtime_get_status(&acquisition) &&
            acquisition_runtime_get_temperature(&measured_temperature) &&
            acquisition_runtime_get_pressure(&measured_pressure)) {
            ESP_LOGI(TAG,
                     "adquisición: convertidor=%s; T=%.2fC (%s, %d cuentas, %.4fV); "
                     "P=%.3fbar (%s, %d cuentas, %.4fV); ciclos=%" PRIu32
                     "; fallos de lectura=%" PRIu32,
                     acquisition.converter_present ? "presente" : "ausente",
                     (double)measured_temperature.value,
                     signal_quality_name(measured_temperature.quality),
                     measured_temperature.counts, (double)measured_temperature.volts,
                     (double)measured_pressure.value,
                     signal_quality_name(measured_pressure.quality),
                     measured_pressure.counts, (double)measured_pressure.volts,
                     acquisition.cycles, acquisition.read_failures);
        }
        zero_cross_snapshot_t zero_cross = {0};
        if (zero_cross_runtime_get_snapshot(&zero_cross)) {
            ESP_LOGI(TAG,
                     "cruce por cero: flancos=%" PRIu32 "; rechazados=%" PRIu32
                     " (cortos=%" PRIu32 " entre %" PRIu32 " y %" PRIu32 "us)"
                     "; intervalo=%" PRIu32 "us (%.1fHz); apta=%s; "
                     "sin flanco desde hace %" PRIu32 "ms",
                     zero_cross.total_events, zero_cross.rejected_events,
                     zero_cross.short_rejections, zero_cross.shortest_short_us,
                     zero_cross.longest_short_us,
                     zero_cross.last_interval_us,
                     (double)zero_cross.observed_frequency_hz,
                     zero_cross.usable ? "sí" : "no", zero_cross.age_ms);
        }
        panel_status_t panel = {0};
        if (panel_runtime_get_status(&panel)) {
            ESP_LOGI(TAG,
                     "mando: encendido=%d café=%d tara=%d purgado=%d (%" PRIu32
                     "ms); eventos publicados=%" PRIu32 "; descartados=%" PRIu32
                     "; taras tomadas=%" PRIu32 "; rechazadas=%" PRIu32
                     "; retenidos: encendido=%d café=%d tara=%d purgado=%d (episodios=%" PRIu32 ")",
                     panel.pressed.power ? 1 : 0, panel.pressed.brew ? 1 : 0,
                     panel.pressed.tare ? 1 : 0, panel.pressed.purge ? 1 : 0,
                     panel.purge_held_ms, panel.published_events, panel.dropped_events,
                     panel.tares_taken, panel.tares_refused,
                     panel.held.power ? 1 : 0, panel.held.brew ? 1 : 0,
                     panel.held.tare ? 1 : 0, panel.held.purge ? 1 : 0,
                     panel.held_occurrences);
        }
        scale_status_t scale = {0};
        if (hx711_runtime_get_status(&scale) && scale.present) {
            ESP_LOGI(TAG,
                     "báscula: %" PRId32 " cuentas (%s); masa=%s; asentada=%s; "
                     "recorrido=%" PRIu32 "; conversiones=%" PRIu32,
                     scale.reading.counts, signal_quality_name(scale.reading.quality),
                     scale.reading.mass_available ? "disponible" : "sin calibrar",
                     scale.stable ? "sí" : "no", scale.spread_counts,
                     scale.conversions);
        }
        indicator_status_t indicators = {0};
        if (indicator_runtime_get_status(&indicators)) {
            ESP_LOGI(TAG, "indicadores: estado=%s; verde=%s azul=%s amarillo=%s rojo=%s",
                     operational_state_name(indicators.state),
                     indicator_pattern_name(indicators.panel.green),
                     indicator_pattern_name(indicators.panel.blue),
                     indicator_pattern_name(indicators.panel.yellow),
                     indicator_pattern_name(indicators.panel.red));
        }
        control_runtime_status_t control = {0};
        if (control_runtime_get_status(&control)) {
            ESP_LOGI(TAG,
                     "lazo de presión: %.2fbar -> %.1f%% (densidad %u%%, %s); "
                     "lazo térmico: %.2fC -> %.1f%% (%s); ciclos=%" PRIu32
                     "; plazos incumplidos=%" PRIu32,
                     (double)control.pressure_measurement,
                     (double)control.pressure_output,
                     (unsigned)control.pressure_density,
                     actuation_verdict_name(control.pressure_verdict),
                     (double)control.temperature_measurement,
                     (double)control.temperature_output,
                     actuation_verdict_name(control.temperature_verdict),
                     control.cycles, control.deadline_misses);
        }
        uint32_t log_pending = 0U;
        uint32_t log_accepted = 0U;
        uint32_t log_lost = 0U;
        if (control_log_runtime_statistics(&log_pending, &log_accepted, &log_lost)) {
            ESP_LOGI(TAG,
                     "registro del lazo: admitidos=%" PRIu32 "; en cola=%" PRIu32
                     "/%" PRIu32 "; perdidos=%" PRIu32,
                     log_accepted, log_pending, control_log_runtime_capacity(),
                     log_lost);
        }
        simulation_snapshot_t simulation = {0};
        const bool has_simulation =
#if CONFIG_CAFE_SIMULATION
            simulation_runtime_get_snapshot(&simulation);
#else
            false;
#endif
        if (has_simulation) {
            ESP_LOGI(TAG,
                     "heartbeat; heap=%" PRIu32
                     " bytes; GPIO potencia=OFF; simulación T=%.2fC P=%.2fbar",
                     esp_get_free_heap_size(), simulation.temperature_c,
                     simulation.pressure_bar);
        } else {
            /* El binario de operación no puede energizar nada mientras
             * `commissioned` sea falso; el de identificación sí, y entonces la
             * afirmación tiene que salir del veredicto y no de un literal. */
            const bool energizable =
                supervisor_runtime_get_status(&vigilance) &&
                (actuation_verdict_allows(vigilance.actuation.pump) ||
                 actuation_verdict_allows(vigilance.actuation.heater));
            ESP_LOGI(TAG,
                     "heartbeat; heap libre=%" PRIu32
                     " bytes; salidas de potencia=%s",
                     esp_get_free_heap_size(), energizable ? "AUTORIZADAS" : "OFF");
        }
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}
