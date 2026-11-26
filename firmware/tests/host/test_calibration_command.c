#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "calibration_command.h"

/*
 * Una calibración de prueba, plausible y distinta de la nominal en cada campo.
 * No está medida: es la que ejercita el formato.
 */
static const char VALID_SET[] =
    "K,set,prueba-anfitrion_1.0,3,3.297,9987.5,98760,25,3971.2,4.4871,-1.512,431.25\n";

static void test_other_lines_are_not_mine(void)
{
    assert(calibration_command_parse(NULL).kind == CALIBRATION_COMMAND_NONE);
    assert(calibration_command_parse("C,show\n").kind == CALIBRATION_COMMAND_NONE);
    assert(calibration_command_parse("V,1\n").kind == CALIBRATION_COMMAND_NONE);
    assert(calibration_command_parse("k,show\n").kind == CALIBRATION_COMMAND_NONE);
    /* El cero no fija nada. */
    const calibration_command_t empty = {0};
    assert(empty.kind == CALIBRATION_COMMAND_NONE);
    assert(empty.reject == CALIBRATION_REJECT_NONE);
}

static void test_simple_orders(void)
{
    assert(calibration_command_parse("K,show\r\n").kind == CALIBRATION_COMMAND_SHOW);
    assert(calibration_command_parse("K,reset\n").kind == CALIBRATION_COMMAND_RESET);
    assert(calibration_command_parse("K,tare").kind == CALIBRATION_COMMAND_TARE);
    const calibration_command_t unknown = calibration_command_parse("K,showall\n");
    assert(unknown.kind == CALIBRATION_COMMAND_INVALID);
    assert(unknown.reject == CALIBRATION_REJECT_SYNTAX);
}

static void test_valid_set(void)
{
    const calibration_command_t command = calibration_command_parse(VALID_SET);
    assert(command.kind == CALIBRATION_COMMAND_SET);
    assert(strcmp(command.calibration.id, "prueba-anfitrion_1.0") == 0);
    assert(command.calibration.version == 3U);
    assert(command.calibration.temperature.supply_v == 3.297F);
    assert(command.calibration.temperature.series_ohms == 9987.5F);
    assert(command.calibration.temperature.reference_ohms == 98760.0F);
    assert(command.calibration.temperature.reference_c == 25.0F);
    assert(command.calibration.temperature.beta_k == 3971.2F);
    assert(command.calibration.pressure.gain_bar_per_v == 4.4871F);
    assert(command.calibration.pressure.offset_bar == -1.512F);
    assert(command.calibration.scale_counts_per_gram == 431.25F);
    assert(sensor_calibration_is_valid(&command.calibration));

    /* La báscula sin calibrar se envía como cero y es válida. */
    const calibration_command_t uncalibrated = calibration_command_parse(
        "K,set,sin-bascula,1,3.3,10000,100000,25,3950,4.5,-1.5,0\n");
    assert(uncalibrated.kind == CALIBRATION_COMMAND_SET);
}

static void assert_rejected(const char *line, calibration_reject_t reject)
{
    const calibration_command_t command = calibration_command_parse(line);
    assert(command.kind == CALIBRATION_COMMAND_INVALID);
    assert(command.reject == reject);
    /* Rechazada no lleva nada que pudiera guardarse por descuido. */
    assert(command.calibration.id[0] == '\0');
}

/* Identificador y versión obligatorios (ADR-020 § 4.1). */
static void test_identifier_and_version_are_required(void)
{
    assert_rejected("K,set,,3,3.3,10000,100000,25,3950,4.5,-1.5,0\n", CALIBRATION_REJECT_SYNTAX);
    assert_rejected("K,set,con espacio,3,3.3,10000,100000,25,3950,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_IDENTIFIER);
    assert_rejected("K,set,nominal-v1,3,3.3,10000,100000,25,3950,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_IDENTIFIER);
    assert_rejected("K,set,a234567890123456789012345678901x,3,3.3,10000,100000,25,3950,4.5,"
                    "-1.5,0\n",
                    CALIBRATION_REJECT_SYNTAX);
    assert_rejected("K,set,ok,0,3.3,10000,100000,25,3950,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_VERSION);
    assert_rejected("K,set,ok,-1,3.3,10000,100000,25,3950,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_VERSION);
    assert_rejected("K,set,ok,v2,3.3,10000,100000,25,3950,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_VERSION);
}

/* Campos de menos, de más, vacíos o que no son números: nada se completa. */
static void test_malformed_numbers(void)
{
    assert_rejected("K,set,ok,1,3.3,10000,100000,25,3950,4.5,-1.5\n", CALIBRATION_REJECT_SYNTAX);
    assert_rejected("K,set,ok,1,3.3,10000,100000,25,3950,4.5,-1.5,0,7\n",
                    CALIBRATION_REJECT_SYNTAX);
    assert_rejected("K,set,ok,1,3.3,,100000,25,3950,4.5,-1.5,0\n", CALIBRATION_REJECT_SYNTAX);
    assert_rejected("K,set,ok,1,3.3,10k,100000,25,3950,4.5,-1.5,0\n", CALIBRATION_REJECT_NUMBER);
    assert_rejected("K,set,ok,1,3.3,10000,100000,25,3950,4.5,-1.5,1e99\n",
                    CALIBRATION_REJECT_NUMBER);
}

/* Números, pero imposibles: se rechazan sin escribir, con el criterio de guardado. */
static void test_implausible_values(void)
{
    assert_rejected("K,set,ok,1,3.3,10000,100000,25,395,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_IMPLAUSIBLE);
    assert_rejected("K,set,ok,1,3.3,10000,100000,25,3950,0,-1.5,0\n",
                    CALIBRATION_REJECT_IMPLAUSIBLE);
    assert_rejected("K,set,ok,1,3.3,10000,100000,25,3950,4.5,-1.5,-431\n",
                    CALIBRATION_REJECT_IMPLAUSIBLE);
    assert_rejected("K,set,ok,1,3.3,10000,100000,25,nan,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_IMPLAUSIBLE);
    assert_rejected("K,set,ok,1,inf,10000,100000,25,3950,4.5,-1.5,0\n",
                    CALIBRATION_REJECT_IMPLAUSIBLE);
}

/*
 * Lo que el dispositivo escribe se vuelve a leer idéntico. Es la base de la
 * confirmación: el computador compara lo guardado con lo que envió.
 */
static void test_round_trip(void)
{
    const calibration_command_t sent = calibration_command_parse(VALID_SET);
    char fields[256];
    assert(calibration_command_format(&sent.calibration, fields, sizeof(fields)) > 0);
    char line[300];
    snprintf(line, sizeof(line), "K,set,%s\n", fields);
    const calibration_command_t read_back = calibration_command_parse(line);
    assert(read_back.kind == CALIBRATION_COMMAND_SET);
    assert(memcmp(&read_back.calibration, &sent.calibration, sizeof(sent.calibration)) == 0);

    /* La nominal también se escribe, aunque no se pueda enviar con su nombre. */
    const sensor_calibration_t nominal = sensor_calibration_nominal();
    assert(sensor_calibration_is_valid(&nominal));
    assert(calibration_command_format(&nominal, fields, sizeof(fields)) > 0);
    assert(strncmp(fields, "nominal-v1,1,", 13U) == 0);

    /* Sin sitio no deja nada a medias. */
    char tiny[8];
    assert(calibration_command_format(&nominal, tiny, sizeof(tiny)) == -1);
    assert(tiny[0] == '\0');
}

int main(void)
{
    test_other_lines_are_not_mine();
    test_simple_orders();
    test_valid_set();
    test_identifier_and_version_are_required();
    test_malformed_numbers();
    test_implausible_values();
    test_round_trip();
    puts("PASS: orden de calibración por el puerto de servicio");
    return 0;
}
