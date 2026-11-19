/* Verificación en host del lote de sincronización y su confirmación. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sync_transport.h"

static const char *MENSAJE_A = "{\"messageId\":\"extraction-sim-0001\",\"payload\":{}}";
static const char *MENSAJE_B = "{\"messageId\":\"extraction-sim-0002\",\"payload\":{}}";

static void test_body_wraps_the_messages_without_reserializing_them(void)
{
    const char *mensajes[] = {MENSAJE_A, MENSAJE_B};
    char buffer[512];
    size_t written = 0U;
    assert(sync_body_build("esp32s3-prototype-01", mensajes, 2U, buffer, sizeof(buffer),
                           &written) == SYNC_BODY_OK);
    assert(written == strlen(buffer));
    assert(strncmp(buffer, "{\"deviceId\":\"esp32s3-prototype-01\",\"messages\":[", 45) == 0);
    assert(strstr(buffer, MENSAJE_A) != NULL);
    assert(strstr(buffer, MENSAJE_B) != NULL);
    assert(strstr(buffer, "},{") != NULL);          /* separados por coma */
    assert(buffer[written - 2U] == ']' && buffer[written - 1U] == '}');
}

static void test_rejects_an_empty_or_oversized_batch(void)
{
    const char *mensajes[SYNC_MAX_BATCH + 1U];
    char buffer[4096];
    for (size_t i = 0U; i < SYNC_MAX_BATCH + 1U; ++i) {
        mensajes[i] = MENSAJE_A;
    }
    assert(sync_body_build("dev", mensajes, 0U, buffer, sizeof(buffer), NULL) ==
           SYNC_BODY_EMPTY_BATCH);
    assert(sync_body_build("dev", mensajes, SYNC_MAX_BATCH + 1U, buffer, sizeof(buffer),
                           NULL) == SYNC_BODY_EMPTY_BATCH);
    assert(sync_body_build("dev", mensajes, SYNC_MAX_BATCH, buffer, sizeof(buffer), NULL) ==
           SYNC_BODY_OK);
}

static void test_reports_a_small_buffer_instead_of_sending_a_truncated_batch(void)
{
    const char *mensajes[] = {MENSAJE_A, MENSAJE_B};
    char buffer[60];
    assert(sync_body_build("dev", mensajes, 2U, buffer, sizeof(buffer), NULL) ==
           SYNC_BODY_BUFFER_TOO_SMALL);
}

static void test_only_acknowledged_identifiers_are_confirmed(void)
{
    const char *respuesta =
        "{\"schemaVersion\":\"1.0\",\"acknowledged\":[\"extraction-sim-0001\","
        "\"extraction-sim-0002\"],\"inserted\":1,\"duplicates\":1}";
    assert(sync_response_acknowledges(respuesta, "extraction-sim-0001"));
    assert(sync_response_acknowledges(respuesta, "extraction-sim-0002"));
    assert(sync_response_acknowledged_count(respuesta) == 2);
    /* un identificador que no está no se borra de la cola */
    assert(!sync_response_acknowledges(respuesta, "extraction-sim-0003"));
    /* ni uno que solo coincide parcialmente */
    assert(!sync_response_acknowledges(respuesta, "extraction-sim-000"));
    assert(!sync_response_acknowledges(respuesta, "extraction-sim-00011"));
}

static void test_an_unreadable_response_confirms_nothing(void)
{
    /* Ante una respuesta cortada o sin la clave, nada se da por recibido: es
       preferible reenviar un duplicado, que el servicio descarta, a perder un
       registro. */
    assert(!sync_response_acknowledges("{\"inserted\":2}", "extraction-sim-0001"));
    assert(sync_response_acknowledged_count("{\"inserted\":2}") == -1);
    assert(sync_response_acknowledged_count("{\"acknowledged\":[\"a\"") == -1);
    assert(!sync_response_acknowledges("{\"acknowledged\":[\"extrac", "extraction-sim-0001"));
    assert(!sync_response_acknowledges(NULL, "extraction-sim-0001"));
    assert(sync_response_acknowledged_count("") == -1);
}

static void test_an_empty_acknowledgement_is_readable_but_confirms_nothing(void)
{
    const char *respuesta = "{\"acknowledged\":[],\"inserted\":0}";
    assert(sync_response_acknowledged_count(respuesta) == 0);
    assert(!sync_response_acknowledges(respuesta, "extraction-sim-0001"));
}


static void test_the_time_request_carries_no_messages(void)
{
    /* Pedir la hora no es entregar: el cuerpo lleva el dispositivo y una lista
       vacía, que es lo que el contrato admite y lo que distingue esta petición
       de una entrega. */
    char cuerpo[64];
    size_t escritos = 0U;
    assert(sync_time_request_build("esp32s3-prototype-01", cuerpo, sizeof(cuerpo), &escritos) ==
           SYNC_BODY_OK);
    assert(strcmp(cuerpo, "{\"deviceId\":\"esp32s3-prototype-01\",\"messages\":[]}") == 0);
    assert(escritos == strlen(cuerpo));

    /* Sin dispositivo no hay petición, y un espacio insuficiente se declara en
       lugar de enviarse cortado. */
    assert(sync_time_request_build("", cuerpo, sizeof(cuerpo), NULL) == SYNC_BODY_INVALID_ARGUMENT);
    assert(sync_time_request_build(NULL, cuerpo, sizeof(cuerpo), NULL) ==
           SYNC_BODY_INVALID_ARGUMENT);
    assert(sync_time_request_build("esp32s3-prototype-01", cuerpo, 8U, NULL) ==
           SYNC_BODY_BUFFER_TOO_SMALL);
}

static void test_the_service_time_can_date_the_evidence(void)
{
    /* Es la respuesta que devuelve la plataforma: hora en UTC con fracción de
       segundo de seis dígitos, de la que solo se conservan los milisegundos. */
    uint64_t epoch_ms = 0U;
    assert(sync_response_server_time_ms(
        "{\"schemaVersion\":\"1.0\",\"serverTime\":\"2026-09-15T20:24:29.219211Z\","
        "\"acknowledged\":[]}",
        &epoch_ms));
    assert(epoch_ms == UINT64_C(1789503869219));

    /* La fracción es opcional. */
    epoch_ms = 0U;
    assert(sync_response_server_time_ms("{\"serverTime\":\"1970-01-01T00:00:00Z\"}", &epoch_ms));
    assert(epoch_ms == 0U);

    /* Un año bisiesto se cuenta como tal y no como 365 días. */
    epoch_ms = 0U;
    assert(sync_response_server_time_ms("{\"serverTime\":\"2024-02-29T12:00:00Z\"}", &epoch_ms));
    assert(epoch_ms == UINT64_C(1709208000000));
}

static void test_an_unusable_time_is_refused_instead_of_interpreted(void)
{
    /* Fechar mal la evidencia es peor que no fecharla: todo lo que no responde
       exactamente al contrato se rechaza y el dispositivo sigue sin hora. */
    uint64_t epoch_ms = UINT64_C(123);
    assert(!sync_response_server_time_ms("{\"acknowledged\":[]}", &epoch_ms));
    /* Con desfase horario en lugar de UTC. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-09-15T20:24:29+02:00\"}",
                                         &epoch_ms));
    /* Sin la marca de zona. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-09-15T20:24:29\"}", &epoch_ms));
    /* Un 30 de febrero no existe aunque el formato encaje. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-02-30T00:00:00Z\"}", &epoch_ms));
    /* Y tampoco el 29 de febrero de un año que no es bisiesto. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-02-29T00:00:00Z\"}", &epoch_ms));
    /* Partes fuera de rango. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-13-01T00:00:00Z\"}", &epoch_ms));
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-09-15T24:00:00Z\"}", &epoch_ms));
    /* Una fecha anterior a la época no puede proceder de un servicio con hora. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"1969-12-31T23:59:59Z\"}", &epoch_ms));
    /* Un punto decimal sin dígitos detrás. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-09-15T20:24:29.Z\"}", &epoch_ms));
    /* Respuesta cortada. */
    assert(!sync_response_server_time_ms("{\"serverTime\":\"2026-09-15T20:24", &epoch_ms));
    assert(!sync_response_server_time_ms(NULL, &epoch_ms));
    /* Ninguna de las anteriores tocó el destino. */
    assert(epoch_ms == UINT64_C(123));
}

static void test_a_clock_far_from_the_service_is_corrected(void)
{
    const uint64_t service = UINT64_C(1792989686000); /* 2026-10-26T14:41:26Z */
    const uint64_t tolerance = UINT64_C(120000);
    /* El caso del 26-oct: 9 h 55 min atrás. */
    assert(sync_clock_needs_correction(service - UINT64_C(35700000), service, tolerance));
    /* Adelantado también. */
    assert(sync_clock_needs_correction(service + UINT64_C(600000), service, tolerance));
    /* El viaje de la petición no es una hora mala. */
    assert(!sync_clock_needs_correction(service - UINT64_C(1500), service, tolerance));
    assert(!sync_clock_needs_correction(service + tolerance, service, tolerance));
    /* Sin hora propia, siempre. */
    assert(sync_clock_needs_correction(0U, service, tolerance));
}

int main(void)
{
    test_body_wraps_the_messages_without_reserializing_them();
    test_rejects_an_empty_or_oversized_batch();
    test_reports_a_small_buffer_instead_of_sending_a_truncated_batch();
    test_only_acknowledged_identifiers_are_confirmed();
    test_an_unreadable_response_confirms_nothing();
    test_an_empty_acknowledgement_is_readable_but_confirms_nothing();
    test_the_time_request_carries_no_messages();
    test_the_service_time_can_date_the_evidence();
    test_an_unusable_time_is_refused_instead_of_interpreted();
    test_a_clock_far_from_the_service_is_corrected();
    printf("PASS: lote de sincronización, confirmación selectiva y hora del servicio\n");
    return 0;
}
