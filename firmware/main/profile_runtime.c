#include "profile_runtime.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "first_use_offset.h"
#include "first_use_offset_runtime.h"
#include "nvs.h"
#include "process_supervisor.h"
#include "water_reference_runtime.h"

/*
 * La configuración efectiva sobrevive a un reinicio: sin ella el dispositivo
 * volvería siempre al perfil de arranque y perdería la referencia con la que
 * se ejecutó la última preparación.
 */
#define PROFILE_STORAGE_NAMESPACE "eko_profile"
#define PROFILE_STORAGE_KEY "effective"
#define PROFILE_STORAGE_FORMAT UINT32_C(4)
#define PROFILE_STORAGE_FORMAT_V3 UINT32_C(3)
#define PROFILE_STORAGE_FORMAT_V2 UINT32_C(2)
#define PROFILE_STORAGE_FORMAT_V1 UINT32_C(1)

typedef struct {
    uint32_t format;
    extraction_profile_t profile;
} stored_profile_t;

/*
 * Formato 1: el perfil antes de que el contrato admitiera el corte del
 * purgado. Se conserva su forma para migrarlo: descartarlo dejaría la máquina
 * con el perfil de arranque (30 s de tope) hasta que la plataforma reenviara
 * uno, y una extracción normal pasa de ese tope.
 */
typedef struct {
    char id[EXTRACTION_PROFILE_ID_LENGTH];
    char name[EXTRACTION_PROFILE_NAME_LENGTH];
    uint32_t version;
    float temperature_setpoint_c;
    float temperature_tolerance_c;
    float pressure_setpoint_bar;
    float pressure_tolerance_bar;
    uint32_t maximum_time_ms;
    bool expected_beverage_mass_defined;
    float expected_beverage_mass_g;
} extraction_profile_v1_t;

typedef struct {
    uint32_t format;
    extraction_profile_v1_t profile;
} stored_profile_v1_t;

/*
 * Formato 2: el perfil antes del desplazamiento de caldera del primer uso
 * (ADR-009, enmienda del 2026-10-19). Igual que con el 1, se migra en vez de
 * descartarlo.
 */
typedef struct {
    char id[EXTRACTION_PROFILE_ID_LENGTH];
    char name[EXTRACTION_PROFILE_NAME_LENGTH];
    uint32_t version;
    float temperature_setpoint_c;
    float temperature_tolerance_c;
    float pressure_setpoint_bar;
    float pressure_tolerance_bar;
    uint32_t maximum_time_ms;
    bool expected_beverage_mass_defined;
    float expected_beverage_mass_g;
    bool purge_maximum_defined;
    uint32_t purge_maximum_ms;
} extraction_profile_v2_t;

typedef struct {
    uint32_t format;
    extraction_profile_v2_t profile;
} stored_profile_v2_t;

/*
 * Formato 3: el perfil antes del refuerzo de la prealimentación (ADR-009,
 * enmienda del 2026-10-27). Se migra como los anteriores.
 */
typedef struct {
    char id[EXTRACTION_PROFILE_ID_LENGTH];
    char name[EXTRACTION_PROFILE_NAME_LENGTH];
    uint32_t version;
    float temperature_setpoint_c;
    float temperature_tolerance_c;
    float pressure_setpoint_bar;
    float pressure_tolerance_bar;
    uint32_t maximum_time_ms;
    bool expected_beverage_mass_defined;
    float expected_beverage_mass_g;
    bool purge_maximum_defined;
    uint32_t purge_maximum_ms;
    float boiler_offset_c;
} extraction_profile_v3_t;

typedef struct {
    uint32_t format;
    extraction_profile_v3_t profile;
} stored_profile_v3_t;

/* El perfil migrado no pide refuerzo ni desplazamiento: ninguno se guardaba. */
static extraction_profile_t profile_from_v3(const extraction_profile_v3_t *old)
{
    extraction_profile_t profile = {0};
    memcpy(profile.id, old->id, sizeof(profile.id));
    memcpy(profile.name, old->name, sizeof(profile.name));
    profile.version = old->version;
    profile.temperature_setpoint_c = old->temperature_setpoint_c;
    profile.temperature_tolerance_c = old->temperature_tolerance_c;
    profile.pressure_setpoint_bar = old->pressure_setpoint_bar;
    profile.pressure_tolerance_bar = old->pressure_tolerance_bar;
    profile.maximum_time_ms = old->maximum_time_ms;
    profile.expected_beverage_mass_defined = old->expected_beverage_mass_defined;
    profile.expected_beverage_mass_g = old->expected_beverage_mass_g;
    profile.purge_maximum_defined = old->purge_maximum_defined;
    profile.purge_maximum_ms = old->purge_maximum_ms;
    return profile;
}

/* El perfil migrado no pide desplazamiento, que es lo que pedía al guardarse. */
static extraction_profile_t profile_from_v2(const extraction_profile_v2_t *old)
{
    extraction_profile_t profile = {0};
    memcpy(profile.id, old->id, sizeof(profile.id));
    memcpy(profile.name, old->name, sizeof(profile.name));
    profile.version = old->version;
    profile.temperature_setpoint_c = old->temperature_setpoint_c;
    profile.temperature_tolerance_c = old->temperature_tolerance_c;
    profile.pressure_setpoint_bar = old->pressure_setpoint_bar;
    profile.pressure_tolerance_bar = old->pressure_tolerance_bar;
    profile.maximum_time_ms = old->maximum_time_ms;
    profile.expected_beverage_mass_defined = old->expected_beverage_mass_defined;
    profile.expected_beverage_mass_g = old->expected_beverage_mass_g;
    profile.purge_maximum_defined = old->purge_maximum_defined;
    profile.purge_maximum_ms = old->purge_maximum_ms;
    return profile;
}

/* El perfil migrado no declara purgado: rige el valor compilado, que es lo
 * que regía cuando se guardó. */
static extraction_profile_t profile_from_v1(const extraction_profile_v1_t *old)
{
    extraction_profile_t profile = {0};
    memcpy(profile.id, old->id, sizeof(profile.id));
    memcpy(profile.name, old->name, sizeof(profile.name));
    profile.version = old->version;
    profile.temperature_setpoint_c = old->temperature_setpoint_c;
    profile.temperature_tolerance_c = old->temperature_tolerance_c;
    profile.pressure_setpoint_bar = old->pressure_setpoint_bar;
    profile.pressure_tolerance_bar = old->pressure_tolerance_bar;
    profile.maximum_time_ms = old->maximum_time_ms;
    profile.expected_beverage_mass_defined = old->expected_beverage_mass_defined;
    profile.expected_beverage_mass_g = old->expected_beverage_mass_g;
    return profile;
}

static const char *TAG = "profile_runtime";

static portMUX_TYPE s_profile_lock = portMUX_INITIALIZER_UNLOCKED;
static extraction_profile_t s_effective_profile;
static bool s_initialized;
static bool s_persisted;

static esp_err_t persist_profile(const extraction_profile_t *profile)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(PROFILE_STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    /*
     * El desplazamiento del primer uso no se guarda. Se pidió para la próxima
     * preparación de esta sesión, con la máquina en un estado que un reinicio
     * ya no conserva; recuperarlo al arrancar lo aplicaría a una situación que
     * nadie predijo. Tras el reinicio la plataforma puede volver a pedirlo: la
     * sincronización lo verá como una petición nueva. El refuerzo, igual.
     */
    stored_profile_t stored = {.format = PROFILE_STORAGE_FORMAT, .profile = *profile};
    stored.profile.boiler_offset_c = 0.0F;
    stored.profile.first_use_feedforward_gain = 0.0F;
    result = nvs_set_blob(handle, PROFILE_STORAGE_KEY, &stored, sizeof(stored));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

/*
 * Los límites del perfil y, además, que el agua pedida caiga dentro de lo que
 * se midió en esta máquina (ADR-009, enmienda del 2026-10-18). Se comprueba al
 * activar y no solo al aplicarlo al supervisor, para que la plataforma reciba
 * el rechazo con su causa en vez de ver adoptado un perfil que la placa no usa.
 */
static extraction_profile_validation_t validate_for_machine(
    const extraction_profile_t *profile)
{
    const extraction_profile_limits_t limits = extraction_simulation_limits();
    const extraction_profile_validation_t result =
        extraction_profile_validate(profile, &limits);
    if (result != PROFILE_VALID) {
        return result;
    }
    const water_reference_t water = water_reference_commissioned();
    float boiler_c = 0.0F;
    if (!water_reference_boiler_setpoint(&water, profile->temperature_setpoint_c,
                                         &boiler_c)) {
        return PROFILE_TEMPERATURE_OUT_OF_RANGE;
    }
    /* El desplazamiento del primer uso se suma después de convertir: no lo
     * acota el intervalo de la recta, sino el máximo medido y el límite duro
     * (ADR-009, enmienda del 2026-10-19). Se juzga con los límites nominales
     * de la máquina; el supervisor lo vuelve a juzgar con los vigentes. */
    first_use_offset_source_t source = FIRST_USE_OFFSET_SOURCE_NONE;
    const process_limits_t machine = process_limits_default();
    const first_use_offset_bounds_t bounds = {
        .maximum_c = first_use_offset_maximum_c(&source),
        .hard_limit_c = machine.hard_overtemperature_c,
        .margin_c = machine.ready_tolerance_c,
    };
    const first_use_offset_check_t offset =
        first_use_offset_check(profile->boiler_offset_c, &bounds, boiler_c);
    if (!first_use_offset_check_allows(offset)) {
        ESP_LOGW(TAG,
                 "Desplazamiento de caldera de %.2f C rechazado (%s): máximo %.2f C "
                 "(%s), caldera %.2f C, límite duro %.1f C",
                 (double)profile->boiler_offset_c, first_use_offset_check_name(offset),
                 (double)bounds.maximum_c, first_use_offset_source_name(source),
                 (double)boiler_c, (double)bounds.hard_limit_c);
        return PROFILE_BOILER_OFFSET_OUT_OF_RANGE;
    }
    /* El refuerzo de la prealimentación (enmienda del 2026-10-27), contra la
     * ganancia que rige y el máximo del acta. Tampoco se recorta. */
    const first_use_offset_bounds_t gains = {
        .feedforward_base_gain = heater_feedforward_configured_gain(NULL),
        .feedforward_maximum_gain = first_use_feedforward_maximum_gain(),
    };
    const first_use_feedforward_check_t gain =
        first_use_feedforward_check(profile->first_use_feedforward_gain, &gains);
    if (!first_use_feedforward_check_allows(gain)) {
        ESP_LOGW(TAG,
                 "Refuerzo de la prealimentación %.3f rechazado (%s): ganancia del acta %.3f, "
                 "máximo %.3f",
                 (double)profile->first_use_feedforward_gain,
                 first_use_feedforward_check_name(gain), (double)gains.feedforward_base_gain,
                 (double)gains.feedforward_maximum_gain);
        return PROFILE_FIRST_USE_FEEDFORWARD_OUT_OF_RANGE;
    }
    return PROFILE_VALID;
}

/*
 * Recupera la configuración persistida y la valida antes de adoptarla: un
 * registro corrupto o de un formato anterior no puede gobernar la máquina.
 */
static bool load_persisted_profile(extraction_profile_t *profile)
{
    nvs_handle_t handle;
    if (nvs_open(PROFILE_STORAGE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    /* La unión deja leer cualquiera de los dos formatos en el mismo búfer; el
     * formato y la longitud deciden cuál es, y ninguna otra combinación vale. */
    union {
        uint32_t format;
        stored_profile_t current;
        stored_profile_v3_t v3;
        stored_profile_v2_t v2;
        stored_profile_v1_t v1;
    } stored;
    memset(&stored, 0, sizeof(stored));
    size_t length = sizeof(stored);
    const esp_err_t result = nvs_get_blob(handle, PROFILE_STORAGE_KEY, &stored, &length);
    nvs_close(handle);
    if (result != ESP_OK) {
        return false;
    }
    extraction_profile_t candidate;
    if (length == sizeof(stored.current) && stored.format == PROFILE_STORAGE_FORMAT) {
        candidate = stored.current.profile;
        /* Ya se guarda en cero; se vuelve a poner por si un registro lo
         * trajera: un desplazamiento no sobrevive a un reinicio. */
        candidate.boiler_offset_c = 0.0F;
        candidate.first_use_feedforward_gain = 0.0F;
    } else if (length == sizeof(stored.v3) && stored.format == PROFILE_STORAGE_FORMAT_V3) {
        candidate = profile_from_v3(&stored.v3.profile);
        ESP_LOGI(TAG, "Configuración persistida en formato 3; migrada sin refuerzo");
    } else if (length == sizeof(stored.v2) && stored.format == PROFILE_STORAGE_FORMAT_V2) {
        candidate = profile_from_v2(&stored.v2.profile);
        ESP_LOGI(TAG, "Configuración persistida en formato 2; migrada sin desplazamiento");
    } else if (length == sizeof(stored.v1) && stored.format == PROFILE_STORAGE_FORMAT_V1) {
        candidate = profile_from_v1(&stored.v1.profile);
        ESP_LOGI(TAG, "Configuración persistida en formato 1; migrada sin purgado declarado");
    } else {
        return false;
    }
    if (validate_for_machine(&candidate) != PROFILE_VALID) {
        ESP_LOGW(TAG, "Configuración persistida descartada por no superar los límites");
        return false;
    }
    *profile = candidate;
    return true;
}

esp_err_t profile_runtime_start(void)
{
    portENTER_CRITICAL(&s_profile_lock);
    if (s_initialized) {
        portEXIT_CRITICAL(&s_profile_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_effective_profile = extraction_simulation_default_profile();
    s_initialized = true;
    portEXIT_CRITICAL(&s_profile_lock);

    extraction_profile_t persisted = {0};
    if (load_persisted_profile(&persisted)) {
        portENTER_CRITICAL(&s_profile_lock);
        s_effective_profile = persisted;
        s_persisted = true;
        portEXIT_CRITICAL(&s_profile_lock);
        ESP_LOGI(TAG, "Configuración efectiva recuperada: %s v%" PRIu32,
                 persisted.id, persisted.version);
    } else {
        ESP_LOGI(TAG, "Sin configuración persistida válida; se usa el perfil de arranque");
    }
    return ESP_OK;
}

bool profile_runtime_is_persisted(void)
{
    portENTER_CRITICAL(&s_profile_lock);
    const bool persisted = s_persisted;
    portEXIT_CRITICAL(&s_profile_lock);
    return persisted;
}

bool profile_runtime_get_effective(extraction_profile_t *profile)
{
    if (profile == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_profile_lock);
    const bool initialized = s_initialized;
    if (initialized) {
        *profile = s_effective_profile;
    }
    portEXIT_CRITICAL(&s_profile_lock);
    return initialized;
}

esp_err_t profile_runtime_activate(const extraction_profile_t *profile,
                                   extraction_profile_validation_t *validation)
{
    const extraction_profile_validation_t result =
        profile == NULL ? PROFILE_INVALID_ARGUMENT : validate_for_machine(profile);
    if (validation != NULL) {
        *validation = result;
    }
    if (result != PROFILE_VALID) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(&s_profile_lock);
    if (!s_initialized) {
        portEXIT_CRITICAL(&s_profile_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_effective_profile = *profile;
    portEXIT_CRITICAL(&s_profile_lock);

    /* Se persiste después de adoptarla: un fallo de escritura no debe impedir
     * que la configuración validada gobierne la preparación en curso. */
    const esp_err_t stored = persist_profile(profile);
    portENTER_CRITICAL(&s_profile_lock);
    s_persisted = stored == ESP_OK;
    portEXIT_CRITICAL(&s_profile_lock);
    if (stored != ESP_OK) {
        ESP_LOGE(TAG, "Configuración activada pero no persistida: %s",
                 esp_err_to_name(stored));
    }
    return ESP_OK;
}

