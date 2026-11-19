#pragma once

/*
 * Persistencia de la calibración de las cadenas de medida (ADR-020 § 4.1).
 *
 * Es la envoltura con memoria no volátil de `sensor_calibration`, que decide qué
 * es un registro válido. Aquí solo se guarda, se recupera y se borra.
 *
 * Lo guardado **rige desde el siguiente arranque**. Quien lee la calibración
 * para convertir medidas la carga una vez al arrancar y no vuelve a mirar, de
 * modo que guardar una nueva nunca hace que una serie en curso mezcle dos
 * calibraciones.
 */

#include <stdbool.h>

#include "esp_err.h"

#include "sensor_calibration.h"

/* Carga la persistida; si no hay ninguna válida, entrega la nominal. */
esp_err_t sensor_calibration_load(sensor_calibration_t *calibration, bool *persisted);

/* Valida y guarda. Rechaza sin escribir si los coeficientes no son posibles. */
esp_err_t sensor_calibration_store(const sensor_calibration_t *calibration);

/*
 * Lee lo guardado sin sustituirlo por la nominal. Devuelve `ESP_ERR_NOT_FOUND`
 * si no hay nada válido: el que pregunta qué hay guardado debe poder distinguir
 * «nada» de «la nominal».
 */
esp_err_t sensor_calibration_read_stored(sensor_calibration_t *calibration);

/* Borra lo guardado; desde el siguiente arranque rige la nominal. */
esp_err_t sensor_calibration_clear(void);
