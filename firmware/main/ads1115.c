#include "ads1115.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_map.h"

#define REGISTER_CONVERSION 0x00
#define REGISTER_CONFIG 0x01

/*
 * Configuración: disparo único, ±4,096 V de fondo de escala (la señal llega
 * como máximo a 3,3 V), 860 muestras por segundo y comparador desactivado.
 *
 * La tasa máxima no se elige por resolución temporal del proceso sino por
 * latencia: a 860 SPS una conversión tarda 1,16 ms, de modo que los dos canales
 * caben con holgura en el ciclo de 10 ms que fija ADR-007 § 3.3. A 128 SPS cada
 * conversión tardaba 7,8 ms y ese ciclo era inalcanzable. El ruido que añade la
 * tasa alta lo absorben la mediana móvil y el filtro del lazo; el retardo, en
 * cambio, no se recupera después.
 */
#define CONFIG_SINGLE_SHOT 0x8000
#define CONFIG_MUX_AIN0 0x4000
#define CONFIG_MUX_AIN1 0x5000
#define CONFIG_PGA_4096 0x0200
#define CONFIG_MODE_SINGLE 0x0100
#define CONFIG_RATE_860SPS 0x00E0
#define CONFIG_COMPARATOR_OFF 0x0003

/* Bit OS del registro de configuración: al leerlo, 1 significa que el
 * convertidor no está convirtiendo, es decir, que el resultado está listo. */
#define CONFIG_STATUS_READY_MASK 0x80U

/*
 * Plazo del sondeo. Una conversión a 860 SPS tarda 1,16 ms; 5 ms cubre esa
 * duración con margen para el reloj interno del convertidor, que tolera una
 * desviación apreciable. Agotarlo no significa «no hay convertidor» —el bus
 * respondió— sino que la conversión no terminó, y eso se propaga como lectura
 * fallida.
 */
#define CONVERSION_TIMEOUT_US 5000
#define I2C_TIMEOUT_MS 100

static const char *TAG = "ads1115";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static bool s_present;

esp_err_t ads1115_initialize(uint8_t address)
{
    if (s_device != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t result = i2c_new_master_bus(&bus, &s_bus);
    if (result != ESP_OK) {
        return result;
    }
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 100000,
    };
    result = i2c_master_bus_add_device(s_bus, &device, &s_device);
    if (result != ESP_OK) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return result;
    }
    /* Una sondeo inicial distingue «no hay convertidor» de «hay un fallo». */
    result = i2c_master_probe(s_bus, address, I2C_TIMEOUT_MS);
    s_present = result == ESP_OK;
    if (!s_present) {
        ESP_LOGW(TAG, "El convertidor no respondió en 0x%02x", address);
    }
    return ESP_OK;
}

void ads1115_deinitialize(void)
{
    if (s_device != NULL) {
        i2c_master_bus_rm_device(s_device);
        s_device = NULL;
    }
    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    s_present = false;
}

/*
 * Espera a que el convertidor declare lista la conversión, sondeando su bit de
 * estado en lugar de dormir un plazo fijo.
 *
 * No hay pausa entre sondeos porque no hace falta: cada lectura del registro
 * ocupa el bus unos 0,3 ms a 100 kHz, de modo que el bucle gira tres o cuatro
 * veces durante una conversión y el propio I²C marca el ritmo. Dormir aquí
 * exigiría una espera de al menos un tick, que es lo que se quiere evitar.
 *
 * El plazo se consulta **antes** de cada sondeo, no después, para que el último
 * sondeo ocurra siempre con el plazo ya vencido. Comprobándolo después, una
 * pausa de la tarea entre un sondeo «aún no» y la comprobación —la escritura en
 * flash de la cola persistente al cerrar una extracción congela la caché unos
 * milisegundos— agotaba el plazo sin volver a mirar un resultado que ya estaba
 * listo. Ocurrió cuatro veces el 2026-10-10, siempre en ese instante, y cada
 * vez el supervisor llevó la máquina a alarma por temperatura no apta. Esto no
 * sustituye ninguna medida: lo que se devuelve es la conversión real.
 */
static esp_err_t wait_for_conversion(void)
{
    const int64_t deadline = esp_timer_get_time() + CONVERSION_TIMEOUT_US;
    const uint8_t pointer = REGISTER_CONFIG;
    uint8_t raw[2] = {0};
    bool expired;
    do {
        expired = esp_timer_get_time() >= deadline;
        const esp_err_t result = i2c_master_transmit_receive(s_device, &pointer, 1U, raw,
                                                            sizeof(raw), I2C_TIMEOUT_MS);
        if (result != ESP_OK) {
            s_present = false;
            return result;
        }
        if ((raw[0] & CONFIG_STATUS_READY_MASK) != 0U) {
            return ESP_OK;
        }
    } while (!expired);
    /* El convertidor responde pero no termina: es un fallo de lectura, no una
     * ausencia. Mantener `s_present` permite distinguir ambos casos. */
    ESP_LOGW(TAG, "La conversión no terminó dentro del plazo");
    return ESP_ERR_TIMEOUT;
}

esp_err_t ads1115_read(ads1115_channel_t channel, int16_t *counts)
{
    if (s_device == NULL || counts == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint16_t multiplexer =
        channel == ADS1115_CHANNEL_PRESSURE ? CONFIG_MUX_AIN0 : CONFIG_MUX_AIN1;
    const uint16_t configuration = CONFIG_SINGLE_SHOT | multiplexer | CONFIG_PGA_4096 |
                                   CONFIG_MODE_SINGLE | CONFIG_RATE_860SPS |
                                   CONFIG_COMPARATOR_OFF;
    const uint8_t request[3] = {REGISTER_CONFIG, (uint8_t)(configuration >> 8),
                                (uint8_t)(configuration & 0xFFU)};
    esp_err_t result = i2c_master_transmit(s_device, request, sizeof(request), I2C_TIMEOUT_MS);
    if (result != ESP_OK) {
        s_present = false;
        return result;
    }
    result = wait_for_conversion();
    if (result != ESP_OK) {
        return result;
    }

    const uint8_t pointer = REGISTER_CONVERSION;
    uint8_t raw[2] = {0};
    result = i2c_master_transmit_receive(s_device, &pointer, 1U, raw, sizeof(raw),
                                         I2C_TIMEOUT_MS);
    if (result != ESP_OK) {
        s_present = false;
        return result;
    }
    s_present = true;
    *counts = (int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return ESP_OK;
}

bool ads1115_is_present(void)
{
    return s_present;
}
