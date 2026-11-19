#pragma once

/*
 * Sintonía en caliente del binario de identificación (ADR-020 § 4.2,
 * revisión del 2026-10-06; puesta en servicio, etapa 7).
 *
 * Nació para el lazo térmico y desde el 2026-10-12 sirve también al de presión
 * (enmienda a ADR-007 § 5): el módulo no sabe qué lazo sintoniza, solo recibe
 * una planta de primer orden con retardo. Cada binario sintoniza uno solo, el
 * que elige su secuencia.
 *
 * ADR-009 § 5 pide empezar con `Ki = 0` y subir `Kp` poco a poco sobre la
 * caldera real. Con la planta fijada en compilación eso exigía un acta, una
 * compilación y una grabación por valor probado. ADR-020 abre una excepción
 * acotada: **solo en el binario de identificación y solo tras la confirmación
 * física en el mando**, una orden por el puerto de servicio cambia `λ` y si la
 * integral está activa, y el PI regula con esos valores.
 *
 * Este módulo es la parte que puede equivocarse: interpreta la orden, guarda la
 * sintonía vigente con su número de revisión y calcula los parámetros del PI
 * por SIMC sobre la planta provisional. Es puro: no conoce el reloj, el puerto
 * ni el guardián.
 *
 * ## Qué no hace
 *
 * - **No cambia la planta.** `K`, `θ` y `τ` llegan compilados, de la 5.2 o,
 *   para la presión, de la 6.5 y la 6.6. Una
 *   orden de una línea no puede tocarlos: ADR-020 los ata a `commissioned`, y
 *   aquí ni siquiera se usan para eso.
 * - **No sube el tope.** La salida del PI se limita al techo de la secuencia,
 *   que el guardián ya juzgó contra su propio tope; este módulo solo declara
 *   ese techo como máximo del PI.
 * - **No pone nada en servicio.** `commissioned` sigue falso: el valor que se
 *   encuentre se congela después en un acta, y de ella sale el binario de
 *   operación.
 * - **No entra por la red.** Solo el puerto USB, con el operador delante.
 *
 * ## Formato de la orden
 *
 *   A,show
 *   A,lambda,<segundos>
 *   A,integral,on|off
 *
 * `A` de «ajuste»: `K` ya es la calibración y `T`, una línea del registro.
 *
 * ## La integral empieza apagada
 *
 * Es lo que ADR-009 § 5 manda: primero el proporcional solo, y la integral
 * cuando el seguimiento y el apagado se hayan visto. Que el estado inicial sea
 * el conservador lo garantiza la estructura puesta a cero.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "control_tuning.h"
#include "pi_controller.h"

/* Por debajo del retardo SIMC deja de ser conservador; por encima de 30 min la
 * respuesta es tan lenta que no se distingue de no regular. */
#define IDENTIFICATION_TUNING_LAMBDA_MAXIMUM_S 1800.0F

typedef enum {
    /* El cero es «esta línea no me incumbe». */
    IDENTIFICATION_TUNING_COMMAND_NONE = 0,
    IDENTIFICATION_TUNING_COMMAND_SHOW,
    IDENTIFICATION_TUNING_COMMAND_LAMBDA,
    IDENTIFICATION_TUNING_COMMAND_INTEGRAL,
    /* Es una orden de ajuste, pero no se puede cumplir tal como viene. */
    IDENTIFICATION_TUNING_COMMAND_INVALID,
} identification_tuning_command_kind_t;

typedef struct {
    identification_tuning_command_kind_t kind;
    float lambda_s;
    bool integral_enabled;
} identification_tuning_command_t;

typedef enum {
    IDENTIFICATION_TUNING_ACCEPTED = 0,
    IDENTIFICATION_TUNING_REJECTED_NOT_CONFIGURED,
    IDENTIFICATION_TUNING_REJECTED_LAMBDA,
    IDENTIFICATION_TUNING_REJECTED_COMMAND,
    /* La envoltura la usa cuando el modo restringido no está corriendo: sin
     * la confirmación física en el mando no se ajusta nada (ADR-020 § 4.2). */
    IDENTIFICATION_TUNING_REJECTED_NOT_CONFIRMED,
} identification_tuning_result_t;

typedef struct {
    bool configured;
    control_identified_plant_t plant;  /* provisional, de la 5.2 */
    float sample_period_s;
    float ceiling_percent;             /* techo de la secuencia */
    float lambda_s;
    bool integral_enabled;
    uint32_t revision;                 /* cuenta los cambios aceptados */
} identification_tuning_t;

/*
 * Interpreta una línea del puerto de servicio. Una línea que no empiece por
 * `A,` devuelve NONE, para que el lector pruebe los demás formatos.
 */
identification_tuning_command_t identification_tuning_parse(const char *line);

/*
 * Prepara la sintonía con la planta provisional y la `λ` inicial. La integral
 * queda apagada. Devuelve false, y deja el módulo sin configurar, si la planta
 * no admite SIMC o la `λ` no es aceptable: sin configurar, los parámetros que
 * entrega son los de un PI que no pide nada.
 */
bool identification_tuning_configure(identification_tuning_t *tuning,
                                     const control_identified_plant_t *plant,
                                     float sample_period_s,
                                     float ceiling_percent);

/* Aplica una orden ya interpretada. SHOW no cambia nada y se acepta. */
identification_tuning_result_t identification_tuning_apply(
    identification_tuning_t *tuning, const identification_tuning_command_t *command);

/*
 * Parámetros del PI para la sintonía vigente: SIMC sobre la planta, `Ki = 0`
 * con la integral apagada y salida entre 0 y el techo. Sin configurar, todo a
 * cero, de modo que el PI no pide potencia.
 */
pi_parameters_t identification_tuning_parameters(const identification_tuning_t *tuning);

/*
 * Identificador para la columna `tuning` del registro, p. ej. `ident-r3-l30-i1`:
 * revisión, `λ` en segundos enteros e integral. Es lo que hace que cada línea
 * diga con qué valores se reguló. Devuelve el número de caracteres escritos, o
 * negativo si no cabe.
 */
int identification_tuning_identifier(const identification_tuning_t *tuning,
                                     char *buffer, size_t buffer_size);

const char *identification_tuning_result_name(identification_tuning_result_t result);
