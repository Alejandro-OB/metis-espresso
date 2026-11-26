/*
 * Pruebas de la decisión de actualizar el firmware por Wi-Fi y de la lectura
 * del manifiesto (ADR-021).
 *
 * Lo que importa es lo que no debe pasar: actualizar con la máquina encendida o
 * con una salida admitida, seguir descargando cuando alguien la enciende,
 * aceptar un manifiesto a medias o de otro binario, y dar por buena una imagen
 * cuya huella no es la publicada.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "firmware_update.h"

static const char PROJECT[] = "cafetera_esp32s3_diagnostic";
static const char DIGEST_HEX[] =
    "00112233445566778899aabbccddeeff00112233445566778899AABBCCDDEEFF";
static const uint32_t PARTITION_BYTES = 0x300000U;

static void digest_bytes(uint8_t out[FIRMWARE_UPDATE_DIGEST_BYTES])
{
    for (size_t index = 0U; index < FIRMWARE_UPDATE_DIGEST_BYTES; index++) {
        out[index] = (uint8_t)(((index % 16U) << 4) | (index % 16U));
    }
}

static firmware_update_conditions_t ready(void)
{
    return (firmware_update_conditions_t){
        .state = OP_STATE_SAFE_INACTIVE,
        .network_ready = true,
        .source_configured = true,
    };
}

/* Apagada, sin salidas admitidas, con red y destino: se admite. */
static void test_allowed_only_when_everything_holds(void)
{
    const firmware_update_conditions_t conditions = ready();
    assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_ALLOWED);
}

/* Cualquier estado que no sea apagada se rechaza, alarma incluida: el gesto
 * pide la máquina en reposo, no en avería. */
static void test_refused_outside_safe_inactive(void)
{
    for (int state = 1; state <= (int)OP_STATE_ALARM; state++) {
        firmware_update_conditions_t conditions = ready();
        conditions.state = (operational_state_t)state;
        assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_REFUSED_STATE);
    }
}

/* Si el guardián admite una salida, manda el guardián aunque el estado diga
 * apagada. */
static void test_refused_when_the_guard_admits_an_output(void)
{
    firmware_update_conditions_t conditions = ready();
    conditions.pump_allowed = true;
    assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_REFUSED_PUMP);
    conditions = ready();
    conditions.heater_allowed = true;
    assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_REFUSED_HEATER);
    conditions = ready();
    conditions.identification_active = true;
    assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_REFUSED_IDENTIFICATION);
}

static void test_refused_without_network_source_or_twice(void)
{
    firmware_update_conditions_t conditions = ready();
    conditions.network_ready = false;
    assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_REFUSED_NO_NETWORK);
    conditions = ready();
    conditions.source_configured = false;
    assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_REFUSED_NO_SOURCE);
    conditions = ready();
    conditions.in_progress = true;
    assert(firmware_update_decide(&conditions) == FIRMWARE_UPDATE_REFUSED_BUSY);
}

/* Una estructura puesta a cero no autoriza nada, y tampoco un puntero nulo. */
static void test_zeroed_does_not_authorize(void)
{
    const firmware_update_conditions_t zeroed = {0};
    assert(firmware_update_decide(&zeroed) != FIRMWARE_UPDATE_ALLOWED);
    assert(firmware_update_decide(NULL) == FIRMWARE_UPDATE_UNDECIDED);
    assert(FIRMWARE_UPDATE_UNDECIDED == 0);
    assert(FIRMWARE_MANIFEST_UNREAD == 0);
}

/* Encenderla durante la descarga la abandona; la alarma no. */
static void test_continue_only_while_off_or_in_alarm(void)
{
    assert(firmware_update_may_continue(OP_STATE_SAFE_INACTIVE));
    assert(firmware_update_may_continue(OP_STATE_ALARM));
    assert(!firmware_update_may_continue(OP_STATE_COFFEE_HEATING));
    assert(!firmware_update_may_continue(OP_STATE_READY));
    assert(!firmware_update_may_continue(OP_STATE_EXTRACTION));
    assert(!firmware_update_may_continue(OP_STATE_MANUAL_PURGE));
}

static void manifest_text(char *out, size_t capacity, const char *project, const char *size,
                          const char *digest)
{
    const int written = snprintf(out, capacity,
                                 "{\"project\": \"%s\", \"version\":\"v9-12-gabc\",\n"
                                 " \"size\":%s,\"imageSha256\":\"%s\"}",
                                 project, size, digest);
    assert(written > 0 && (size_t)written < capacity);
}

static void test_manifest_is_read_whole(void)
{
    char json[256];
    manifest_text(json, sizeof(json), PROJECT, "1022656", DIGEST_HEX);
    firmware_manifest_t manifest = {0};
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_OK);
    assert(strcmp(manifest.project, PROJECT) == 0);
    assert(strcmp(manifest.version, "v9-12-gabc") == 0);
    assert(manifest.size_bytes == 1022656U);
    uint8_t expected[FIRMWARE_UPDATE_DIGEST_BYTES];
    digest_bytes(expected);
    assert(firmware_manifest_digest_matches(&manifest, expected));
}

/* Un campo ausente, vacío o con otra forma invalida el manifiesto entero. */
static void test_incomplete_manifest_is_refused(void)
{
    firmware_manifest_t manifest = {0};
    char json[256];
    const char *const broken[] = {
        "{\"project\":\"cafetera_esp32s3_diagnostic\",\"version\":\"v1\",\"size\":10}",
        "{\"project\":\"\",\"version\":\"v1\",\"size\":10,\"imageSha256\":\"00\"}",
        "{\"project\":\"a\\\"b\",\"version\":\"v1\",\"size\":10,\"imageSha256\":\"00\"}",
        "no es json",
        "",
    };
    for (size_t index = 0U; index < sizeof(broken) / sizeof(broken[0]); index++) {
        assert(firmware_manifest_parse(broken[index], PROJECT, NULL, PARTITION_BYTES,
                                       &manifest) == FIRMWARE_MANIFEST_MALFORMED);
    }
    manifest_text(json, sizeof(json), PROJECT, "0", DIGEST_HEX);
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_MALFORMED);
    manifest_text(json, sizeof(json), PROJECT, "\"1022656\"", DIGEST_HEX);
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_MALFORMED);
    manifest_text(json, sizeof(json), PROJECT, "99999999999", DIGEST_HEX);
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_MALFORMED);
    /* Una huella corta, larga o con caracteres que no son hexadecimales. */
    manifest_text(json, sizeof(json), PROJECT, "10", "00112233");
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_MALFORMED);
    manifest_text(json, sizeof(json), PROJECT, "10",
                  "zz112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_MALFORMED);
    assert(firmware_manifest_parse(NULL, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_UNREAD);
}

static void test_manifest_must_fit_and_belong_here(void)
{
    char json[256];
    firmware_manifest_t manifest = {0};
    manifest_text(json, sizeof(json), "otro_proyecto", "1022656", DIGEST_HEX);
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_WRONG_PROJECT);
    manifest_text(json, sizeof(json), PROJECT, "3145729", DIGEST_HEX);
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_TOO_LARGE);
    manifest_text(json, sizeof(json), PROJECT, "3145728", DIGEST_HEX);
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_OK);
}

/* Lo que ya corre no se vuelve a escribir. */
static void test_same_image_is_not_installed_again(void)
{
    char json[256];
    manifest_text(json, sizeof(json), PROJECT, "1022656", DIGEST_HEX);
    uint8_t running[FIRMWARE_UPDATE_DIGEST_BYTES];
    digest_bytes(running);
    firmware_manifest_t manifest = {0};
    assert(firmware_manifest_parse(json, PROJECT, running, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_ALREADY_RUNNING);
    running[0] ^= 1U;
    assert(firmware_manifest_parse(json, PROJECT, running, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_OK);
}

/* Un solo bit distinto en la imagen escrita basta para no arrancarla. */
static void test_written_digest_must_match_every_byte(void)
{
    char json[256];
    manifest_text(json, sizeof(json), PROJECT, "1022656", DIGEST_HEX);
    firmware_manifest_t manifest = {0};
    assert(firmware_manifest_parse(json, PROJECT, NULL, PARTITION_BYTES, &manifest) ==
           FIRMWARE_MANIFEST_OK);
    uint8_t written[FIRMWARE_UPDATE_DIGEST_BYTES];
    for (size_t index = 0U; index < FIRMWARE_UPDATE_DIGEST_BYTES; index++) {
        digest_bytes(written);
        written[index] ^= 0x80U;
        assert(!firmware_manifest_digest_matches(&manifest, written));
    }
    assert(!firmware_manifest_digest_matches(NULL, written));
    assert(!firmware_manifest_digest_matches(&manifest, NULL));
}

int main(void)
{
    test_allowed_only_when_everything_holds();
    test_refused_outside_safe_inactive();
    test_refused_when_the_guard_admits_an_output();
    test_refused_without_network_source_or_twice();
    test_zeroed_does_not_authorize();
    test_continue_only_while_off_or_in_alarm();
    test_manifest_is_read_whole();
    test_incomplete_manifest_is_refused();
    test_manifest_must_fit_and_belong_here();
    test_same_image_is_not_installed_again();
    test_written_digest_must_match_every_byte();
    printf("PASS: actualización del firmware\n");
    return 0;
}
