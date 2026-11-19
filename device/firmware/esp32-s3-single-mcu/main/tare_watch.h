#pragma once

/*
 * Aviso de tara corrida (pedido del usuario el 2026-10-17).
 *
 * Una tara deja la báscula en cero con la taza puesta. Si después la lectura se
 * aleja de cero —la taza se movió, cayó algo, la báscula derivó—, el café se
 * pesaría mal y nadie lo notaría hasta ver la masa final. El 2026-10-16 una tara
 * derivó a −1,4 g en segundos y hubo que repetirla antes de pulsar.
 *
 * La vigilancia se arma con cada tara hecha y se desarma al empezar un café o
 * un purgado: desde ahí la masa crece porque cae agua, y eso no es una tara
 * corrida. Una vez corrida, el aviso se queda hasta la siguiente tara aunque la
 * lectura vuelva a cero: un aviso que se apaga solo puede pasar sin verse.
 *
 * ## El umbral
 *
 * `TARE_WATCH_LIMIT_G` es provisional: tres veces la desviación de la báscula
 * en reposo medida el 2026-10-17 (0,29 g en 6 min), redondeado a 1 g. Se ajusta
 * con el uso.
 *
 * ## Lo que no hace
 *
 * - **No corrige la tara.** Avisa; tarar otra vez es cosa del barista.
 * - **No afirma una báscula sana sin lectura.** Sin masa disponible, la tara no
 *   se puede comprobar y se da por corrida.
 *
 * Puro: recibe la lectura y el estado y devuelve si avisar.
 */

#include <stdbool.h>

#include "operational_fsm.h"

#define TARE_WATCH_LIMIT_G 1.0F

typedef struct {
    /* A cero, desarmada y sin aviso: no hay tara que vigilar. */
    bool armed;
    bool drifted;
} tare_watch_t;

typedef struct {
    bool tare_taken;       /* se hizo una tara desde la llamada anterior */
    operational_state_t state;
    bool mass_available;
    float grams;
} tare_watch_input_t;

/* Avanza la vigilancia y devuelve si la tara está corrida. */
bool tare_watch_update(tare_watch_t *watch, const tare_watch_input_t *input);
