/*
 * Verificación en host de la sintonía térmica en caliente (ADR-020 § 4.2).
 *
 * Comprueba propiedades: que la integral empieza apagada, que la salida nunca
 * pide más que el techo de la secuencia, que una `λ` mayor da una respuesta más
 * suave, que una orden rechazada no cambia nada, que sin configurar el PI no
 * pide potencia y que cada cambio aceptado cambia el identificador del registro.
 *
 * La planta es de prueba, con números del orden de la caldera medida, y no
 * describe la máquina.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "identification_tuning.h"

static const control_identified_plant_t PLANT = {
    .identifier = "planta-de-prueba",
    .gain = 27.0F,
    .time_constant_s = 2400.0F,
    .dead_time_s = 3.0F,
    .lambda_s = 30.0F,
    .filter_time_constant_s = 0.5F,
    .setpoint_rate = 0.5F,
};

static identification_tuning_t configured(void)
{
    identification_tuning_t tuning;
    assert(identification_tuning_configure(&tuning, &PLANT, 0.2F, 10.0F));
    return tuning;
}

static identification_tuning_result_t send(identification_tuning_t *tuning, const char *line)
{
    const identification_tuning_command_t command = identification_tuning_parse(line);
    return identification_tuning_apply(tuning, &command);
}

static void test_parses_only_its_own_lines(void)
{
    assert(identification_tuning_parse("K,show").kind == IDENTIFICATION_TUNING_COMMAND_NONE);
    assert(identification_tuning_parse("T,1,2").kind == IDENTIFICATION_TUNING_COMMAND_NONE);
    assert(identification_tuning_parse("A,show\n").kind == IDENTIFICATION_TUNING_COMMAND_SHOW);
    const identification_tuning_command_t lambda = identification_tuning_parse("A,lambda,45\r\n");
    assert(lambda.kind == IDENTIFICATION_TUNING_COMMAND_LAMBDA);
    assert(lambda.lambda_s == 45.0F);
    assert(identification_tuning_parse("A,integral,on").integral_enabled);
    assert(!identification_tuning_parse("A,integral,off").integral_enabled);
    /* Lo que no es exactamente un número o una palabra conocida no se adivina. */
    assert(identification_tuning_parse("A,lambda,30s").kind == IDENTIFICATION_TUNING_COMMAND_INVALID);
    assert(identification_tuning_parse("A,lambda,").kind == IDENTIFICATION_TUNING_COMMAND_INVALID);
    assert(identification_tuning_parse("A,lambda,nan").kind == IDENTIFICATION_TUNING_COMMAND_INVALID);
    assert(identification_tuning_parse("A,integral,yes").kind == IDENTIFICATION_TUNING_COMMAND_INVALID);
    assert(identification_tuning_parse("A,gain,99").kind == IDENTIFICATION_TUNING_COMMAND_INVALID);
}

static void test_starts_proportional_only_and_under_the_ceiling(void)
{
    const identification_tuning_t tuning = configured();
    const pi_parameters_t parameters = identification_tuning_parameters(&tuning);
    assert(parameters.proportional_gain > 0.0F);
    assert(parameters.integral_gain == 0.0F);
    assert(parameters.output_minimum == 0.0F);
    assert(parameters.output_maximum == 10.0F);
    assert(parameters.setpoint_rate == 0.5F);
}

static void test_a_larger_lambda_is_gentler(void)
{
    identification_tuning_t tuning = configured();
    const float aggressive = identification_tuning_parameters(&tuning).proportional_gain;
    assert(send(&tuning, "A,lambda,120") == IDENTIFICATION_TUNING_ACCEPTED);
    assert(identification_tuning_parameters(&tuning).proportional_gain < aggressive);
}

static void test_the_integral_follows_the_order(void)
{
    identification_tuning_t tuning = configured();
    assert(send(&tuning, "A,integral,on") == IDENTIFICATION_TUNING_ACCEPTED);
    const pi_parameters_t on = identification_tuning_parameters(&tuning);
    assert(on.integral_gain > 0.0F);
    assert(send(&tuning, "A,integral,off") == IDENTIFICATION_TUNING_ACCEPTED);
    assert(identification_tuning_parameters(&tuning).integral_gain == 0.0F);
}

static void test_a_rejected_order_changes_nothing(void)
{
    identification_tuning_t tuning = configured();
    const identification_tuning_t before = tuning;
    /* Por debajo del retardo SIMC deja de ser conservador. */
    assert(send(&tuning, "A,lambda,1") == IDENTIFICATION_TUNING_REJECTED_LAMBDA);
    assert(send(&tuning, "A,lambda,99999") == IDENTIFICATION_TUNING_REJECTED_LAMBDA);
    assert(send(&tuning, "A,gain,99") == IDENTIFICATION_TUNING_REJECTED_COMMAND);
    assert(memcmp(&before, &tuning, sizeof(tuning)) == 0);
}

static void test_unconfigured_asks_for_nothing(void)
{
    identification_tuning_t tuning;
    control_identified_plant_t broken = PLANT;
    broken.gain = 0.0F;
    assert(!identification_tuning_configure(&tuning, &broken, 0.2F, 10.0F));
    const pi_parameters_t parameters = identification_tuning_parameters(&tuning);
    assert(parameters.proportional_gain == 0.0F && parameters.output_maximum == 0.0F);
    assert(send(&tuning, "A,lambda,60") == IDENTIFICATION_TUNING_REJECTED_NOT_CONFIGURED);
    /* Un techo por encima del 100 % no se interpreta. */
    assert(!identification_tuning_configure(&tuning, &PLANT, 0.2F, 150.0F));
    /* Ni una λ inicial por debajo del retardo. */
    control_identified_plant_t tight = PLANT;
    tight.lambda_s = 1.0F;
    assert(!identification_tuning_configure(&tuning, &tight, 0.2F, 10.0F));
}

static void test_each_accepted_change_is_visible_in_the_log(void)
{
    identification_tuning_t tuning = configured();
    char first[CONTROL_TUNING_ID_BYTES];
    char second[CONTROL_TUNING_ID_BYTES];
    assert(identification_tuning_identifier(&tuning, first, sizeof(first)) > 0);
    assert(strcmp(first, "ident-r0-l30-i0") == 0);
    assert(send(&tuning, "A,show") == IDENTIFICATION_TUNING_ACCEPTED);
    assert(identification_tuning_identifier(&tuning, second, sizeof(second)) > 0);
    assert(strcmp(first, second) == 0);
    assert(send(&tuning, "A,integral,on") == IDENTIFICATION_TUNING_ACCEPTED);
    assert(identification_tuning_identifier(&tuning, second, sizeof(second)) > 0);
    assert(strcmp(second, "ident-r1-l30-i1") == 0);
    /* Con los valores más largos posibles sigue cabiendo en la columna. */
    tuning.revision = UINT32_MAX;
    tuning.lambda_s = IDENTIFICATION_TUNING_LAMBDA_MAXIMUM_S;
    assert(identification_tuning_identifier(&tuning, second, sizeof(second)) > 0);
    assert(strchr(second, ',') == NULL);
    /* Y el más largo con milisegundos también. */
    tuning.lambda_s = IDENTIFICATION_TUNING_LAMBDA_MAXIMUM_S - 0.001F;
    assert(identification_tuning_identifier(&tuning, second, sizeof(second)) > 0);
}

/* La presión se sintoniza por debajo del segundo, y el registro tiene que
 * distinguir 0,5 s de 1 s. */
static void test_subsecond_lambda_is_told_apart(void)
{
    const control_identified_plant_t pressure = {
        .identifier = "presion-de-prueba",
        .gain = 0.06F,
        .time_constant_s = 0.1F,
        .dead_time_s = 0.1F,
        .lambda_s = 1.0F,
        .filter_time_constant_s = 0.05F,
        .setpoint_rate = 3.0F,
    };
    identification_tuning_t tuning;
    assert(identification_tuning_configure(&tuning, &pressure, 0.02F, 75.0F));
    char text[CONTROL_TUNING_ID_BYTES];
    assert(identification_tuning_identifier(&tuning, text, sizeof(text)) > 0);
    assert(strcmp(text, "ident-r0-l1-i0") == 0);
    assert(send(&tuning, "A,lambda,0.5") == IDENTIFICATION_TUNING_ACCEPTED);
    assert(identification_tuning_identifier(&tuning, text, sizeof(text)) > 0);
    assert(strcmp(text, "ident-r1-l500ms-i0") == 0);
    /* Por debajo del retardo no: SIMC deja de ser conservador. */
    assert(send(&tuning, "A,lambda,0.05") == IDENTIFICATION_TUNING_REJECTED_LAMBDA);
    const pi_parameters_t parameters = identification_tuning_parameters(&tuning);
    assert(parameters.output_maximum == 75.0F);
    assert(parameters.integral_gain == 0.0F);
}

int main(void)
{
    test_parses_only_its_own_lines();
    test_starts_proportional_only_and_under_the_ceiling();
    test_a_larger_lambda_is_gentler();
    test_the_integral_follows_the_order();
    test_a_rejected_order_changes_nothing();
    test_unconfigured_asks_for_nothing();
    test_each_accepted_change_is_visible_in_the_log();
    test_subsecond_lambda_is_told_apart();
    printf("PASS: sintonía en caliente del binario de identificación\n");
    return 0;
}
