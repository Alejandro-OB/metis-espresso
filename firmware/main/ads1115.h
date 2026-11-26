#pragma once

/*
 * Lectura del convertidor ADS1115 por I²C, en modo de disparo único.
 *
 * El módulo lleva `ADDR` a `GND_ISO`, de modo que responde en 0x48. Se emplea
 * disparo único y no modo continuo: cada canal se convierte cuando se pide, lo
 * que evita mezclar lecturas de entradas distintas al multiplexar.
 *
 * La conversión corre a 860 muestras por segundo y su final se detecta
 * sondeando el bit de estado del convertidor, no esperando un plazo fijo. Así
 * `ads1115_read` retorna en cuanto el dato existe —alrededor de 1,5 ms— y el
 * ciclo de adquisición de 10 ms que fija ADR-007 § 3.3 es alcanzable.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define ADS1115_DEFAULT_ADDRESS 0x48

typedef enum {
    ADS1115_CHANNEL_PRESSURE = 0,  /* AIN0 */
    ADS1115_CHANNEL_TEMPERATURE,   /* AIN1 */
} ads1115_channel_t;

esp_err_t ads1115_initialize(uint8_t address);
void ads1115_deinitialize(void);

/* Convierte un canal y devuelve su código con signo. */
esp_err_t ads1115_read(ads1115_channel_t channel, int16_t *counts);

/* Indica si el convertidor respondió a la última operación. */
bool ads1115_is_present(void);
