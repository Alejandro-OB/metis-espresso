#include "external_hil_runtime.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "acquisition_runtime.h"
#include "calibration_command.h"
#include "config_command.h"
#include "control_runtime.h"
#include "identification_tuning.h"
#include "device_config.h"
#include "hx711_runtime.h"
#include "sensor_calibration_store.h"
#include "operational_runtime.h"
#include "simulation_runtime.h"
#include "supervisor_runtime.h"
#include "sync_runtime.h"

/* Holgado a propósito: la orden de calibración lleva la línea, sus campos
 * partidos, dos registros y un `printf` de ocho flotantes en la misma pila, y
 * un desbordamiento aquí se ve como un reinicio sin explicación. */
#define HIL_STACK_BYTES 6144U
#define HIL_PRIORITY (tskIDLE_PRIORITY + 5U)
/* Cabe una orden `K,set` completa, que con identificador largo y ocho números
 * a nueve cifras ronda los 190 bytes. Una línea más larga que esto se partiría
 * en dos, y la primera mitad se rechaza por campos de menos. */
#define HIL_LINE_BYTES 256U

static const char *TAG = "external_hil";
static TaskHandle_t s_task;

/*
 * Trama de entrada: I,<time_ms>,<temperature_c>,<pressure_bar>,<mode>\n
 * mode: 0 preparado, 1 extracción, 3 purga.
 * El 2 (vapor) y el 4 (enfriamiento tras el vapor) se retiraron con el vapor
 * (ADR-006, revisión del 2026-10-06) y se rechazan con `E,mode`. Los números
 * que quedan no se reasignan, para que un registro antiguo del puente siga
 * diciendo lo mismo.
 * Trama de salida: O,<time_ms>,<heater_duty>,<pump_duty>\n
 * heater_duty y pump_duty son demandas virtuales [0,1].
 */
/*
 * Atiende una orden de configuración del enlace.
 *
 * La vía existe porque `device_config` sabía guardar la dirección y la
 * credencial desde el principio y nadie podía pedírselo: cambiar de plataforma
 * exigía recompilar y regrabar el firmware que gobierna la máquina, que es lo
 * que ADR-012 § 4.4 decide evitar.
 *
 * Va por el puerto físico y no por la red a propósito. Desviar a dónde envía el
 * dispositivo su evidencia es una forma de perderla, y el diseño mantiene fuera de
 * la red toda autoridad sobre el dispositivo. Tampoco inicia ninguna
 * preparación ni toca las salidas de potencia.
 *
 * Devuelve false cuando la línea no era una orden de configuración, para que el
 * lector siga probando con los demás formatos del puerto.
 */
static bool handle_config_command(const char *line)
{
    const config_command_t command = config_command_parse(line);
    if (command.kind == CONFIG_COMMAND_NONE) {
        return false;
    }

    esp_err_t result = ESP_OK;
    switch (command.kind) {
    case CONFIG_COMMAND_SHOW: {
        device_config_t current;
        device_config_load(&current);
        /* La credencial no se devuelve, ni siquiera recortada: lo que hace
         * falta saber es de dónde procede, no cuál es. */
        /* El manifiesto del firmware va al final para no mover los campos que
         * ya leía quien consultara esta orden; «-» es «ninguno». */
        printf("C,show,%s,%s,%s,%s,%s\n", current.sync_url,
               device_config_origin_name(current.url_origin),
               device_config_origin_name(current.token_origin),
               current.update_url[0] != '\0' ? current.update_url : "-",
               device_config_origin_name(current.update_url_origin));
        fflush(stdout);
        return true;
    }
    case CONFIG_COMMAND_SET_URL:
        result = device_config_set_sync_url(command.value);
        break;
    case CONFIG_COMMAND_SET_TOKEN:
        result = device_config_set_sync_token(command.value);
        break;
    case CONFIG_COMMAND_RESET:
        result = device_config_reset();
        break;
    case CONFIG_COMMAND_SET_UPDATE_URL:
        result = device_config_set_update_url(command.value);
        break;
    default:
        puts("C,error");
        fflush(stdout);
        return true;
    }

    if (result != ESP_OK) {
        printf("C,error,%s\n", esp_err_to_name(result));
        fflush(stdout);
        return true;
    }

    /* El cambio rige desde ya. Sin esto solo valdría tras reiniciar, y reiniciar
     * para cambiar de plataforma vacía la retención en memoria de los registros
     * que todavía esperan hora. */
    sync_runtime_reload_config();
    /* No se hace eco del valor fijado: la credencial viajaría a cualquier
     * captura de la consola. */
    puts(command.kind == CONFIG_COMMAND_SET_TOKEN        ? "C,token,ok"
         : command.kind == CONFIG_COMMAND_SET_URL        ? "C,url,ok"
         : command.kind == CONFIG_COMMAND_SET_UPDATE_URL ? "C,ota,ok"
                                                         : "C,reset,ok");
    fflush(stdout);
    return true;
}

/* Escribe un registro de calibración con su etiqueta, o `none` si no lo hay. */
static void print_calibration(const char *label, const sensor_calibration_t *calibration)
{
    char fields[HIL_LINE_BYTES];
    if (calibration == NULL || calibration_command_format(calibration, fields,
                                                          sizeof(fields)) < 0) {
        printf("K,%s,none\n", label);
        return;
    }
    printf("K,%s,%s\n", label, fields);
}

/*
 * Atiende una orden de calibración (ADR-020 § 4.1).
 *
 * Lo guardado **no se aplica**: rige desde el siguiente arranque, para que una
 * serie en curso nunca mezcle dos calibraciones. Por eso `show` distingue la
 * que rige (`active`) de la guardada (`stored`), que pueden no coincidir hasta
 * reiniciar.
 *
 * Tras guardar se relee de la memoria y se escribe lo leído, no lo recibido: es
 * lo que el programa del computador compara con lo que envió.
 *
 * Devuelve false cuando la línea no era una orden de calibración.
 */
static bool handle_calibration_command(const char *line)
{
    const calibration_command_t command = calibration_command_parse(line);
    if (command.kind == CALIBRATION_COMMAND_NONE) {
        return false;
    }

    switch (command.kind) {
    case CALIBRATION_COMMAND_SHOW: {
        sensor_calibration_t active = {0};
        sensor_calibration_t stored = {0};
        print_calibration("active",
                          acquisition_runtime_get_calibration(&active) ? &active : NULL);
        print_calibration("stored",
                          sensor_calibration_read_stored(&stored) == ESP_OK ? &stored : NULL);
        break;
    }
    case CALIBRATION_COMMAND_SET: {
        const esp_err_t result = sensor_calibration_store(&command.calibration);
        sensor_calibration_t stored = {0};
        if (result != ESP_OK || sensor_calibration_read_stored(&stored) != ESP_OK) {
            printf("K,error,%s\n", esp_err_to_name(result != ESP_OK ? result
                                                                     : ESP_ERR_INVALID_STATE));
            break;
        }
        ESP_LOGW(TAG, "Calibración %s v%u guardada; rige desde el siguiente arranque",
                 stored.id, (unsigned)stored.version);
        print_calibration("stored", &stored);
        puts("K,set,ok,next-boot");
        break;
    }
    case CALIBRATION_COMMAND_RESET: {
        const esp_err_t result = sensor_calibration_clear();
        if (result != ESP_OK) {
            printf("K,error,%s\n", esp_err_to_name(result));
            break;
        }
        ESP_LOGW(TAG, "Calibración guardada borrada; rige la nominal desde el siguiente "
                      "arranque");
        puts("K,reset,ok,next-boot");
        break;
    }
    case CALIBRATION_COMMAND_TARE:
        /* La tara no es calibración: se toma en cada sesión con la báscula vacía
         * y no se guarda, de modo que esta sí rige al momento. */
        puts(hx711_runtime_tare() == ESP_OK ? "K,tare,ok" : "K,error,not-settled");
        break;
    case CALIBRATION_COMMAND_INVALID:
    case CALIBRATION_COMMAND_NONE:
    default:
        printf("K,error,%s\n", calibration_reject_name(command.reject));
        break;
    }
    fflush(stdout);
    return true;
}

/*
 * Atiende una orden de ajuste en caliente (ADR-020 § 4.2).
 *
 * La respuesta lleva el instante, la sintonía que queda, sus ganancias y el
 * veredicto del lazo que se sintoniza, calentador o bomba: es el registro que ADR-020 exige de cada cambio,
 * y el computador que registra el puerto lo guarda junto a la serie. La
 * columna `tuning` de cada línea `T` repite el identificador.
 *
 * Devuelve false cuando la línea no era una orden de ajuste.
 */
static bool handle_tuning_command(const char *line)
{
    const identification_tuning_command_t command = identification_tuning_parse(line);
    if (command.kind == IDENTIFICATION_TUNING_COMMAND_NONE) {
        return false;
    }
    control_runtime_tuning_report_t report;
    const identification_tuning_result_t result =
        command.kind == IDENTIFICATION_TUNING_COMMAND_INVALID
            ? IDENTIFICATION_TUNING_REJECTED_COMMAND
            : control_runtime_tuning_command(&command, &report);
    if (command.kind == IDENTIFICATION_TUNING_COMMAND_INVALID) {
        report = (control_runtime_tuning_report_t){0};
    }
    /* λ con centésimas: la de presión se mueve en décimas de segundo. */
    printf("A,%s,%" PRIu64 ",%s,%.2f,%u,%.4f,%.6f,%.1f,%s\n",
           identification_tuning_result_name(result),
           (uint64_t)(esp_timer_get_time() / 1000), report.identifier,
           (double)report.lambda_s, report.integral_enabled ? 1U : 0U,
           (double)report.proportional_gain, (double)report.integral_gain,
           (double)report.ceiling_percent, actuation_verdict_name(report.verdict));
    if (result == IDENTIFICATION_TUNING_ACCEPTED &&
        command.kind != IDENTIFICATION_TUNING_COMMAND_SHOW) {
        ESP_LOGW(TAG, "sintonía en caliente: %s (λ=%.2f s, integral=%s)", report.identifier,
                 (double)report.lambda_s, report.integral_enabled ? "sí" : "no");
    }
    fflush(stdout);
    return true;
}

/*
 * Atiende una orden de la escalera de la bomba (enmienda a ADR-007 § 5 del
 * 2026-10-10). La respuesta dice el resultado, el escalón vigente y el instante,
 * y queda en el registro junto a la serie.
 *
 * Devuelve false cuando la línea no era una orden de la escalera.
 */
static bool handle_ladder_command(const char *line)
{
    const pump_ladder_command_t command = pump_ladder_parse(line);
    if (command.kind == PUMP_LADDER_COMMAND_NONE) {
        return false;
    }
    uint8_t current = 0U;
    const pump_ladder_result_t result =
        command.kind == PUMP_LADDER_COMMAND_INVALID
            ? PUMP_LADDER_REJECTED_NOT_IN_LADDER
            : supervisor_runtime_ladder_command(&command, &current);
    printf("B,%s,%" PRIu64 ",%u\n", pump_ladder_result_name(result),
           (uint64_t)(esp_timer_get_time() / 1000), (unsigned)current);
    fflush(stdout);
    return true;
}

/*
 * Atiende una orden del ensayo hidráulico E2 (enmienda a ADR-007 § 5 del
 * 2026-10-16). La respuesta dice el resultado, el instante, la orden elegida
 * para la próxima extracción, la de la extracción en curso y las declaradas,
 * y queda en el registro junto a la serie.
 *
 * Devuelve false cuando la línea no era una orden del ensayo.
 */
static bool handle_hydraulic_command(const char *line)
{
    const hydraulic_test_command_t command = hydraulic_test_parse(line);
    if (command.kind == HYDRAULIC_TEST_COMMAND_NONE) {
        return false;
    }
    control_runtime_hydraulic_report_t report;
    const hydraulic_test_result_t result = control_runtime_hydraulic_command(&command, &report);
    printf("H,%s,%" PRIu64 ",%u,%u,%u/%u/%u\n", hydraulic_test_result_name(result),
           (uint64_t)(esp_timer_get_time() / 1000), (unsigned)report.selected,
           (unsigned)report.applied, (unsigned)report.orders[0], (unsigned)report.orders[1],
           (unsigned)report.orders[2]);
    fflush(stdout);
    return true;
}

static void external_hil_task(void *argument)
{
    (void)argument;
    char line[HIL_LINE_BYTES];
    ESP_LOGI(TAG, "HIL activo; núcleo=%d; protocolo I/O serie; GPIO de potencia aislados",
             (int)xPortGetCoreID());
    while (true) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }
        /*
         * `V,<evento>` inyecta un evento en la máquina de estados desde el banco
         * de pruebas, por el puerto USB y con las salidas de potencia aisladas.
         * Sustituye al mando físico mientras no exista (F-04) y permite
         * ejercitar el ciclo completo de registro. No es una ruta de red: el diseño
         * sigue impidiendo que la API, un temporizador o un servicio externo
         * inicien una preparación.
         */
        if (handle_config_command(line) || handle_calibration_command(line) ||
            handle_tuning_command(line) || handle_ladder_command(line) ||
            handle_hydraulic_command(line)) {
            continue;
        }

        unsigned int injected_event = 0U;
        if (sscanf(line, "V,%u", &injected_event) == 1) {
            if (injected_event >= (unsigned int)OP_EVENT_COUNT) {
                puts("E,event");
            } else {
                const bool posted = operational_runtime_post_event(
                    (operational_event_t)injected_event, pdMS_TO_TICKS(100U));
                printf("V,%u,%s\n", injected_event, posted ? "ok" : "busy");
            }
            fflush(stdout);
            continue;
        }

        unsigned long time_ms = 0U;
        float temperature_c = 0.0F;
        float pressure_bar = 0.0F;
        unsigned int mode = 0U;
        if (sscanf(line, "I,%lu,%f,%f,%u", &time_ms, &temperature_c,
                   &pressure_bar, &mode) != 4) {
            puts("E,format");
            fflush(stdout);
            continue;
        }
        if (mode != 0U && mode != 1U && mode != 3U) {
            puts("E,mode");
            fflush(stdout);
            continue;
        }
        const float target_c = 93.0F;
        const bool heating_mode = mode == 0U || mode == 1U;
        const bool extraction_mode = mode == 1U;
        const bool purge_mode = mode == 3U;
        const float heater_duty = heating_mode && temperature_c < target_c - 0.5F ? 1.0F : 0.0F;
        const float pump_duty = extraction_mode && pressure_bar < 9.0F ? 0.75F :
                                (purge_mode ? 0.35F : 0.0F);
        simulation_snapshot_t snapshot = {
            .elapsed_ms = time_ms,
            .temperature_c = temperature_c,
            .pressure_bar = pressure_bar,
            .temperature_valid = true,
            .pressure_valid = true,
            .zero_cross_available = true,
            .simulated_heater_on = heater_duty > 0.0F,
            .simulated_pump_on = pump_duty > 0.0F,
            .simulated_pump_duty = pump_duty,
        };
        simulation_runtime_set_external_snapshot(&snapshot);
        printf("O,%lu,%.3f,%.3f\n", time_ms, heater_duty, pump_duty);
        fflush(stdout);
    }
}

esp_err_t external_hil_runtime_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xTaskCreatePinnedToCore(external_hil_task, "external_hil", HIL_STACK_BYTES,
                       NULL, HIL_PRIORITY, &s_task, 0) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
