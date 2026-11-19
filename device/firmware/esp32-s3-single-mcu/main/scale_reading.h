#pragma once

/*
 * Interpretación de lo que entrega el HX711 de la báscula de ensayo.
 *
 * ## Qué es esta báscula y qué no es
 *
 * Es **instrumentación de ensayo**, no una función de la cafetera. Existe para
 * la campaña de caudal de ADR-006: pesando la bebida a lo largo de la extracción
 * se obtiene el caudal real con el que validar la estimación que hoy hace
 * ADR-009 § 3.4 a partir de la densidad PSM, es decir, para pasar de `ff-v1` a
 * `ff-v2`.
 *
 * De ahí se sigue lo más importante: **su ausencia no puede afectar a nada**. El
 * lazo de control, el supervisor y el registro de preparaciones deben comportarse
 * igual con báscula y sin ella, y una báscula desconectada o averiada no es una
 * condición insegura. Por eso no entra en el guardián de actuación.
 *
 * ## Sin calibración medida no hay gramos
 *
 * El HX711 entrega cuentas de un convertidor de 24 bits. Convertirlas a masa
 * exige un factor que **solo se obtiene pesando una masa patrón conocida**, y ese
 * factor depende de la célula de carga concreta, de su montaje y de su tensión
 * de excitación.
 *
 * Mientras ese factor no se haya medido, este módulo entrega cuentas y declara
 * que **la masa no está disponible**. No inventa un valor plausible: un número
 * en gramos que nadie ha calibrado se parece demasiado a una medida para dejarlo
 * suelto en un registro que después sostendrá una campaña experimental.
 *
 * ## El módulo es puro
 *
 * No toca pines ni conoce el reloj. El protocolo de dos hilos vive en
 * `hx711_runtime`, y aquí queda lo que puede equivocarse sin que se note:
 * la extensión de signo de 24 a 32 bits, la detección de saturación, la tara y
 * el criterio de lectura asentada.
 */

#include <stdbool.h>
#include <stdint.h>

#include "signal_quality.h"

/* Extremos del convertidor de 24 bits con signo. */
#define SCALE_COUNTS_MINIMUM (-8388608)
#define SCALE_COUNTS_MAXIMUM (8388607)

/* Muestras de la ventana con la que se juzga si la lectura está asentada. */
#define SCALE_STABILITY_SAMPLES 8U

typedef struct {
    /*
     * Cuentas por gramo, **medidas con una masa patrón**. Un valor no positivo
     * significa «sin calibrar», y entonces no se entregan gramos.
     */
    float counts_per_gram;
    /* Lectura en vacío, restada antes de convertir. */
    int32_t tare_counts;
    /* Identificador de la calibración, para que un registro diga con qué se
     * convirtió. NULL mientras no exista ninguna. */
    const char *calibration_id;
} scale_calibration_t;

typedef struct {
    int32_t counts;        /* siempre disponible, calibrada o no */
    float grams;           /* válido solo si `mass_available` */
    bool mass_available;
    signal_quality_t quality;
} scale_reading_t;

/*
 * Lleva los 24 bits crudos del HX711 a un entero con signo.
 *
 * El convertidor entrega complemento a dos de 24 bits; copiarlos a un `int32_t`
 * sin extender el signo convierte cualquier peso negativo —una célula cargada al
 * revés, o una tara mal tomada— en un valor enorme y positivo. Es el error
 * clásico de este integrado y no se nota hasta que la báscula marca ocho
 * millones.
 */
int32_t scale_reading_sign_extend(uint32_t raw);

/*
 * Interpreta una lectura.
 *
 * `acquired` es falso cuando el hilo de datos no llegó a estar listo: entonces
 * la calidad es «no adquirida» y no se inventa ningún número. `age_ms` permite
 * declarar desactualizada una lectura vieja, con el mismo criterio que el resto
 * de los canales del sistema.
 *
 * `released` dice si `DOUT` volvió a nivel alto tras el pulso 25 de la ráfaga,
 * como hace siempre un HX711 que responde. Si siguió bajo, la línea está pegada
 * a masa o el reloj no llega al integrado, y los 24 bits leídos son ceros que
 * nadie convirtió: la lectura se marca en cortocircuito, con sus cuentas tal
 * cual. Sin esta comprobación esos ceros pasaban por válidos y asentados
 * (calibración de la báscula, 2026-10-09).
 */
scale_reading_t scale_reading_evaluate(int32_t counts,
                                       const scale_calibration_t *calibration,
                                       uint32_t age_ms,
                                       uint32_t maximum_age_ms,
                                       bool acquired,
                                       bool released);

/*
 * Ventana para decidir si la lectura está asentada.
 *
 * Importa para dos cosas: tomar la tara —que sobre una báscula que aún oscila
 * queda mal tomada y desplaza todo lo que venga después— y registrar la masa
 * final de una bebida. Una célula de carga tarda en estabilizarse tras un
 * golpe, y ninguna media lo arregla.
 */
typedef struct {
    int32_t samples[SCALE_STABILITY_SAMPLES];
    uint32_t count;   /* muestras acumuladas, hasta el tamaño de la ventana */
    uint32_t index;   /* próxima posición a escribir */
} scale_stability_t;

void scale_stability_reset(scale_stability_t *window);

/*
 * Añade una muestra y responde si la ventana está llena y su recorrido cabe
 * dentro de la tolerancia. Con la ventana a medias devuelve false: no se puede
 * afirmar que algo está quieto habiéndolo mirado dos veces.
 */
bool scale_stability_push(scale_stability_t *window, int32_t counts, uint32_t tolerance_counts);

/* Media de la ventana, para tomar la tara sobre algo más firme que una muestra. */
int32_t scale_stability_average(const scale_stability_t *window);

/* Recorrido entre el mínimo y el máximo de la ventana; 0 si está vacía. */
uint32_t scale_stability_spread(const scale_stability_t *window);
