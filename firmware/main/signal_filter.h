#pragma once

/*
 * Filtrado de la señal ya validada.
 *
 * Se emplea una mediana móvil y no una media: el ruido que sobrevive al filtro
 * físico es principalmente impulsivo —conmutación de cargas, rizado residual de
 * la bomba— y la mediana lo rechaza sin arrastrar el valor, mientras que una
 * media desplaza la salida en proporción al impulso.
 *
 * Solo entran muestras válidas. Una lectura no apta no se filtra ni sustituye a
 * la anterior: interrumpe la ventana, porque promediar a través de un hueco
 * inventaría continuidad donde no la hubo.
 */

#include <stdbool.h>
#include <stddef.h>

#define SIGNAL_FILTER_WINDOW 5U

typedef struct {
    float samples[SIGNAL_FILTER_WINDOW];
    size_t count;      /* muestras acumuladas, hasta el tamaño de la ventana */
    size_t next;       /* posición de escritura en el anillo */
} signal_filter_t;

void signal_filter_reset(signal_filter_t *filter);

/*
 * Incorpora una muestra válida y devuelve la mediana de la ventana. Mientras la
 * ventana no está llena devuelve la mediana de lo acumulado, de modo que la
 * señal esté disponible desde la primera lectura.
 */
float signal_filter_push(signal_filter_t *filter, float sample);

/* Último valor filtrado; false si la ventana está vacía. */
bool signal_filter_value(const signal_filter_t *filter, float *value);

size_t signal_filter_count(const signal_filter_t *filter);
