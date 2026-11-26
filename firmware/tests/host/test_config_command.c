/* Verificación en host de la lectura de órdenes de configuración del enlace. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "config_command.h"

static void test_a_line_that_is_not_a_command_is_left_alone(void)
{
    /* El puerto de servicio lleva varios formatos. Una línea ajena no puede
       interpretarse como una orden a medias: se devuelve sin tocar nada, que es
       lo que significa el cero del enumerado. */
    assert(config_command_parse("V,3\n").kind == CONFIG_COMMAND_NONE);
    assert(config_command_parse("I,1000,93.0,9.0,1\n").kind == CONFIG_COMMAND_NONE);
    assert(config_command_parse("").kind == CONFIG_COMMAND_NONE);
    assert(config_command_parse(NULL).kind == CONFIG_COMMAND_NONE);
    /* «C» sola tampoco: el prefijo es «C,». */
    assert(config_command_parse("Cualquier cosa\n").kind == CONFIG_COMMAND_NONE);
}

static void test_the_address_is_read_whole(void)
{
    config_command_t orden = config_command_parse("C,url,http://192.168.1.106:8080/api/v1/sync\n");
    assert(orden.kind == CONFIG_COMMAND_SET_URL);
    assert(strcmp(orden.value, "http://192.168.1.106:8080/api/v1/sync") == 0);

    /* Una dirección puede contener comas; partirla por la primera dejaría una
       dirección truncada con toda la apariencia de estar bien. */
    orden = config_command_parse("C,url,https://equipo.local/api/v1/sync?a=1,2\n");
    assert(orden.kind == CONFIG_COMMAND_SET_URL);
    assert(strcmp(orden.value, "https://equipo.local/api/v1/sync?a=1,2") == 0);

    /* El salto de línea y los espacios finales no forman parte del valor. */
    orden = config_command_parse("C,url,http://equipo.local/sync  \r\n");
    assert(orden.kind == CONFIG_COMMAND_SET_URL);
    assert(strcmp(orden.value, "http://equipo.local/sync") == 0);
}

static void test_something_that_is_not_an_address_is_refused(void)
{
    /* Sin protocolo no hay a dónde enviar. Aceptarlo dejaría al dispositivo sin
       destino creyendo que tiene uno, y el síntoma aparecería mucho después
       como evidencia que no llega. */
    config_command_t orden = config_command_parse("C,url,192.168.1.106:8080/api/v1/sync\n");
    assert(orden.kind == CONFIG_COMMAND_INVALID);
    assert(orden.value[0] == '\0');

    assert(config_command_parse("C,url,\n").kind == CONFIG_COMMAND_INVALID);
    assert(config_command_parse("C,url,   \n").kind == CONFIG_COMMAND_INVALID);
    assert(config_command_parse("C,url,ftp://equipo.local/sync\n").kind == CONFIG_COMMAND_INVALID);

    /* Una dirección que no cabe donde va a guardarse se rechaza entera. */
    char larga[DEVICE_CONFIG_URL_BYTES + 32U];
    int escrito = snprintf(larga, sizeof(larga), "C,url,http://");
    memset(larga + escrito, 'a', sizeof(larga) - (size_t)escrito - 1U);
    larga[sizeof(larga) - 1U] = '\0';
    assert(config_command_parse(larga).kind == CONFIG_COMMAND_INVALID);

    assert(config_command_url_is_usable("http://equipo.local/sync"));
    assert(!config_command_url_is_usable(""));
    assert(!config_command_url_is_usable(NULL));
}

static void test_the_credential_is_measured_against_its_own_room(void)
{
    config_command_t orden = config_command_parse("C,token,dev-barista-token\n");
    assert(orden.kind == CONFIG_COMMAND_SET_TOKEN);
    assert(strcmp(orden.value, "dev-barista-token") == 0);

    assert(config_command_parse("C,token,\n").kind == CONFIG_COMMAND_INVALID);

    /* Una credencial que no quepa se rechaza en lugar de guardarse truncada: al
       entregar fallaría por un motivo que no se parece a la causa. El hueco de
       la credencial es menor que el de la dirección, de modo que un valor
       intermedio cabe en la estructura y aun así debe rechazarse. */
    char intermedia[DEVICE_CONFIG_URL_BYTES + 16U];
    int escrito = snprintf(intermedia, sizeof(intermedia), "C,token,");
    memset(intermedia + escrito, 'k', DEVICE_CONFIG_TOKEN_BYTES + 4U);
    intermedia[escrito + DEVICE_CONFIG_TOKEN_BYTES + 4U] = '\0';
    config_command_t rechazada = config_command_parse(intermedia);
    assert(rechazada.kind == CONFIG_COMMAND_INVALID);
    assert(rechazada.value[0] == '\0');
}

static void test_the_queries_carry_no_value(void)
{
    config_command_t mostrar = config_command_parse("C,show\n");
    assert(mostrar.kind == CONFIG_COMMAND_SHOW);
    assert(mostrar.value[0] == '\0');

    config_command_t reiniciar = config_command_parse("C,reset\r\n");
    assert(reiniciar.kind == CONFIG_COMMAND_RESET);
    assert(reiniciar.value[0] == '\0');

    /* Una orden desconocida se declara inválida y no se confunde con otra: es
       una orden de configuración mal escrita, no una línea ajena. */
    assert(config_command_parse("C,shows\n").kind == CONFIG_COMMAND_INVALID);
    assert(config_command_parse("C,\n").kind == CONFIG_COMMAND_INVALID);
    assert(config_command_parse("C,borrar todo\n").kind == CONFIG_COMMAND_INVALID);
}

/* El manifiesto del firmware es una dirección más, y se le exige lo mismo; no
   se confunde con la de la plataforma (ADR-021). */
static void test_the_firmware_address_is_its_own_order(void)
{
    config_command_t orden =
        config_command_parse("C,ota,http://192.168.1.50:8070/firmware/manifest.json\r\n");
    assert(orden.kind == CONFIG_COMMAND_SET_UPDATE_URL);
    assert(strcmp(orden.value, "http://192.168.1.50:8070/firmware/manifest.json") == 0);

    config_command_t sin_protocolo = config_command_parse("C,ota,192.168.1.50/manifest.json\n");
    assert(sin_protocolo.kind == CONFIG_COMMAND_INVALID);
    assert(sin_protocolo.value[0] == '\0');
    assert(config_command_parse("C,ota,\n").kind == CONFIG_COMMAND_INVALID);
    assert(config_command_parse("C,ota\n").kind == CONFIG_COMMAND_INVALID);
}

int main(void)
{
    test_a_line_that_is_not_a_command_is_left_alone();
    test_the_address_is_read_whole();
    test_something_that_is_not_an_address_is_refused();
    test_the_credential_is_measured_against_its_own_room();
    test_the_queries_carry_no_value();
    test_the_firmware_address_is_its_own_order();
    printf("PASS: órdenes de configuración del enlace\n");
    return 0;
}
