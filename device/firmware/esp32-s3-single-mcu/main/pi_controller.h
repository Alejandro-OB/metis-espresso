#pragma once

/*
 * Controlador PI unilateral, común a los dos lazos del proyecto.
 *
 * ADR-007 § 3.2 lo decide para la presión y ADR-009 § 3.3 adopta la misma
 * estructura para la temperatura, deliberadamente: las dos plantas comparten la
 * propiedad esencial de que el actuador solo puede aportar energía —la bomba no
 * aspira, el calentador no enfría—, de modo que una salida por debajo de cero no
 * significaría nada. Compartir ley reduce lo que hay que verificar y permite que
 * una sola prueba de anti-*windup* sirva a ambos.
 *
 *     e[k] = r[k] − y[k]
 *     u_sin_limitar[k] = Kp·e[k] + I[k] + u_ff[k]
 *     u[k] = limitar(u_sin_limitar[k], u_min, u_max)
 *
 * El módulo es puro: no lee sensores, no conoce el reloj y no toca salidas. Cada
 * ejecución recibe lo que necesita y devuelve lo que decidió, incluidos los
 * valores intermedios que ADR-007 § 3.6 y ADR-009 § 3.9 obligan a registrar.
 *
 * Tres propiedades merecen atención porque no se deducen de la fórmula:
 *
 * **La prealimentación entra en la salida, no en la referencia.** Es la
 * diferencia respecto a Gaggiuino, que eleva el umbral de temperatura durante la
 * extracción. Esa forma es correcta para un controlador sin integral, que se
 * estabiliza con error permanente y necesita apuntar más alto para acabar donde
 * quiere. Aquí sería un error: la integral empuja el error hasta cero, de modo
 * que el lazo cumpliría fielmente una referencia falsa y calentaría de más.
 * Sumándola a la salida, la referencia sigue diciendo la verdad y el PI conserva
 * autoridad para restar lo que sobre.
 *
 * **La saturación y el anti-*windup* operan sobre la suma completa**, con la
 * prealimentación incluida. Si la integral se evaluara contra la salida del PI
 * por separado, se cargaría compensando un término que ya está saturando.
 *
 * **La referencia interna persigue a la autorizada con velocidad acotada, solo
 * al subir.** Un salto de referencia hacia arriba produciría una aplicación
 * instantánea del 100 %, que es exactamente lo que un arranque suave debe
 * evitar. Un salto hacia abajo no puede producir eso, y frenarlo solo alargaría
 * una actuación que ya nadie autoriza.
 */

#include <stdbool.h>

typedef struct {
    float proportional_gain;   /* Kp, en unidades de salida por unidad de error */
    float integral_gain;       /* Ki, lo mismo por segundo */
    float sample_period_s;     /* periodo de ejecución del lazo */
    float output_minimum;      /* normalmente 0: el actuador no resta energía */
    float output_maximum;      /* normalmente 100 */
    float setpoint_rate;       /* unidades de referencia por segundo; <= 0 sin límite */
} pi_parameters_t;

typedef struct {
    pi_parameters_t parameters;
    float integral;
    float internal_setpoint;
    bool primed;
} pi_controller_t;

/* Lo que decidió una ejecución. Contiene los términos intermedios porque el
 * registro obligatorio de ambos ADR los exige, y porque sin ellos una sintonía
 * no puede diagnosticarse desde los datos. */
typedef struct {
    float setpoint;        /* referencia interna realmente aplicada */
    float error;
    float proportional;
    float integral;        /* término integral tras esta ejecución */
    float feedforward;
    float unlimited;       /* salida antes de limitar */
    float output;          /* salida aplicada */
    bool saturated_high;
    bool saturated_low;
    bool integral_held;    /* el anti-windup impidió acumular */
} pi_result_t;

/*
 * Fija los parámetros y deja el controlador sin cebar.
 *
 * Rechaza una configuración inutilizable —periodo no positivo, límites
 * invertidos, valores no finitos— en lugar de aceptarla y producir salidas sin
 * sentido. Un `Ki` de cero es válido y es el estado de partida que exigen ambos
 * ADR mientras el control no esté puesto en servicio.
 */
bool pi_controller_configure(pi_controller_t *controller, const pi_parameters_t *parameters);

/*
 * Olvida integral y referencia interna, y ceba esta última con la medida.
 *
 * Partir de la medida y no de cero es lo que da sentido al limitador de cambio:
 * la referencia interna arranca donde está el proceso y sube desde ahí. Se
 * invoca al habilitar el lazo y cada vez que se inhibe, para que al volver no
 * arrastre una integral acumulada mientras la salida estaba cortada.
 */
void pi_controller_reset(pi_controller_t *controller, float measurement);

/*
 * Como `pi_controller_reset`, pero la integral arranca en un valor dado,
 * limitado al rango de salida. Sirve para entregar el mando al PI desde una
 * fase anterior sin que arranque de cero ni del techo: la preinfusión de la
 * presión (ADR-007, enmienda del 2026-10-12) lo arranca en la potencia baja que
 * pide una pastilla ya cerrada. Un valor no finito arranca en cero.
 */
void pi_controller_reset_with_integral(pi_controller_t *controller, float measurement,
                                       float integral);

/*
 * Ejecuta un paso del lazo.
 *
 * `authorized_setpoint` es la referencia validada; la interna la persigue con la
 * velocidad configurada. `feedforward` es cero salvo que haya prealimentación
 * activa. Antes del primer `reset` el controlador se ceba solo, con la medida
 * recibida.
 */
pi_result_t pi_controller_update(pi_controller_t *controller,
                                 float authorized_setpoint,
                                 float measurement,
                                 float feedforward);

/*
 * Como `pi_controller_update`, pero con `hold_integral` la integral no se
 * actualiza en este paso, sature o no la salida. El proporcional y la
 * prealimentación siguen actuando: retener la integral no recorta la potencia
 * que pide el error, solo impide que una perturbación pasajera se aprenda como
 * si fuera la potencia de sostenimiento. Quién decide retenerla, y cuándo, no
 * es asunto de este módulo (ver `heater_recovery_hold`).
 */
pi_result_t pi_controller_update_holding(pi_controller_t *controller,
                                         float authorized_setpoint,
                                         float measurement,
                                         float feedforward,
                                         bool hold_integral);

/*
 * Acerca la integral a `target` en la fracción dada de la distancia (0–1),
 * dentro del rango de salida. Es la devolución de `heater_recovery_return`:
 * no cambia la ley del PI, solo mueve el estado que esa ley va a usar en el
 * paso siguiente. Un objetivo o una fracción no finitos no hacen nada.
 */
void pi_controller_relax_integral(pi_controller_t *controller, float target, float fraction);

/* Estado interno, para el registro y el diagnóstico. */
float pi_controller_integral(const pi_controller_t *controller);
float pi_controller_internal_setpoint(const pi_controller_t *controller);
