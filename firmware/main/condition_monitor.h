#pragma once

/*
 * Detección de condiciones que deben comunicarse al historial.
 *
 * El sistema debe informar de sobretemperatura, sensor crítico inválido, tarea
 * detenida o condición insegura. Ninguna de ellas produce necesariamente una
 * transición de la máquina de estados: un sensor puede soltarse con la cafetera
 * en reposo, el guardián corta las salidas por su propia ruta y el estado
 * operativo no cambia. Por eso los eventos que nacen de transiciones no bastan.
 *
 * Este módulo es puro: recibe el veredicto vigente del guardián y decide si hay
 * algo nuevo que comunicar. No conoce el reloj, no serializa y no encola.
 *
 * Observa **flancos y no niveles**: publicar mientras dure una avería inundaría
 * la cola, y la cola guarda evidencia. Se informa al entrar en la condición y al
 * salir de ella.
 */

#include <stdbool.h>

#include "actuation_guard.h"

typedef enum {
    CONDITION_EVENT_NONE = 0,
    CONDITION_EVENT_FAULT_RAISED,
    CONDITION_EVENT_FAULT_CLEARED,
} condition_event_kind_t;

typedef struct {
    condition_event_kind_t kind;
    /* Veredicto que provocó el aviso; al despejarse, el que lo había provocado. */
    actuation_verdict_t verdict;
    /* Salida que declaró la condición, para que el aviso diga dónde ocurrió. */
    bool from_pump;
} condition_event_t;

typedef struct {
    /* Falso hasta la primera observación: el valor inicial del guardián no es
     * ni avería ni permiso, y tratarlo como estado previo produciría un aviso
     * en el primer ciclo que no corresponde a ningún cambio real. */
    bool known;
    actuation_verdict_t pump;
    actuation_verdict_t heater;
} condition_monitor_t;

void condition_monitor_init(condition_monitor_t *monitor);

/*
 * Observa el veredicto vigente y devuelve el aviso que corresponda. La decisión
 * llega completa, tomada en un mismo instante: leer bomba y calentador por
 * separado podría describir una combinación que nunca existió.
 */
condition_event_t condition_monitor_observe(condition_monitor_t *monitor,
                                            const actuation_decision_t *decision);
