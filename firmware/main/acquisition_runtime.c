#include "acquisition_runtime.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ads1115.h"
#include "sensor_calibration_store.h"
#include "sensor_scaling.h"
#include "signal_filter.h"

#define ACQUISITION_TASK_STACK_BYTES 4096U
#define ACQUISITION_TASK_PRIORITY (tskIDLE_PRIORITY + 6U)
#define ACQUISITION_TASK_CORE 1     /* junto a la máquina de estados */
#define ACQUISITION_PERIOD_MS 10U   /* cadencia de presión, ADR-007 § 3.3 */

/*
 * La temperatura se lee una vez cada diez ciclos, es decir cada 100 ms.
 *
 * Las dos señales no piden la misma cadencia. La presión la fija el filtro
 * antialias del canal y el rizado de 60 Hz de la bomba: muestreada a 100 Hz, lo
 * que el filtro deja pasar se pliega sobre 40 Hz y queda como ruido rápido que
 * la mediana rechaza. La temperatura de la caldera evoluciona con constantes de
 * segundos, de modo que leerla cien veces por segundo no añadiría información y
 * sí ocuparía el convertidor, que atiende un canal cada vez.
 */
#define TEMPERATURE_DECIMATION 10U

static const char *TAG = "acquisition";

static TaskHandle_t s_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static acquisition_status_t s_status;
static sensor_calibration_t s_calibration;
static signal_limits_t s_temperature_limits;
static signal_limits_t s_pressure_limits;
static signal_filter_t s_temperature_filter;
static signal_filter_t s_pressure_filter;

static uint64_t monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / UINT64_C(1000);
}

/*
 * Convierte un canal, clasifica su calidad y filtra solo lo válido.
 *
 * El instante se toma tras la conversión de este canal concreto, no al abrir el
 * ciclo: el convertidor atiende un canal cada vez, de modo que temperatura y
 * presión se leen separadas por el tiempo de conversión y no comparten instante.
 */
static measurement_t read_channel(ads1115_channel_t channel)
{
    const bool is_temperature = channel == ADS1115_CHANNEL_TEMPERATURE;
    signal_filter_t *filter = is_temperature ? &s_temperature_filter : &s_pressure_filter;
    const signal_limits_t *limits = is_temperature ? &s_temperature_limits : &s_pressure_limits;
    measurement_t measurement = {
        .value = 0.0F,
        .raw_value = 0.0F,
        .volts = 0.0F,
        .counts = 0,
        .unit = is_temperature ? ACQUISITION_UNIT_TEMPERATURE : ACQUISITION_UNIT_PRESSURE,
        .calibration_id = s_calibration.id,
        .acquired_ms = 0U,
        .quality = SIGNAL_NOT_ACQUIRED,
    };
    int16_t counts = 0;
    const esp_err_t result = ads1115_read(channel, &counts);
    measurement.acquired_ms = monotonic_ms();
    if (result != ESP_OK) {
        signal_filter_reset(filter);
        portENTER_CRITICAL(&s_lock);
        s_status.read_failures++;
        portEXIT_CRITICAL(&s_lock);
        return measurement;
    }
    measurement.counts = counts;
    measurement.volts = sensor_volts_from_counts(counts);

    if (is_temperature) {
        const float resistance =
            sensor_ntc_resistance_ohms(measurement.volts, &s_calibration.temperature);
        float temperature = 0.0F;
        if (resistance > 0.0F &&
            sensor_ntc_temperature_c(resistance, &s_calibration.temperature, &temperature)) {
            measurement.raw_value = temperature;
        }
    } else {
        measurement.raw_value = sensor_pressure_bar(measurement.volts, &s_calibration.pressure);
    }
    measurement.quality =
        signal_quality_evaluate(measurement.volts, measurement.raw_value, 0U, true, limits);

    /* Solo se filtra lo válido: una lectura no apta interrumpe la ventana en
     * lugar de promediarse a través del hueco. */
    if (signal_quality_is_usable(measurement.quality)) {
        measurement.value = signal_filter_push(filter, measurement.raw_value);
    } else {
        signal_filter_reset(filter);
        measurement.value = measurement.raw_value;
    }
    return measurement;
}

static void acquisition_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    ESP_LOGI(TAG, "Adquisición iniciada; núcleo=%d; presión cada %u ms; temperatura cada %u ms",
             (int)xPortGetCoreID(), (unsigned)ACQUISITION_PERIOD_MS,
             (unsigned)(ACQUISITION_PERIOD_MS * TEMPERATURE_DECIMATION));
    (void)esp_task_wdt_add(NULL);
    uint32_t tick = 0U;
    while (true) {
        /* La temperatura se convierte primero para que, en el ciclo en que
         * ambas coinciden, la presión sea la más reciente de las dos. */
        const bool read_temperature = (tick % TEMPERATURE_DECIMATION) == 0U;
        measurement_t temperature = {0};
        if (read_temperature) {
            temperature = read_channel(ADS1115_CHANNEL_TEMPERATURE);
        }
        const measurement_t pressure = read_channel(ADS1115_CHANNEL_PRESSURE);
        portENTER_CRITICAL(&s_lock);
        /* En los ciclos sin conversión de temperatura se conserva la anterior
         * con su propio instante: envejece sola, y su antigüedad máxima decide
         * cuándo deja de servir. Sobrescribirla con una medida vacía la
         * declararía no adquirida diez veces de cada once. */
        if (read_temperature) {
            s_status.temperature = temperature;
        }
        s_status.pressure = pressure;
        s_status.converter_present = ads1115_is_present();
        s_status.cycles++;
        portEXIT_CRITICAL(&s_lock);
        /* Solo se alimenta cuando el ciclo completó conversión y publicación. */
        (void)esp_task_wdt_reset();
        tick++;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(ACQUISITION_PERIOD_MS));
    }
}

esp_err_t acquisition_runtime_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    bool persisted = false;
    (void)sensor_calibration_load(&s_calibration, &persisted);
    s_temperature_limits = signal_limits_temperature();
    s_pressure_limits = signal_limits_pressure();
    signal_filter_reset(&s_temperature_filter);
    signal_filter_reset(&s_pressure_filter);
    portENTER_CRITICAL(&s_lock);
    s_status.calibration_persisted = persisted;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "Calibración %s (%s)", s_calibration.id,
             persisted ? "recuperada" : "nominal");

    const esp_err_t result = ads1115_initialize(ADS1115_DEFAULT_ADDRESS);
    if (result != ESP_OK) {
        return result;
    }
    /* El sondeo ya respondió: se publica ahora y no en el primer ciclo, porque
     * el diagnóstico de arranque consulta el estado nada más volver de aquí y
     * leería el cero de la estructura como «ausente». Así se informó un
     * convertidor ausente que sí estaba el 2026-10-07. */
    portENTER_CRITICAL(&s_lock);
    s_status.converter_present = ads1115_is_present();
    portEXIT_CRITICAL(&s_lock);
    /* La tarea arranca aunque el convertidor no responda: publicará medidas no
     * adquiridas, que es información útil, en lugar de callar. */
    if (!ads1115_is_present()) {
        ESP_LOGW(TAG, "Sin convertidor en el bus; las medidas quedarán sin adquirir");
    }
    return xTaskCreatePinnedToCore(acquisition_task, "acquisition",
                                   ACQUISITION_TASK_STACK_BYTES, NULL,
                                   ACQUISITION_TASK_PRIORITY, &s_task,
                                   ACQUISITION_TASK_CORE) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

bool acquisition_runtime_get_status(acquisition_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

static bool copy_measurement(measurement_t *destination, bool temperature)
{
    if (destination == NULL || s_task == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    *destination = temperature ? s_status.temperature : s_status.pressure;
    portEXIT_CRITICAL(&s_lock);
    /* La antigüedad se evalúa al consultar, no al adquirir: una medida válida
     * deja de serlo si nadie la refresca. */
    const uint64_t age = monotonic_ms() - destination->acquired_ms;
    if (destination->quality == SIGNAL_VALID) {
        const signal_limits_t *limits =
            temperature ? &s_temperature_limits : &s_pressure_limits;
        if (age > limits->maximum_age_ms) {
            destination->quality = SIGNAL_STALE;
        }
    }
    return true;
}

bool acquisition_runtime_get_temperature(measurement_t *measurement)
{
    return copy_measurement(measurement, true);
}

bool acquisition_runtime_get_pressure(measurement_t *measurement)
{
    return copy_measurement(measurement, false);
}

bool acquisition_runtime_get_calibration(sensor_calibration_t *calibration)
{
    if (calibration == NULL || s_task == NULL) {
        return false;
    }
    /* Se fija al arrancar y no cambia después, de modo que no hace falta
     * protegerla contra la tarea de adquisición. */
    *calibration = s_calibration;
    return true;
}
