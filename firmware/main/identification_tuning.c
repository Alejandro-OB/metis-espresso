#include "identification_tuning.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* El mismo suelo que `control_tuning`: con θ = 0 SIMC divide por cero, y el
 * propio muestreo introduce medio periodo de retardo. */
static float effective_dead_time(float dead_time_s, float sample_period_s)
{
    const float floor_s = 0.5F * sample_period_s;
    return dead_time_s > floor_s ? dead_time_s : floor_s;
}

static bool lambda_acceptable(const control_identified_plant_t *plant, float lambda_s)
{
    /* Las comparaciones con NaN son falsas: un valor no finito no pasa. */
    return lambda_s >= plant->dead_time_s && lambda_s > 0.0F &&
           lambda_s <= IDENTIFICATION_TUNING_LAMBDA_MAXIMUM_S;
}

identification_tuning_command_t identification_tuning_parse(const char *line)
{
    identification_tuning_command_t command = {0};
    if (line == NULL || strncmp(line, "A,", 2U) != 0) {
        return command;
    }
    char text[64];
    const size_t length = strcspn(line + 2, "\r\n");
    if (length >= sizeof(text)) {
        command.kind = IDENTIFICATION_TUNING_COMMAND_INVALID;
        return command;
    }
    memcpy(text, line + 2, length);
    text[length] = '\0';

    if (strcmp(text, "show") == 0) {
        command.kind = IDENTIFICATION_TUNING_COMMAND_SHOW;
        return command;
    }
    if (strcmp(text, "integral,on") == 0 || strcmp(text, "integral,off") == 0) {
        command.kind = IDENTIFICATION_TUNING_COMMAND_INTEGRAL;
        command.integral_enabled = strcmp(text, "integral,on") == 0;
        return command;
    }
    if (strncmp(text, "lambda,", 7U) == 0) {
        char *end = NULL;
        const float value = strtof(text + 7, &end);
        /* Toda la línea tiene que ser el número: «30s» o «30,5» no son 30. */
        if (end != text + 7 && *end == '\0' && isfinite(value)) {
            command.kind = IDENTIFICATION_TUNING_COMMAND_LAMBDA;
            command.lambda_s = value;
            return command;
        }
    }
    command.kind = IDENTIFICATION_TUNING_COMMAND_INVALID;
    return command;
}

bool identification_tuning_configure(identification_tuning_t *tuning,
                                     const control_identified_plant_t *plant,
                                     float sample_period_s,
                                     float ceiling_percent)
{
    if (tuning == NULL) {
        return false;
    }
    *tuning = (identification_tuning_t){0};
    if (plant == NULL || !(sample_period_s > 0.0F) ||
        !(ceiling_percent > 0.0F && ceiling_percent <= 100.0F) ||
        !lambda_acceptable(plant, plant->lambda_s)) {
        return false;
    }
    float kp = 0.0F;
    float ki = 0.0F;
    if (!control_tuning_simc(plant->gain, plant->time_constant_s,
                             effective_dead_time(plant->dead_time_s, sample_period_s),
                             plant->lambda_s, &kp, &ki)) {
        return false;
    }
    tuning->plant = *plant;
    tuning->sample_period_s = sample_period_s;
    tuning->ceiling_percent = ceiling_percent;
    tuning->lambda_s = plant->lambda_s;
    tuning->integral_enabled = false;
    tuning->configured = true;
    return true;
}

identification_tuning_result_t identification_tuning_apply(
    identification_tuning_t *tuning, const identification_tuning_command_t *command)
{
    if (tuning == NULL || !tuning->configured) {
        return IDENTIFICATION_TUNING_REJECTED_NOT_CONFIGURED;
    }
    if (command == NULL) {
        return IDENTIFICATION_TUNING_REJECTED_COMMAND;
    }
    switch (command->kind) {
    case IDENTIFICATION_TUNING_COMMAND_SHOW:
        return IDENTIFICATION_TUNING_ACCEPTED;
    case IDENTIFICATION_TUNING_COMMAND_LAMBDA:
        if (!lambda_acceptable(&tuning->plant, command->lambda_s)) {
            return IDENTIFICATION_TUNING_REJECTED_LAMBDA;
        }
        tuning->lambda_s = command->lambda_s;
        tuning->revision++;
        return IDENTIFICATION_TUNING_ACCEPTED;
    case IDENTIFICATION_TUNING_COMMAND_INTEGRAL:
        tuning->integral_enabled = command->integral_enabled;
        tuning->revision++;
        return IDENTIFICATION_TUNING_ACCEPTED;
    case IDENTIFICATION_TUNING_COMMAND_NONE:
    case IDENTIFICATION_TUNING_COMMAND_INVALID:
    default:
        return IDENTIFICATION_TUNING_REJECTED_COMMAND;
    }
}

pi_parameters_t identification_tuning_parameters(const identification_tuning_t *tuning)
{
    pi_parameters_t parameters = {0};
    if (tuning == NULL || !tuning->configured) {
        return parameters;
    }
    float kp = 0.0F;
    float ki = 0.0F;
    if (!control_tuning_simc(tuning->plant.gain, tuning->plant.time_constant_s,
                             effective_dead_time(tuning->plant.dead_time_s,
                                                 tuning->sample_period_s),
                             tuning->lambda_s, &kp, &ki)) {
        return parameters;
    }
    parameters.proportional_gain = kp;
    parameters.integral_gain = tuning->integral_enabled ? ki : 0.0F;
    parameters.sample_period_s = tuning->sample_period_s;
    parameters.output_minimum = 0.0F;
    parameters.output_maximum = tuning->ceiling_percent;
    parameters.setpoint_rate = tuning->plant.setpoint_rate;
    return parameters;
}

int identification_tuning_identifier(const identification_tuning_t *tuning,
                                     char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0U) {
        return -1;
    }
    int written;
    if (tuning == NULL || !tuning->configured) {
        written = snprintf(buffer, buffer_size, "ident-unconfigured");
    } else {
        /* La térmica se sintoniza en segundos enteros y la presión por debajo
         * del segundo: redondear 0,5 s a «l1» haría que dos pruebas distintas
         * dijeran lo mismo en el registro. Los segundos enteros conservan el
         * formato que ya tienen los registros de la etapa 7 térmica. */
        const long lambda_ms = lroundf(tuning->lambda_s * 1000.0F);
        written = lambda_ms % 1000L == 0L
                      ? snprintf(buffer, buffer_size, "ident-r%u-l%ld-i%u",
                                 (unsigned)tuning->revision, lambda_ms / 1000L,
                                 tuning->integral_enabled ? 1U : 0U)
                      : snprintf(buffer, buffer_size, "ident-r%u-l%ldms-i%u",
                                 (unsigned)tuning->revision, lambda_ms,
                                 tuning->integral_enabled ? 1U : 0U);
    }
    return written >= 0 && (size_t)written < buffer_size ? written : -1;
}

const char *identification_tuning_result_name(identification_tuning_result_t result)
{
    switch (result) {
    case IDENTIFICATION_TUNING_ACCEPTED:
        return "ok";
    case IDENTIFICATION_TUNING_REJECTED_NOT_CONFIGURED:
        return "not-configured";
    case IDENTIFICATION_TUNING_REJECTED_LAMBDA:
        return "lambda-out-of-range";
    case IDENTIFICATION_TUNING_REJECTED_NOT_CONFIRMED:
        return "not-confirmed";
    case IDENTIFICATION_TUNING_REJECTED_COMMAND:
    default:
        return "command";
    }
}
