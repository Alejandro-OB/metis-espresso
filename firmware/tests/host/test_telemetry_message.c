/* Verificación en host de la serialización de una muestra de telemetría. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "telemetry_message.h"

#define BUFFER_BYTES TELEMETRY_MESSAGE_JSON_BYTES

static telemetry_message_context_t measured_context(void)
{
    telemetry_message_context_t context = {
        .device_id = "espresso-lab-01",
        .sequence = 7U,
        .epoch_ms = UINT64_C(1789999200000),
        .preparation_id = "sim-00000042",
        .elapsed_ms = 12500U,
        .state = OP_STATE_EXTRACTION,
        .target_temperature_c = 93.0F,
        .target_pressure_bar = 9.0F,
        .heater_on = true,
        .pump_duty_percent = 80U,
        .virtual_actuation = false,
    };
    context.process.temperature_c = 92.4F;
    context.process.pressure_bar = 9.1F;
    context.process.temperature_quality = SIGNAL_VALID;
    context.process.pressure_quality = SIGNAL_VALID;
    context.process.source = PROCESS_SOURCE_MEASURED;
    return context;
}

int main(int argc, char **argv)
{
    char buffer[BUFFER_BYTES];
    char message_id[TELEMETRY_MESSAGE_ID_LENGTH + 1U];

    /* Una muestra medida y completa se serializa según el contrato. */
    telemetry_message_context_t context = measured_context();
    assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == TELEMETRY_MESSAGE_OK);
    assert(strstr(buffer, "\"messageType\":\"telemetry.sample\"") != NULL);
    assert(strstr(buffer, "\"source\":\"measured\"") != NULL);
    assert(strstr(buffer, "\"state\":\"extraction\"") != NULL);
    assert(strstr(buffer, "\"preparationId\":\"sim-00000042\"") != NULL);
    assert(strstr(buffer, "\"quality\":\"valid\"") != NULL);
    assert(strstr(buffer, "\"virtual\":false") != NULL);
    /* Sin contador del lazo no se emite: un cero diría que no calentó. */
    assert(strstr(buffer, "heaterAppliedMs") == NULL);

    /* Con el contador, la conducción acumulada viaja entera, sin redondeo. */
    context = measured_context();
    context.heater_applied_known = true;
    context.heater_applied_ms = UINT64_C(5000000020);
    assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == TELEMETRY_MESSAGE_OK);
    assert(strstr(buffer, "\"heaterOn\":true,\"heaterAppliedMs\":5000000020,\"pumpDutyPercent\":80")
           != NULL);

    /* Sin origen declarado no se emite nada: el contrato solo admite medición o
     * simulación, y elegir una por omisión afirmaría algo que no consta. */
    context = measured_context();
    context.process.source = PROCESS_SOURCE_NONE;
    assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == TELEMETRY_MESSAGE_SOURCE_UNAVAILABLE);

    /* Una lectura simulada se declara simulada, no válida. */
    context = measured_context();
    context.process.source = PROCESS_SOURCE_SIMULATED;
    assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == TELEMETRY_MESSAGE_OK);
    assert(strstr(buffer, "\"source\":\"simulation\"") != NULL);
    assert(strstr(buffer, "\"quality\":\"simulated\"") != NULL);

    /* Un canal no utilizable viaja nulo en lugar de transmitir un número en el
     * que no se confía. */
    context = measured_context();
    context.process.pressure_quality = SIGNAL_OPEN_CIRCUIT;
    assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == TELEMETRY_MESSAGE_OK);
    assert(strstr(buffer, "\"value\":null,\"unit\":\"bar\",\"quality\":\"invalid\"") != NULL);

    /* Sin hora de pared fiable no se inventa una fecha. */
    context = measured_context();
    context.epoch_ms = 1000U;
    assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == TELEMETRY_MESSAGE_TIME_BASE_UNAVAILABLE);

    /* Sin preparación en curso el contrato admite el identificador nulo. */
    context = measured_context();
    context.preparation_id = NULL;
    context.state = OP_STATE_READY;
    assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                   sizeof(message_id)) == TELEMETRY_MESSAGE_OK);
    assert(strstr(buffer, "\"preparationId\":null") != NULL);

    if (argc > 1) {
        context = measured_context();
        assert(telemetry_message_build(&context, buffer, sizeof(buffer), message_id,
                                       sizeof(message_id)) == TELEMETRY_MESSAGE_OK);
        FILE *file = fopen(argv[1], "w");
        assert(file != NULL);
        fputs(buffer, file);
        fclose(file);
    }

    printf("telemetry_message: 6 casos correctos\n");
    return 0;
}
