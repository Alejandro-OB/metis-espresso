#pragma once

/*
 * Orden de calibración recibida por el puerto de servicio (ADR-020 § 4.1, F2).
 *
 * Es el mismo patrón que `config_command`: un módulo puro que dice qué se pidió
 * y si se puede cumplir, y una envoltura tonta en `external_hil_runtime` que lo
 * guarda. No toca memoria no volátil, no aplica nada en caliente y no conoce la
 * máquina.
 *
 * ## Por el puerto de servicio y no por la red
 *
 * La calibración cambia cómo se interpreta una lectura. ADR-020 la deja entrar
 * por USB porque exige estar delante de la máquina, que es además donde se
 * calibra: el computador que registró la medida es el que envía el resultado.
 * La planta identificada, que es la llave de la actuación, no entra por aquí ni
 * por ninguna otra orden (ADR-020 § 4.2).
 *
 * ## Formato
 *
 *   K,show
 *   K,set,<id>,<versión>,<v_ntc>,<r_serie>,<r_ref>,<t_ref>,<beta>,<ganancia>,<cero>,<cuentas_g>
 *   K,reset
 *   K,tare
 *
 * Los campos de `set` son, en orden, los de `sensor_calibration_t`: tensión del
 * divisor del NTC (V), resistencia superior (Ω), resistencia del NTC a la
 * temperatura de referencia (Ω), esa temperatura (°C), β (K), pendiente de la
 * presión (bar/V), cero de la presión (bar) y cuentas por gramo de la báscula
 * (0 = sin calibrar).
 *
 * **Se envía el registro entero, no un campo.** Calibrar una sola cadena es
 * copiar las otras del registro vigente y cambiar la suya, y eso lo hace el
 * programa del computador. Una orden por campo dejaría estados intermedios
 * —el NTC nuevo con un identificador viejo— que ninguna medida debería poder
 * declarar.
 *
 * La respuesta del dispositivo escribe el registro con el mismo formato, de
 * modo que quien envía puede leer lo guardado con este mismo intérprete y
 * compararlo campo a campo con lo que mandó. Esa relectura es la confirmación:
 * un dígito alterado en el cable produce un registro plausible, y solo
 * comparándolo se descubre.
 */

#include <stdbool.h>
#include <stddef.h>

#include "sensor_calibration.h"

typedef enum {
    /* El cero es «esta línea no me incumbe»: no fijar nada. */
    CALIBRATION_COMMAND_NONE = 0,
    CALIBRATION_COMMAND_SHOW,
    CALIBRATION_COMMAND_SET,
    CALIBRATION_COMMAND_RESET,
    CALIBRATION_COMMAND_TARE,
    /* Es una orden de calibración, pero no se puede cumplir. */
    CALIBRATION_COMMAND_INVALID,
} calibration_command_kind_t;

/* Por qué se rechazó. El cero, «no se rechazó». */
typedef enum {
    CALIBRATION_REJECT_NONE = 0,
    CALIBRATION_REJECT_SYNTAX,       /* orden desconocida, campos de más o de menos */
    CALIBRATION_REJECT_IDENTIFIER,   /* vacío, con caracteres no admitidos o el nominal */
    CALIBRATION_REJECT_VERSION,      /* ausente, cero o no numérica */
    CALIBRATION_REJECT_NUMBER,       /* un coeficiente que no es un número */
    CALIBRATION_REJECT_IMPLAUSIBLE,  /* números, pero físicamente imposibles */
} calibration_reject_t;

typedef struct {
    calibration_command_kind_t kind;
    calibration_reject_t reject;
    sensor_calibration_t calibration;  /* solo en `SET` */
} calibration_command_t;

/*
 * Interpreta una línea del puerto de servicio. Una que no empiece por `K,`
 * devuelve `CALIBRATION_COMMAND_NONE` para que el lector siga probando con los
 * demás formatos.
 *
 * La validez física es la de `sensor_calibration_is_valid`, la misma con la que
 * se guarda: no hay dos criterios que puedan separarse.
 */
calibration_command_t calibration_command_parse(const char *line);

/*
 * Escribe los campos de un registro tal como los espera `K,set`, sin el
 * prefijo ni el salto de línea. Los números van con nueve cifras
 * significativas, que es lo que un `float` necesita para volver idéntico.
 *
 * Devuelve los bytes escritos, o -1 si no caben: nunca deja uno a medias.
 */
int calibration_command_format(const sensor_calibration_t *calibration,
                               char *buffer,
                               size_t size);

const char *calibration_reject_name(calibration_reject_t reject);
