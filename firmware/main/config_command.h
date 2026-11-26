#pragma once

/*
 * Lectura de una orden de configuración del enlace recibida por el puerto de
 * servicio.
 *
 * Módulo puro, verificable en el anfitrión: no depende de ESP-IDF, no toca
 * memoria no volátil y no decide nada sobre la máquina. Solo dice qué se pidió
 * y con qué valor, o que la línea no es una orden utilizable.
 *
 * Existe porque `device_config` sabía guardar la dirección y la credencial
 * desde el principio, pero nadie podía pedírselo: no había ninguna vía para
 * cambiar a dónde apunta el dispositivo sin recompilar y regrabar el firmware
 * que gobierna la máquina, que es precisamente lo que ADR-012 § 4.4 decide
 * evitar y lo que su § 9 exige verificar.
 *
 * La orden llega por el puerto físico y no por la red. Eso no es un detalle de
 * comodidad: cambiar a dónde envía el dispositivo su evidencia por una vía
 * remota sería una forma de desviarla, y el diseño mantiene fuera de la red toda
 * autoridad sobre el dispositivo.
 *
 * Este módulo **no valida que la dirección exista ni que la credencial sirva**.
 * Comprueba que lo recibido tiene la forma de una dirección y que cabe donde va
 * a guardarse; si responde o no, se sabrá al intentar entregar.
 */

#include <stdbool.h>
#include <stddef.h>

#include "device_config_limits.h"

typedef enum {
    /* El cero es «esta línea no me incumbe», que es lo que debe ocurrir con una
     * estructura sin rellenar: no fijar nada y no tocar la configuración. */
    CONFIG_COMMAND_NONE = 0,
    /* Mostrar a dónde apunta el dispositivo y de dónde procede cada dato. */
    CONFIG_COMMAND_SHOW,
    CONFIG_COMMAND_SET_URL,
    CONFIG_COMMAND_SET_TOKEN,
    /* Devolver el dispositivo a su configuración de puesta en servicio. */
    CONFIG_COMMAND_RESET,
    /* Fijar de dónde se baja un firmware nuevo (ADR-021). */
    CONFIG_COMMAND_SET_UPDATE_URL,
    /* Es una orden de configuración, pero no se puede cumplir. */
    CONFIG_COMMAND_INVALID,
} config_command_kind_t;

typedef struct {
    config_command_kind_t kind;
    /* Vacío salvo en las órdenes que fijan un valor. */
    char value[DEVICE_CONFIG_URL_BYTES];
} config_command_t;

/*
 * Interpreta una línea recibida por el puerto de servicio.
 *
 * Formato: `C,<orden>[,<valor>]`
 *
 *   C,show          a dónde apunta y de dónde viene cada dato
 *   C,url,<valor>   fija la dirección de la plataforma
 *   C,token,<valor> fija la credencial
 *   C,reset         descarta lo guardado
 *   C,ota,<valor>   fija el manifiesto del firmware en el computador (ADR-021)
 *
 * Una línea que no empiece por `C,` devuelve `CONFIG_COMMAND_NONE`, para que el
 * lector siga probando con los demás formatos del puerto.
 */
config_command_t config_command_parse(const char *line);

/*
 * Indica si un texto puede guardarse como dirección de la plataforma.
 *
 * Se exige protocolo explícito. Una cadena sin él no es una dirección a la que
 * se pueda enviar nada, y aceptarla dejaría al dispositivo sin destino creyendo
 * que tiene uno: el síntoma aparecería mucho después, como evidencia que no
 * llega.
 */
bool config_command_url_is_usable(const char *url);
