/* Verificación en host de la composición de la serie medida de una preparación. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "series_message.h"

static extraction_session_t sesion;

/* Registra una sesión con muestras cada `paso_ms`, todas válidas salvo las que
   indique `invalida_cada`. */
static void preparar_sesion(uint32_t muestras, uint32_t paso_ms, uint32_t invalida_cada)
{
    memset(&sesion, 0, sizeof(sesion));
    snprintf(sesion.summary.id, sizeof(sesion.summary.id), "sim-00000007");
    sesion.summary.sample_count = muestras;
    for (uint32_t indice = 0U; indice < muestras; ++indice) {
        const bool invalida = invalida_cada != 0U && (indice % invalida_cada) == 0U;
        sesion.samples[indice].elapsed_ms = indice * paso_ms;
        sesion.samples[indice].observed_temperature_c = 90.0F + (float)indice * 0.01F;
        sesion.samples[indice].observed_pressure_bar = 8.0F + (float)indice * 0.001F;
        sesion.samples[indice].temperature_valid = !invalida;
        sesion.samples[indice].pressure_valid = true;
    }
}

/* Antepone `muestras` previas a pulsar, cada 500 ms y terminando en −500 ms,
   como las deja la sesión al tomar el minuto previo. */
static void anteponer_minuto(uint32_t muestras)
{
    sesion.pre_start_count = muestras;
    for (uint32_t indice = 0U; indice < muestras; ++indice) {
        sesion.pre_start[indice].elapsed_ms =
            -(int32_t)((muestras - indice) * SERIES_MESSAGE_INTERVAL_MS);
        sesion.pre_start[indice].temperature_c = 102.3F;
        sesion.pre_start[indice].pressure_bar = 0.05F;
        sesion.pre_start[indice].temperature_valid = true;
        sesion.pre_start[indice].pressure_valid = true;
    }
}

/* Recorre todos los tramos y devuelve los instantes en orden de emisión. */
static uint32_t instantes_emitidos(const series_message_context_t *ctx, long *instantes,
                                   uint32_t capacidad)
{
    const uint32_t tramos = series_message_chunk_count(&sesion);
    uint32_t vistos = 0U;
    for (uint32_t tramo = 0U; tramo < tramos; ++tramo) {
        char json[SERIES_MESSAGE_JSON_BYTES];
        assert(series_message_build(&sesion, ctx, tramo, json, sizeof(json), NULL, 0U) ==
               SERIES_MESSAGE_OK);
        for (const char *cursor = strstr(json, "\"elapsedMs\":"); cursor != NULL;
             cursor = strstr(cursor + 1, "\"elapsedMs\":")) {
            assert(vistos < capacidad);
            assert(sscanf(cursor, "\"elapsedMs\":%ld", &instantes[vistos]) == 1);
            ++vistos;
        }
    }
    return vistos;
}

static series_message_context_t contexto(bool medida)
{
    const series_message_context_t valor = {
        .device_id = "esp32s3-prototype-01",
        .sequence = 7U,
        .epoch_ms = UINT64_C(1789505462848),
        .measured = medida,
    };
    return valor;
}

static void test_only_the_comparison_grid_is_kept(void)
{
    /* El dispositivo registra cada 100 ms y la comparación se hace sobre una
       rejilla de 500 ms: conservar lo demás ocuparía sitio en la cola sin que
       nada lo compare. 30 s a 100 ms son 300 muestras y 60 conservadas. */
    preparar_sesion(300U, 100U, 0U);
    const uint32_t tramos = series_message_chunk_count(&sesion);
    assert(tramos == (60U + SERIES_MESSAGE_SAMPLES_PER_CHUNK - 1U) / SERIES_MESSAGE_SAMPLES_PER_CHUNK);

    /* Una sesión sin muestras no produce ningún tramo, y eso no es un error:
       una preparación puede terminar antes de la primera muestra. */
    preparar_sesion(0U, 100U, 0U);
    assert(series_message_chunk_count(&sesion) == 0U);
    assert(series_message_chunk_count(NULL) == 0U);
}

static void test_the_chunk_declares_where_it_fits(void)
{
    preparar_sesion(300U, 100U, 0U);
    const uint32_t tramos = series_message_chunk_count(&sesion);
    const series_message_context_t ctx = contexto(true);

    char json[SERIES_MESSAGE_JSON_BYTES];
    char identificador[SERIES_MESSAGE_ID_LENGTH + 1U];
    assert(series_message_build(&sesion, &ctx, 0U, json, sizeof(json), identificador,
                                sizeof(identificador)) == SERIES_MESSAGE_OK);

    assert(strstr(json, "\"messageType\":\"extraction.series\"") != NULL);
    assert(strstr(json, "\"preparationId\":\"sim-00000007\"") != NULL);
    assert(strstr(json, "\"chunkIndex\":0") != NULL);
    assert(strstr(json, "\"intervalMs\":500") != NULL);
    assert(strstr(json, "\"source\":\"measured\"") != NULL);
    /* El identificador incluye el tramo: dos tramos de la misma preparación no
       pueden compartirlo, o el servicio descartaría uno como reenvío del otro. */
    assert(strcmp(identificador, "series-sim-00000007-000") == 0);

    char json_ultimo[SERIES_MESSAGE_JSON_BYTES];
    char id_ultimo[SERIES_MESSAGE_ID_LENGTH + 1U];
    assert(series_message_build(&sesion, &ctx, tramos - 1U, json_ultimo, sizeof(json_ultimo),
                                id_ultimo, sizeof(id_ultimo)) == SERIES_MESSAGE_OK);
    assert(strcmp(identificador, id_ultimo) != 0);

    /* Pedir un tramo que no existe se declara, no se inventa uno vacío. */
    assert(series_message_build(&sesion, &ctx, tramos, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_ABSENT);
}

static void test_the_whole_series_is_covered_once(void)
{
    /* Ningún instante puede aparecer en dos tramos ni faltar: la serie se parte
       para que quepa en la cola, no se recorta. */
    preparar_sesion(300U, 100U, 0U);
    const series_message_context_t ctx = contexto(true);
    const uint32_t tramos = series_message_chunk_count(&sesion);

    uint32_t vistas = 0U;
    uint32_t anterior_ms = 0U;
    bool primera = true;
    for (uint32_t tramo = 0U; tramo < tramos; ++tramo) {
        char json[SERIES_MESSAGE_JSON_BYTES];
        assert(series_message_build(&sesion, &ctx, tramo, json, sizeof(json), NULL, 0U) ==
               SERIES_MESSAGE_OK);
        for (const char *cursor = strstr(json, "\"elapsedMs\":"); cursor != NULL;
             cursor = strstr(cursor + 1, "\"elapsedMs\":")) {
            unsigned long instante = 0UL;
            assert(sscanf(cursor, "\"elapsedMs\":%lu", &instante) == 1);
            if (!primera) {
                /* Estrictamente creciente entre tramos y dentro de cada uno. */
                assert((uint32_t)instante > anterior_ms);
            }
            anterior_ms = (uint32_t)instante;
            primera = false;
            ++vistas;
        }
    }
    assert(vistas == 60U);
    assert(anterior_ms == 29500U);
}

static void test_a_gap_does_not_produce_a_burst(void)
{
    /* Si el registro tiene un hueco, al otro lado se retoma la rejilla en lugar
       de recuperar lo perdido con muestras seguidas, que darían una densidad
       falsa justo donde menos información hay. */
    memset(&sesion, 0, sizeof(sesion));
    snprintf(sesion.summary.id, sizeof(sesion.summary.id), "sim-00000008");
    const uint32_t instantes[] = {0U, 100U, 200U, 10000U, 10100U, 10200U, 10600U};
    sesion.summary.sample_count = (uint32_t)(sizeof(instantes) / sizeof(instantes[0]));
    for (uint32_t indice = 0U; indice < sesion.summary.sample_count; ++indice) {
        sesion.samples[indice].elapsed_ms = instantes[indice];
        sesion.samples[indice].observed_temperature_c = 92.0F;
        sesion.samples[indice].observed_pressure_bar = 9.0F;
        sesion.samples[indice].temperature_valid = true;
        sesion.samples[indice].pressure_valid = true;
    }

    const series_message_context_t ctx = contexto(true);
    char json[SERIES_MESSAGE_JSON_BYTES];
    assert(series_message_build(&sesion, &ctx, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_OK);
    /* Se conservan 0, el primero tras el hueco y el siguiente de la rejilla. */
    assert(strstr(json, "\"elapsedMs\":0,") != NULL);
    assert(strstr(json, "\"elapsedMs\":10000,") != NULL);
    assert(strstr(json, "\"elapsedMs\":10600,") != NULL);
    /* 10100 y 10200 caen dentro del mismo intervalo que 10000 y no se emiten. */
    assert(strstr(json, "\"elapsedMs\":10100,") == NULL);
    assert(strstr(json, "\"elapsedMs\":10200,") == NULL);
}

static void test_an_unusable_channel_is_marked_not_replaced(void)
{
    /* Una medida no apta se marca y su valor viaja ausente. Sustituirla por la
       anterior o por cero afirmaría una medida que nadie tomó. */
    preparar_sesion(20U, 500U, 1U);  /* todas las temperaturas inválidas */
    const series_message_context_t ctx = contexto(true);
    char json[SERIES_MESSAGE_JSON_BYTES];
    assert(series_message_build(&sesion, &ctx, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_OK);
    assert(strstr(json, "\"temperature\":{\"value\":null,\"quality\":\"invalid\"}") != NULL);
    assert(strstr(json, "\"quality\":\"valid\"") != NULL);  /* la presión sí lo era */

    /* Un valor que no es finito tampoco se emite como número. */
    preparar_sesion(4U, 500U, 0U);
    sesion.samples[0].observed_temperature_c = (float)NAN;
    assert(series_message_build(&sesion, &ctx, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_OK);
    assert(strstr(json, "\"value\":nan") == NULL);
    assert(strstr(json, "\"value\":null") != NULL);
}

static void test_a_series_that_was_not_measured_says_so(void)
{
    /* Sin sensores la serie procede de la planta virtual, y presentarla como
       medición metería simulación en la evidencia física. */
    preparar_sesion(20U, 500U, 0U);
    const series_message_context_t ctx = contexto(false);
    char json[SERIES_MESSAGE_JSON_BYTES];
    assert(series_message_build(&sesion, &ctx, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_OK);
    assert(strstr(json, "\"source\":\"simulation\"") != NULL);
    assert(strstr(json, "\"quality\":\"simulated\"") != NULL);
    assert(strstr(json, "\"quality\":\"valid\"") == NULL);
}

static void test_without_time_or_room_nothing_is_emitted(void)
{
    preparar_sesion(20U, 500U, 0U);
    char json[SERIES_MESSAGE_JSON_BYTES];

    /* El contrato exige instantes reales: sin hora no se compone el tramo. */
    series_message_context_t sin_hora = contexto(true);
    sin_hora.epoch_ms = 0U;
    assert(series_message_build(&sesion, &sin_hora, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_TIME_BASE_UNAVAILABLE);

    const series_message_context_t ctx = contexto(true);
    assert(series_message_build(NULL, &ctx, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_INVALID_ARGUMENT);
    assert(series_message_build(&sesion, NULL, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_INVALID_ARGUMENT);

    series_message_context_t sin_equipo = contexto(true);
    sin_equipo.device_id = "";
    assert(series_message_build(&sesion, &sin_equipo, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_INVALID_ARGUMENT);

    /* Un espacio insuficiente se declara en lugar de emitir un tramo cortado,
       que sería JSON inválido y se perdería entero al validarlo. */
    char estrecho[64];
    assert(series_message_build(&sesion, &ctx, 0U, estrecho, sizeof(estrecho), NULL, 0U) ==
           SERIES_MESSAGE_BUFFER_TOO_SMALL);
}

static void test_the_chunk_fits_in_the_queue_slot(void)
{
    /* Cada tramo debe caber en un hueco de la cola persistente, o no podría
       encolarse y la serie se perdería justo cuando más falta hace. */
    /* Con los valores más largos que el proceso puede dar: temperatura de tres
       cifras durante el vapor y presión de dos. Si el margen se calculara con
       valores cómodos, el tramo se saldría el día que la máquina trabaje de
       verdad. */
    preparar_sesion(300U, 100U, 0U);
    for (uint32_t indice = 0U; indice < 300U; ++indice) {
        sesion.samples[indice].observed_temperature_c = -129.987F;
        sesion.samples[indice].observed_pressure_bar = -12.345F;
    }
    const series_message_context_t ctx = contexto(true);
    const uint32_t tramos = series_message_chunk_count(&sesion);
    for (uint32_t tramo = 0U; tramo < tramos; ++tramo) {
        char json[SERIES_MESSAGE_JSON_BYTES];
        assert(series_message_build(&sesion, &ctx, tramo, json, sizeof(json), NULL, 0U) ==
               SERIES_MESSAGE_OK);
        assert(strlen(json) < SERIES_MESSAGE_JSON_BYTES);
    }
}

static void test_the_minute_before_pressing_comes_first(void)
{
    /* La estabilidad térmica de la § 4.4 se mide en el minuto previo a pulsar:
       esas muestras van delante, con instante negativo, y la serie entera
       sigue siendo estrictamente creciente aunque cruce el cero. */
    preparar_sesion(300U, 100U, 0U);
    anteponer_minuto(120U);
    const series_message_context_t ctx = contexto(true);
    assert(series_message_chunk_count(&sesion) ==
           (120U + 60U + SERIES_MESSAGE_SAMPLES_PER_CHUNK - 1U) / SERIES_MESSAGE_SAMPLES_PER_CHUNK);

    static long instantes[400];
    const uint32_t vistos = instantes_emitidos(&ctx, instantes, 400U);
    assert(vistos == 180U);
    uint32_t negativos = 0U;
    for (uint32_t indice = 0U; indice < vistos; ++indice) {
        if (instantes[indice] < 0L) {
            /* Ningún negativo después de un positivo. */
            assert(negativos == indice);
            ++negativos;
        }
        if (indice > 0U) {
            assert(instantes[indice] > instantes[indice - 1U]);
            /* Sobre la rejilla, también antes de pulsar. */
            if (instantes[indice] < 0L) {
                assert(instantes[indice] - instantes[indice - 1U] ==
                       (long)SERIES_MESSAGE_INTERVAL_MS);
            }
        }
    }
    assert(negativos == 120U);
    assert(instantes[0] == -60000L);
    assert(instantes[120] == 0L);
}

static void test_without_previous_minute_the_series_is_unchanged(void)
{
    /* Sin historia —recién lista, tras un reinicio— no se inventa ningún
       minuto previo: la serie es exactamente la de antes. */
    preparar_sesion(300U, 100U, 0U);
    const series_message_context_t ctx = contexto(true);
    static long instantes[400];
    const uint32_t vistos = instantes_emitidos(&ctx, instantes, 400U);
    assert(vistos == 60U);
    assert(instantes[0] == 0L);

    /* Con historia corta va lo que haya. */
    anteponer_minuto(7U);
    assert(instantes_emitidos(&ctx, instantes, 400U) == 67U);
    assert(instantes[0] == -3500L);
}

static void test_a_preparation_without_samples_keeps_its_minute(void)
{
    /* Una preparación cortada antes de su primera muestra no tiene serie
       propia, pero el minuto previo sigue diciendo cómo estaba la máquina. */
    preparar_sesion(0U, 100U, 0U);
    assert(series_message_chunk_count(&sesion) == 0U);
    anteponer_minuto(25U);
    assert(series_message_chunk_count(&sesion) == 3U);
}

static void test_an_unfit_previous_sample_is_marked(void)
{
    preparar_sesion(20U, 500U, 0U);
    anteponer_minuto(4U);
    sesion.pre_start[1].temperature_valid = false;
    const series_message_context_t ctx = contexto(true);
    char json[SERIES_MESSAGE_JSON_BYTES];
    assert(series_message_build(&sesion, &ctx, 0U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_OK);
    assert(strstr(json, "{\"elapsedMs\":-1500,\"temperature\":{\"value\":null,"
                        "\"quality\":\"invalid\"}") != NULL);
    assert(strstr(json, "{\"elapsedMs\":-2000,\"temperature\":{\"value\":102.300,") != NULL);
}

static void test_the_worst_chunk_fits_in_the_queue_slot(void)
{
    /* El peor tramo posible con el minuto previo: instantes de seis cifras a
       los dos lados del cero (−60000 y más de 100 s de preparación), valores
       de ocho caracteres, la calidad más larga («simulated», con su fuente
       «simulation») y el número de secuencia más largo. Si cabe este, cabe
       cualquiera que la máquina pueda dar. */
    size_t mayor = 0U;
    for (int medida = 0; medida <= 1; ++medida) {
        preparar_sesion(EXTRACTION_MAX_SAMPLES, 100U, 0U);
        for (uint32_t indice = 0U; indice < EXTRACTION_MAX_SAMPLES; ++indice) {
            sesion.samples[indice].observed_temperature_c = -129.987F;
            sesion.samples[indice].observed_pressure_bar = -12.345F;
        }
        anteponer_minuto(PRE_START_HISTORY_CAPACITY);
        for (uint32_t indice = 0U; indice < PRE_START_HISTORY_CAPACITY; ++indice) {
            sesion.pre_start[indice].elapsed_ms = -60000 + (int32_t)indice;
            sesion.pre_start[indice].temperature_c = -129.987F;
            sesion.pre_start[indice].pressure_bar = -12.345F;
        }
        series_message_context_t ctx = contexto(medida != 0);
        ctx.sequence = UINT32_MAX;
        const uint32_t tramos = series_message_chunk_count(&sesion);
        assert(tramos > 1U);
        for (uint32_t tramo = 0U; tramo < tramos; ++tramo) {
            char json[SERIES_MESSAGE_JSON_BYTES];
            assert(series_message_build(&sesion, &ctx, tramo, json, sizeof(json), NULL, 0U) ==
                   SERIES_MESSAGE_OK);
            const size_t largo = strlen(json);
            assert(largo < SERIES_MESSAGE_JSON_BYTES);
            if (largo > mayor) {
                mayor = largo;
            }
        }
    }
    printf("Peor tramo: %zu de %u bytes\n", mayor, (unsigned)SERIES_MESSAGE_JSON_BYTES);
}

/* Escribe un tramo real para validarlo contra el contrato publicado: es lo que
   impide que el firmware y el contrato se separen sin que nadie lo note. */
static void escribir_muestra(const char *ruta)
{
    /* El tramo que cruza el cero, con instantes negativos y positivos: es el
       que obliga al contrato a admitir el minuto previo. */
    preparar_sesion(300U, 100U, 7U);
    anteponer_minuto(15U);
    const series_message_context_t ctx = contexto(true);
    char json[SERIES_MESSAGE_JSON_BYTES];
    assert(series_message_build(&sesion, &ctx, 1U, json, sizeof(json), NULL, 0U) ==
           SERIES_MESSAGE_OK);
    assert(strstr(json, "\"elapsedMs\":-500,") != NULL);
    assert(strstr(json, "\"elapsedMs\":0,") != NULL);
    FILE *archivo = fopen(ruta, "w");
    assert(archivo != NULL);
    fputs(json, archivo);
    fclose(archivo);
}

int main(int argc, char **argv)
{
    test_only_the_comparison_grid_is_kept();
    test_the_chunk_declares_where_it_fits();
    test_the_whole_series_is_covered_once();
    test_a_gap_does_not_produce_a_burst();
    test_an_unusable_channel_is_marked_not_replaced();
    test_a_series_that_was_not_measured_says_so();
    test_without_time_or_room_nothing_is_emitted();
    test_the_chunk_fits_in_the_queue_slot();
    test_the_minute_before_pressing_comes_first();
    test_without_previous_minute_the_series_is_unchanged();
    test_a_preparation_without_samples_keeps_its_minute();
    test_an_unfit_previous_sample_is_marked();
    test_the_worst_chunk_fits_in_the_queue_slot();
    if (argc > 1) {
        escribir_muestra(argv[1]);
    }
    printf("PASS: serie medida de la preparación\n");
    return 0;
}
