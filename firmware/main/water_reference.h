#pragma once

/*
 * La temperatura del perfil es la del agua sobre el café (decisión del usuario,
 * 2026-10-18; ADR-009, enmienda del 2026-10-18).
 *
 * El barista pide la temperatura del agua que cae sobre el café, no la del metal
 * de la caldera. Las dos no coinciden: la media del chorro entre 5 y 19 s,
 * medida dentro del portafiltro montado, sigue una recta del metal al pulsar
 * (evidencia `temperatura-cierre-2026-10-17.md`, «Curva caldera → agua» y su
 * comprobación). Este módulo invierte esa recta para obtener la referencia de
 * caldera que se le da al lazo, suponiendo el metal en la referencia al pulsar.
 *
 * La recta se toma contra el metal y no contra la referencia: contra la
 * referencia llevaba dentro lo que el metal se pasó de ella la tarde de la
 * curva, que en otra sesión fue distinto, y la comprobación falló por 1,4 °C.
 *
 * Los cuatro números (pendiente, ordenada y el intervalo de caldera medido)
 * salen del acta de temperatura, sección `water_reference`. Sin ella, el
 * firmware hace lo que hacía antes: la temperatura del perfil es la de la
 * caldera. Así un acta anterior sigue significando lo mismo que cuando se firmó.
 *
 * ## Lo que no hace
 *
 * - **No extrapola.** Una petición cuya caldera caería fuera del intervalo
 *   medido se rechaza; la recta no se ha comprobado allí, y por encima de
 *   100 °C el agua del primer chorro ya pasa del hervor local.
 * - **No corrige con el café.** La curva se midió con agua sola; con café la
 *   temperatura sobre la pastilla depende de la molienda y no entra aquí.
 * - **No mira el metal.** Convierte una referencia en otra; cómo llega el metal
 *   a ella es cosa del lazo.
 *
 * Puro: recibe la relación y la petición y devuelve la referencia de caldera.
 */

#include <stdbool.h>

/* Margen de redondeo en los extremos del intervalo. Pedir el agua exacta de un
 * extremo del intervalo da una caldera que en coma flotante
 * puede salir unas millonésimas fuera; eso no es extrapolar. */
#define WATER_REFERENCE_EDGE_C 0.001F

typedef struct {
    /* A cero, sin relación declarada: la temperatura del perfil es la de la
     * caldera, como antes de existir la sección del acta. */
    bool defined;
    float gain;              /* °C de agua por °C de caldera */
    float offset_c;          /* agua con la caldera en 0 °C, de la recta */
    float boiler_minimum_c;  /* intervalo de caldera medido */
    float boiler_maximum_c;
} water_reference_t;

/* La relación es utilizable: pendiente positiva y finita, intervalo ordenado. */
bool water_reference_is_valid(const water_reference_t *relation);

/*
 * Referencia de caldera para la temperatura que pide el perfil.
 *
 * Sin relación definida devuelve la misma temperatura. Con ella, devuelve la
 * caldera que da ese agua, o false si la relación no es válida o la caldera
 * caería fuera del intervalo medido; en ese caso no escribe en `boiler_c`.
 */
bool water_reference_boiler_setpoint(const water_reference_t *relation,
                                     float requested_c, float *boiler_c);

/* Agua que da una caldera: la recta directa, para los registros. */
float water_reference_water_for(const water_reference_t *relation, float boiler_c);
