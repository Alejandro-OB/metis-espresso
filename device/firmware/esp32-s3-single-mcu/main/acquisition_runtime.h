#pragma once

/*
 * Adquisición periódica de temperatura y presión (RF-007, RF-008).
 *
 * Cada medida se publica con su unidad, su instante y su estado de calidad. Una
 * lectura no apta no se corrige ni se sustituye por la anterior: se marca, y el
 * consumidor decide. Mientras no haya convertidor, la tarea no inventa valores.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "sensor_calibration.h"
#include "signal_quality.h"

typedef struct {
    float value;               /* magnitud filtrada, en la unidad declarada */
    float raw_value;           /* magnitud antes de filtrar, para diagnóstico */
    float volts;               /* tensión leída */
    int16_t counts;            /* código del convertidor */
    const char *unit;          /* unidad de `value`, nunca implícita (RNF-004) */
    const char *calibration_id;/* calibración con la que se convirtió */
    uint64_t acquired_ms;      /* instante monótono de esta lectura concreta */
    signal_quality_t quality;
} measurement_t;

typedef struct {
    bool converter_present;
    bool calibration_persisted;
    uint32_t cycles;
    uint32_t read_failures;
    measurement_t temperature;  /* °C */
    measurement_t pressure;     /* bar */
} acquisition_status_t;

esp_err_t acquisition_runtime_start(void);
bool acquisition_runtime_get_status(acquisition_status_t *status);

/* Unidades declaradas de cada canal. */
#define ACQUISITION_UNIT_TEMPERATURE "Cel"
#define ACQUISITION_UNIT_PRESSURE "bar"

/* Copia la última medida de cada canal; false si la tarea no está activa. */
bool acquisition_runtime_get_temperature(measurement_t *measurement);
bool acquisition_runtime_get_pressure(measurement_t *measurement);

/*
 * Calibración con la que se convierte desde el arranque. Es la que rige, que no
 * tiene por qué ser la guardada: una guardada después rige en el siguiente.
 */
bool acquisition_runtime_get_calibration(sensor_calibration_t *calibration);
