#pragma once

/*
 * Conversión de las lecturas del ADS1115 a magnitudes físicas.
 *
 * Módulo puro, verificable en el host. Las cadenas de medida están descritas en
 * `hardware/kicad/current/esp32-s3-single-mcu/esquema_interconexion_tres_placas_v1.md`:
 *
 *   Temperatura  +3V3 ── 10 kΩ ──┬── NTC ── GND      (divisor, NTC abajo)
 *                                └── 1 kΩ ── AIN1
 *   Presión      HK1100C 0,5–4,5 V ── 10 kΩ/20 kΩ ── AIN0   (relación 2/3)
 *
 * Los coeficientes son configurables porque la calibración de la cadena real
 * los sustituirá: aquí solo viven los valores nominales de partida.
 */

#include <stdbool.h>
#include <stdint.h>

/* Fondo de escala del ADS1115 con ganancia unitaria (PGA ±4,096 V). */
#define ADS1115_FULL_SCALE_V 4.096F
#define ADS1115_FULL_SCALE_COUNTS 32768.0F

typedef struct {
    float supply_v;        /* tensión del divisor, +3V3 nominal */
    float series_ohms;     /* resistencia superior del divisor */
    float reference_ohms;  /* resistencia del NTC a la temperatura de referencia */
    float reference_c;     /* temperatura de referencia, 25 °C nominal */
    float beta_k;          /* coeficiente β del NTC */
} ntc_scaling_t;

typedef struct {
    float gain_bar_per_v;  /* pendiente de la recta del transductor */
    float offset_bar;      /* desplazamiento de cero */
} pressure_scaling_t;

/* Valores nominales del prototipo: NTC MF58-104X3950YA y HK1100C de 0–12 bar. */
ntc_scaling_t sensor_ntc_default(void);
pressure_scaling_t sensor_pressure_default(void);

/* Tensión en la entrada analógica a partir del código de 16 bits con signo. */
float sensor_volts_from_counts(int16_t counts);

/*
 * Resistencia del NTC deducida del divisor. Devuelve un valor negativo si la
 * tensión no es físicamente posible para esta cadena.
 */
float sensor_ntc_resistance_ohms(float volts, const ntc_scaling_t *scaling);

/* Temperatura por la ecuación β. Devuelve false si la resistencia no es válida. */
bool sensor_ntc_temperature_c(float resistance_ohms, const ntc_scaling_t *scaling,
                              float *temperature_c);

/* Presión del transductor, que entrega una recta sobre su alimentación. */
float sensor_pressure_bar(float volts, const pressure_scaling_t *scaling);

/* Tensión que produciría una temperatura dada; útil para fijar umbrales. */
float sensor_ntc_volts_at(float temperature_c, const ntc_scaling_t *scaling);
