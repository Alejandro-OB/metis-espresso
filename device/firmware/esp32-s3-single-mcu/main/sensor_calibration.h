#pragma once

/*
 * Calibración de las cadenas de medida (RNF-004, ADR-020 § 4.1).
 *
 * Cada medición debe poder decir qué calibración usó. Los coeficientes de
 * partida son los nominales del esquema; la calibración de la cadena real los
 * sustituye y queda guardada, de modo que una lectura registrada hoy siga
 * siendo interpretable mañana.
 *
 * Este módulo es puro: define el registro y decide si es físicamente posible.
 * Guardarlo y recuperarlo es de `sensor_calibration_store`, que es lo único que
 * conoce la memoria no volátil. La separación existe para que la orden de
 * calibración, que llega por el puerto de servicio, se pruebe en el anfitrión
 * con exactamente el mismo criterio de validez con el que se guarda.
 *
 * ## Un solo registro para las tres cadenas
 *
 * Temperatura, presión y báscula comparten identificador y versión. Calibrar
 * una sola cadena produce un registro nuevo que copia las otras dos, y lo hace
 * el programa del computador, no el operador. Así cada medida lleva un único
 * identificador que dice todo lo que hay que saber de su conversión, en vez de
 * tres que habría que cruzar.
 */

#include <stdbool.h>
#include <stdint.h>

#include "sensor_scaling.h"

#define SENSOR_CALIBRATION_ID_LENGTH 32U

typedef struct {
    char id[SENSOR_CALIBRATION_ID_LENGTH];  /* identificador que viaja con la medida */
    uint32_t version;
    ntc_scaling_t temperature;
    pressure_scaling_t pressure;
    /*
     * Cuentas por gramo de la báscula de ensayo, medidas con una masa patrón.
     * Cero significa «sin calibrar», que es el valor nominal: no hay factor de
     * catálogo para una célula concreta. La tara no está aquí porque no es
     * calibración: se toma al empezar cada sesión con la báscula vacía.
     */
    float scale_counts_per_gram;
} sensor_calibration_t;

/* Coeficientes nominales del esquema, sin ajuste de la cadena real. */
sensor_calibration_t sensor_calibration_nominal(void);

/*
 * Un identificador utilizable: no vacío, que cabe con su terminador, y hecho
 * solo de letras, cifras, `.`, `_` y `-`. Viaja en columnas separadas por comas
 * y en nombres de archivo, y una coma dentro lo partiría en dos.
 */
bool sensor_calibration_id_is_usable(const char *id);

/*
 * Comprueba que los coeficientes son físicamente posibles. Una calibración
 * corrupta o absurda no puede gobernar una medición: se rechaza y se vuelve a
 * la nominal.
 */
bool sensor_calibration_is_valid(const sensor_calibration_t *calibration);
