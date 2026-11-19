#pragma once

/*
 * Tamaños de la configuración de enlace.
 *
 * Viven aparte de `device_config.h` porque esa cabecera arrastra ESP-IDF, y los
 * módulos puros que comprueban lo que se va a guardar —qué cabe y qué no— deben
 * compilarse también en el anfitrión. Separar solo las medidas evita que existan
 * dos definiciones del mismo hueco, que es como se guarda una credencial
 * truncada sin que nadie se entere.
 */

#define DEVICE_CONFIG_URL_BYTES 160U
#define DEVICE_CONFIG_TOKEN_BYTES 128U
