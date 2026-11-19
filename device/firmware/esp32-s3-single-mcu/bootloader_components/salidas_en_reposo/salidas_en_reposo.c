/*
 * Lleva PUMP_CMD y HEATER_CMD a bajo en el cargador de arranque.
 *
 * La aplicación ya los pone en bajo al empezar (`configure_gpio_safely`), pero
 * eso ocurre unos 0,75 s después del reinicio, tras la prueba de la PSRAM. El
 * 2026-10-08, en la puesta en servicio, la bomba daba un golpe de cerca de un
 * segundo en cada reinicio y al conectar el USB: durante esa ventana el pin no
 * estaba gobernado por nadie y el dimmer lo tomaba como orden. El gancho
 * `bootloader_before_init` corre en las primeras milésimas, antes incluso de
 * cargar la aplicación.
 *
 * Lo que no hace: no cubre el tramo de la ROM del chip, que va antes del
 * cargador y que ningún código nuestro puede tocar. Si quedara un golpe
 * residual, el remedio es de hardware —una resistencia más fuerte hacia GND en
 * la línea— y no de firmware.
 *
 * Los números de GPIO se repiten aquí a mano porque el cargador no ve
 * `main/pin_map.h`; deben coincidir con `PIN_PUMP_CMD` y `PIN_HEATER_CMD`.
 */

#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

#define PUMP_CMD_GPIO 5
#define HEATER_CMD_GPIO 6

/* Obliga al enlazador a incluir este archivo; los ganchos son débiles. */
void bootloader_hooks_include(void)
{
}

static void drive_low(uint32_t gpio)
{
    /* El nivel se fija antes de habilitar la salida, para que no haya un
     * instante en alto al conmutar. */
    gpio_ll_set_level(&GPIO, gpio, 0);
    esp_rom_gpio_pad_select_gpio(gpio);
    gpio_ll_output_enable(&GPIO, gpio);
    gpio_ll_set_level(&GPIO, gpio, 0);
}

void bootloader_before_init(void)
{
    drive_low(PUMP_CMD_GPIO);
    drive_low(HEATER_CMD_GPIO);
}
