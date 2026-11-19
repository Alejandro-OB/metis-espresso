#pragma once

/*
 * Máximo de la compensación del primer uso que rige en este binario (ADR-009,
 * enmienda del 2026-10-19).
 *
 * Sale del acta de temperatura, sección `first_use_offset`, que el generador
 * `software/commissioning/plant.py` escribe como
 * `COMMISSIONED_TEMPERATURE_FIRST_USE_MAX_OFFSET_C`. Solo el binario de
 * identificación admite uno provisional, para el ensayo que lo mide antes de
 * que exista la sección: si se da, manda sobre el acta y el supervisor lo avisa
 * en el registro. Mismo patrón que el arranque rápido provisional
 * (`CONFIG_CAFE_HEATER_FAST_START_PROVISIONAL`).
 *
 * Sin ninguno de los dos, el máximo es cero y cualquier desplazamiento pedido
 * se rechaza con causa: no se sube la caldera con un número supuesto.
 *
 * No aplica nada: la decisión está en `first_use_offset.c`, que es puro.
 */

typedef enum {
    /* Cero es «sin máximo»: no se admite ningún desplazamiento. */
    FIRST_USE_OFFSET_SOURCE_NONE = 0,
    FIRST_USE_OFFSET_SOURCE_ACTA,
    FIRST_USE_OFFSET_SOURCE_PROVISIONAL,
} first_use_offset_source_t;

/* Máximo vigente en °C y, si `source` no es NULL, de dónde sale. */
float first_use_offset_maximum_c(first_use_offset_source_t *source);

/* Para el registro: el identificador del acta, o qué otra cosa rige. */
const char *first_use_offset_source_name(first_use_offset_source_t source);

/*
 * Ganancia de la prealimentación `ff-v1` que rige en este binario, y de dónde
 * sale (ADR-009 § 3.4): la opción provisional del binario de identificación,
 * el acta, o cero. Es la que configura `control_runtime` y contra la que se
 * juzga el refuerzo; vive aquí para que las dos partes lean la misma.
 */
float heater_feedforward_configured_gain(const char **source);

/*
 * Máximo del refuerzo del primer uso (ADR-009, enmienda del 2026-10-27): campo
 * `first_use_offset.max_feedforward_gain` del acta. Sin él, la ganancia
 * configurada: no se admite ningún refuerzo. No hay valor provisional: el
 * ensayo que lo midió se hizo con un binario de identificación con la ganancia
 * fija, y lo que firma el acta es lo que se midió.
 */
float first_use_feedforward_maximum_gain(void);
