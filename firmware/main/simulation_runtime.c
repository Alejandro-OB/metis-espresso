#include "simulation_runtime.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "operational_runtime.h"
#include "profile_runtime.h"
#include "supervisor_runtime.h"
#include "water_reference_runtime.h"

#define SIMULATION_PERIOD_MS 100U
#define SIMULATION_LOG_PERIOD_MS 1000U
#define SIMULATION_COMMAND_QUEUE_LENGTH 8U
#define SIMULATION_TASK_STACK_BYTES 4096U
#define SIMULATION_TASK_PRIORITY (tskIDLE_PRIORITY + 6U)

static const char *TAG = "simulation_task";

static QueueHandle_t s_fault_queue;
static TaskHandle_t s_task_handle;
static simulation_model_t s_model;
static simulation_snapshot_t s_published_snapshot;
static simulation_snapshot_t s_external_snapshot;
static bool s_external_active;
static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;

static void publish_snapshot(const simulation_snapshot_t *snapshot)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    s_published_snapshot = *snapshot;
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void simulation_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t log_elapsed_ms = 0U;
    uint32_t applied_profile_version = 0U;
    char applied_profile_id[EXTRACTION_PROFILE_ID_LENGTH] = {0};
    float applied_offset_c = 0.0F;

    /* La planta publica su estado y nada más: los eventos de la máquina los
     * decide el supervisor, con la medida real o con esta, indistintamente. */
    ESP_LOGI(TAG, "Simulación iniciada; núcleo=%d; ninguna salida física será accionada",
             (int)xPortGetCoreID());
    while (true) {
        uint32_t requested_faults = 0U;
        while (xQueueReceive(s_fault_queue, &requested_faults, 0U) == pdTRUE) {
            simulation_model_set_faults(&s_model, requested_faults);
            ESP_LOGW(TAG, "Fallas inyectadas=0x%08" PRIx32,
                     requested_faults);
        }

        const operational_state_t state = operational_runtime_state();
        /* El desplazamiento del primer uso lo decide el supervisor y lo
         * retira al terminar la preparación (ADR-009, enmienda del
         * 2026-10-19). La planta simulada regula su propia caldera a la
         * referencia que recibe: sin él, el supervisor esperaría una
         * temperatura que la simulación no busca y nunca declararía «lista». */
        supervisor_status_t supervisor = {0};
        const float offset_c = supervisor_runtime_get_status(&supervisor)
                                   ? supervisor.first_use_offset_c
                                   : 0.0F;
        extraction_profile_t profile = {0};
        if (profile_runtime_get_effective(&profile) &&
            (profile.version != applied_profile_version ||
             strcmp(profile.id, applied_profile_id) != 0 ||
             offset_c != applied_offset_c)) {
            /* La planta simulada es la caldera: recibe la misma referencia
             * convertida que el lazo real (ADR-009, enmienda del 2026-10-18). */
            const water_reference_t water = water_reference_commissioned();
            float boiler_setpoint_c = 0.0F;
            if (water_reference_boiler_setpoint(&water, profile.temperature_setpoint_c,
                                                &boiler_setpoint_c) &&
                simulation_model_apply_extraction_profile(
                    &s_model, boiler_setpoint_c + offset_c,
                    profile.pressure_setpoint_bar)) {
                applied_profile_version = profile.version;
                snprintf(applied_profile_id, sizeof(applied_profile_id), "%s",
                         profile.id);
                applied_offset_c = offset_c;
                ESP_LOGI(TAG, "Perfil simulado aplicado=%s v%" PRIu32 "; primer uso +%.2f C",
                         profile.id, profile.version, (double)offset_c);
            } else {
                ESP_LOGE(TAG, "Perfil efectivo incompatible con la simulación");
            }
        }
        simulation_model_step(&s_model, state, SIMULATION_PERIOD_MS);
        const simulation_snapshot_t *snapshot =
            simulation_model_snapshot(&s_model);
        publish_snapshot(snapshot);

        log_elapsed_ms += SIMULATION_PERIOD_MS;
        if (log_elapsed_ms >= SIMULATION_LOG_PERIOD_MS) {
            log_elapsed_ms = 0U;
            ESP_LOGI(TAG,
                     "estado=%s T=%.2fC P=%.2fbar heater_sim=%s pump_sim=%.0f%%",
                     operational_state_name(state),
                     snapshot->temperature_c, snapshot->pressure_bar,
                     snapshot->simulated_heater_on ? "ON" : "OFF",
                     (double)(snapshot->simulated_pump_duty * 100.0F));
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SIMULATION_PERIOD_MS));
    }
}

esp_err_t simulation_runtime_start(void)
{
    if (s_task_handle != NULL || s_fault_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const simulation_config_t config = simulation_default_config();
    if (!simulation_model_init(&s_model, &config)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_published_snapshot = *simulation_model_snapshot(&s_model);

    s_fault_queue = xQueueCreate(SIMULATION_COMMAND_QUEUE_LENGTH,
                                 sizeof(uint32_t));
    if (s_fault_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        simulation_task, "plant_sim", SIMULATION_TASK_STACK_BYTES,
        NULL, SIMULATION_TASK_PRIORITY, &s_task_handle, 0);
    if (created != pdPASS) {
        vQueueDelete(s_fault_queue);
        s_fault_queue = NULL;
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool simulation_runtime_set_faults(uint32_t fault_mask, TickType_t wait_ticks)
{
    if (s_fault_queue == NULL) {
        return false;
    }
    return xQueueSend(s_fault_queue, &fault_mask, wait_ticks) == pdTRUE;
}

bool simulation_runtime_get_snapshot(simulation_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_task_handle == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_snapshot_lock);
    *snapshot = s_external_active ? s_external_snapshot : s_published_snapshot;
    portEXIT_CRITICAL(&s_snapshot_lock);
    return true;
}

void simulation_runtime_set_external_snapshot(const simulation_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_snapshot_lock);
    s_external_snapshot = *snapshot;
    s_external_active = true;
    portEXIT_CRITICAL(&s_snapshot_lock);
}
