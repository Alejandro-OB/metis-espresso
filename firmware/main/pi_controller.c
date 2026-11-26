#include "pi_controller.h"

#include <math.h>
#include <string.h>

static float clamp(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static bool finite_parameters(const pi_parameters_t *parameters)
{
    return isfinite(parameters->proportional_gain) && isfinite(parameters->integral_gain) &&
           isfinite(parameters->sample_period_s) && isfinite(parameters->output_minimum) &&
           isfinite(parameters->output_maximum) && isfinite(parameters->setpoint_rate);
}

bool pi_controller_configure(pi_controller_t *controller, const pi_parameters_t *parameters)
{
    if (controller == NULL || parameters == NULL || !finite_parameters(parameters)) {
        return false;
    }
    if (parameters->sample_period_s <= 0.0F ||
        parameters->output_maximum <= parameters->output_minimum) {
        return false;
    }
    memset(controller, 0, sizeof(*controller));
    controller->parameters = *parameters;
    return true;
}

void pi_controller_reset(pi_controller_t *controller, float measurement)
{
    if (controller == NULL) {
        return;
    }
    controller->integral = 0.0F;
    controller->internal_setpoint = isfinite(measurement) ? measurement : 0.0F;
    controller->primed = true;
}

void pi_controller_reset_with_integral(pi_controller_t *controller, float measurement,
                                       float integral)
{
    if (controller == NULL) {
        return;
    }
    pi_controller_reset(controller, measurement);
    if (isfinite(integral)) {
        controller->integral = clamp(integral, controller->parameters.output_minimum,
                                     controller->parameters.output_maximum);
    }
}

/*
 * Acerca la referencia interna a la autorizada sin superar la velocidad fijada.
 *
 * Una velocidad no positiva significa «sin límite» y no «congelada»: es la
 * interpretación útil, porque permite desactivar el limitador dejando el
 * parámetro en cero mientras no se haya medido la velocidad adecuada, que es un
 * valor reservado a la puesta en marcha en ambos ADR.
 *
 * **Solo frena las subidas, y es deliberado.** ADR-007 § 3.2 le da un propósito
 * concreto: evitar una aplicación instantánea del 100 %. Una referencia que baja
 * no puede producir eso; lo único que puede producir es menos actuación.
 * Frenarla también alargaría una actuación que ya no se quiere, y durante ese
 * tiempo el lazo seguiría persiguiendo una referencia que nadie autoriza. Se
 * detectó el 2026-09-15 al cerrar el lazo contra la planta de banco: bajar la
 * referencia de 20 a 5 bar dejaba la salida al 100 % durante cinco segundos.
 */
static float advance_setpoint(const pi_controller_t *controller, float authorized)
{
    const float rate = controller->parameters.setpoint_rate;
    if (rate <= 0.0F) {
        return authorized;
    }
    const float maximum_step = rate * controller->parameters.sample_period_s;
    const float difference = authorized - controller->internal_setpoint;
    if (difference > maximum_step) {
        return controller->internal_setpoint + maximum_step;
    }
    return authorized;
}

pi_result_t pi_controller_update(pi_controller_t *controller,
                                 float authorized_setpoint,
                                 float measurement,
                                 float feedforward)
{
    return pi_controller_update_holding(controller, authorized_setpoint, measurement,
                                        feedforward, false);
}

pi_result_t pi_controller_update_holding(pi_controller_t *controller,
                                         float authorized_setpoint,
                                         float measurement,
                                         float feedforward,
                                         bool hold_integral)
{
    pi_result_t result = {0};
    if (controller == NULL) {
        return result;
    }
    const pi_parameters_t *parameters = &controller->parameters;

    /* Una entrada no finita no puede gobernar: se responde con la salida más
     * conservadora y sin tocar el estado, de modo que un dato imposible no deje
     * el controlador envenenado para siempre. */
    if (!isfinite(authorized_setpoint) || !isfinite(measurement) || !isfinite(feedforward)) {
        result.setpoint = controller->internal_setpoint;
        result.integral = controller->integral;
        result.output = parameters->output_minimum;
        result.unlimited = parameters->output_minimum;
        result.integral_held = true;
        return result;
    }

    if (!controller->primed) {
        pi_controller_reset(controller, measurement);
    }

    controller->internal_setpoint = advance_setpoint(controller, authorized_setpoint);
    const float error = controller->internal_setpoint - measurement;

    /*
     * La integral se calcula como candidata y solo se adopta si el
     * anti-*windup* lo permite. Evaluarla antes de saber si la salida satura es
     * necesario: la condición de aceptación depende de la salida que produciría.
     */
    const float candidate = controller->integral +
                            parameters->integral_gain * error * parameters->sample_period_s;
    const float proportional = parameters->proportional_gain * error;
    /* Retenida, la salida usa la integral que hay, no la candidata: si no, cada
     * paso retenido sumaría a la salida un incremento que nunca se adopta. */
    const float integral_term = hold_integral ? controller->integral : candidate;
    const float unlimited = proportional + integral_term + feedforward;
    const float output = clamp(unlimited, parameters->output_minimum, parameters->output_maximum);

    const bool saturated = unlimited != output;
    /*
     * Condicionamiento integral: si la salida satura, la integral solo se
     * actualiza cuando el error empuja hacia dentro del rango. Así deja de
     * crecer contra un tope que no puede superar, y al invertirse el error la
     * salida abandona la saturación de inmediato en vez de esperar a que se
     * descargue lo acumulado.
     */
    bool leaving_saturation = false;
    if (saturated) {
        leaving_saturation = (unlimited > parameters->output_maximum && error < 0.0F) ||
                             (unlimited < parameters->output_minimum && error > 0.0F);
    }
    const bool accumulate = !hold_integral && (!saturated || leaving_saturation);
    if (accumulate) {
        controller->integral = candidate;
    }

    result.setpoint = controller->internal_setpoint;
    result.error = error;
    result.proportional = proportional;
    result.integral = controller->integral;
    result.feedforward = feedforward;
    result.unlimited = unlimited;
    result.output = output;
    result.saturated_high = unlimited > parameters->output_maximum;
    result.saturated_low = unlimited < parameters->output_minimum;
    result.integral_held = !accumulate;
    return result;
}

void pi_controller_relax_integral(pi_controller_t *controller, float target, float fraction)
{
    if (controller == NULL || !isfinite(target) || !isfinite(fraction) || fraction <= 0.0F) {
        return;
    }
    if (fraction > 1.0F) {
        fraction = 1.0F;
    }
    const float moved = controller->integral + (target - controller->integral) * fraction;
    controller->integral = clamp(moved, controller->parameters.output_minimum,
                                 controller->parameters.output_maximum);
}

float pi_controller_integral(const pi_controller_t *controller)
{
    return controller == NULL ? 0.0F : controller->integral;
}

float pi_controller_internal_setpoint(const pi_controller_t *controller)
{
    return controller == NULL ? 0.0F : controller->internal_setpoint;
}
