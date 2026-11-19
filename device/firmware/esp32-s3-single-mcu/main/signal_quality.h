#pragma once

/*
 * Clasificación de la calidad de una medida antes de usarla (RF-008).
 *
 * Una señal no apta no se corrige ni se sustituye: se marca, y quien la consuma
 * decide. El control no puede regular con una lectura inválida y el registro no
 * debe promediarla.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    /*
     * El cero es deliberadamente el estado más conservador: una estructura
     * recién puesta a cero, o un campo que nadie ha escrito todavía, debe
     * significar «no hay medida», nunca «medida válida».
     */
    SIGNAL_NOT_ACQUIRED = 0,
    SIGNAL_VALID,
    SIGNAL_OPEN_CIRCUIT,    /* tensión pegada al extremo alto: sensor desconectado */
    SIGNAL_SHORT_CIRCUIT,   /* tensión pegada al extremo bajo: cortocircuito */
    SIGNAL_SATURATED,       /* entrada del convertidor al tope de su escala */
    SIGNAL_OUT_OF_RANGE,    /* magnitud fuera de lo físicamente posible */
    SIGNAL_STALE,           /* lectura demasiado antigua para gobernar nada */
} signal_quality_t;

typedef struct {
    float minimum_volts;      /* por debajo se considera cortocircuito */
    float maximum_volts;      /* por encima se considera circuito abierto */
    float minimum_value;      /* límite físico inferior de la magnitud */
    float maximum_value;      /* límite físico superior */
    uint32_t maximum_age_ms;  /* antigüedad máxima admisible */
} signal_limits_t;

/* Límites nominales de cada canal del prototipo. */
signal_limits_t signal_limits_temperature(void);
signal_limits_t signal_limits_pressure(void);

/*
 * Evalúa una lectura. `age_ms` es el tiempo transcurrido desde que se tomó.
 * El orden de comprobación va de la causa más específica a la más general, para
 * que el diagnóstico apunte al fallo real y no a su consecuencia.
 */
signal_quality_t signal_quality_evaluate(float volts,
                                         float value,
                                         uint32_t age_ms,
                                         bool acquired,
                                         const signal_limits_t *limits);

bool signal_quality_is_usable(signal_quality_t quality);
const char *signal_quality_name(signal_quality_t quality);
