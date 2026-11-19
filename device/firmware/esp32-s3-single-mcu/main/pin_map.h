#pragma once

#include "driver/gpio.h"

/* Mapa aprobado en esquema_interconexion_tres_placas_v1.md, sección 5.5. */
#define PIN_PUMP_ZC       GPIO_NUM_4
/* Repetidos a mano en `bootloader_components/salidas_en_reposo`, que los pone
 * en bajo en el cargador de arranque: si cambian aquí, cambian allí. */
#define PIN_PUMP_CMD      GPIO_NUM_5
#define PIN_HEATER_CMD    GPIO_NUM_6
#define PIN_I2C_SDA       GPIO_NUM_8
#define PIN_I2C_SCL       GPIO_NUM_9

#define PIN_BTN_POWER_N   GPIO_NUM_16
#define PIN_BTN_BREW_N    GPIO_NUM_17
/* Los nombres son los de las redes de la placa, que valen por coincidir con la
 * serigrafía y el esquemático, no por describir el uso. Desde el 2026-10-07
 * `BTN_STEAM_N` es el purgado y `BTN_PURGE_N` la tara: así la tara queda al
 * final del panel, a la derecha (ADR-006, revisión de esa fecha). La asignación
 * a funciones vive en `panel_runtime.c`. */
#define PIN_BTN_STEAM_N   GPIO_NUM_18
#define PIN_BTN_PURGE_N   GPIO_NUM_21

/* Báscula de ensayo: presente solo durante la caracterización del caudal.
 * Su ausencia no afecta a ninguna función de la cafetera. */
#define PIN_SCALE_DOUT    GPIO_NUM_10
#define PIN_SCALE_SCK     GPIO_NUM_11

/* El orden va de 41 a 38 porque así está trazada PCB-CONTROL (esquemático de la
 * liberación 2026-09-20: LED_SAFE en el pin 29, GPIO41). El mapa aprobado decía
 * 38 a 41 y el firmware lo siguió; el 2026-10-07, en C6 de la lista de
 * comprobación, la secuencia de arranque salió al revés y quedaba fija la
 * alarma en vez del estado seguro. Se corrige aquí y no en la placa, que ya
 * está fabricada; el esquema de interconexión se ajustó a la placa real. */
#define PIN_LED_SAFE      GPIO_NUM_41
#define PIN_LED_READY     GPIO_NUM_40
#define PIN_LED_BREW      GPIO_NUM_39
#define PIN_LED_ALARM     GPIO_NUM_38
