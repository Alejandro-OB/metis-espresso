/* Verificación en host de la lectura de una configuración propuesta. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "profile_message.h"

#define ACCEPTED_PROFILE \
    "{\"schemaVersion\":\"1.0\",\"serverTime\":\"2026-09-15T10:00:00Z\"" \
    ",\"acknowledged\":[],\"inserted\":0,\"duplicates\":0" \
    ",\"profileProposals\":[{\"id\":\"profile-lab-01\",\"name\":\"Perfil de laboratorio\"" \
    ",\"definedBy\":null,\"version\":3,\"temperatureSetpointC\":93.0" \
    ",\"temperatureToleranceC\":0.5,\"pressureSetpointBar\":9.0" \
    ",\"pressureToleranceBar\":0.5,\"maximumTimeMs\":40000" \
    ",\"expectedBeverageMassG\":36.0,\"status\":\"accepted\"}]}"

int main(void)
{
    extraction_profile_t profile;

    /* Una propuesta aceptada y completa se lee entera. */
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(ACCEPTED_PROFILE, &profile) == PROFILE_MESSAGE_OK);
    assert(strcmp(profile.id, "profile-lab-01") == 0);
    assert(profile.version == 3U);
    assert(profile.maximum_time_ms == 40000U);
    assert(profile.expected_beverage_mass_defined);

    /* Sin propuestas no hay nada que aplicar, y eso es lo normal. */
    assert(profile_message_parse_first(
               "{\"schemaVersion\":\"1.0\",\"profileProposals\":[]}", &profile) ==
           PROFILE_MESSAGE_ABSENT);

    /* Una configuración que nadie declaró aceptada no se aplica, aunque el
     * resto del objeto esté impecable: el dispositivo no se fía de que la
     * plataforma haya filtrado. */
    char proposed[1024];
    snprintf(proposed, sizeof(proposed), "%s", ACCEPTED_PROFILE);
    char *status = strstr(proposed, "\"accepted\"");
    assert(status != NULL);
    memcpy(status, "\"proposed\"", 10);
    assert(profile_message_parse_first(proposed, &profile) == PROFILE_MESSAGE_NOT_ACCEPTED);

    /* Un campo que falta invalida la configuración entera: media configuración
     * gobernaría temperatura y presión de una máquina real. */
    char incomplete[1024];
    snprintf(incomplete, sizeof(incomplete),
             "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":93.0,\"pressureSetpointBar\":9.0"
             ",\"pressureToleranceBar\":0.5,\"maximumTimeMs\":40000"
             ",\"status\":\"accepted\"}]}");
    assert(profile_message_parse_first(incomplete, &profile) == PROFILE_MESSAGE_MALFORMED);

    /* Un identificador vacío no es un identificador. */
    char empty_id[1024];
    snprintf(empty_id, sizeof(empty_id), "%s", ACCEPTED_PROFILE);
    char *identifier = strstr(empty_id, "\"profile-lab-01\"");
    assert(identifier != NULL);
    /* "profile-lab-01" ocupa de 0 a 15: copiar la comilla de cierre sobre la
     * primera letra deja el identificador vacío. */
    memmove(identifier + 1, identifier + 15, strlen(identifier + 15) + 1U);
    assert(profile_message_parse_first(empty_id, &profile) == PROFILE_MESSAGE_MALFORMED);

    /* La masa esperada es opcional: su ausencia declarada no invalida nada. */
    char without_mass[1024];
    snprintf(without_mass, sizeof(without_mass),
             "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":93.0,\"temperatureToleranceC\":0.5"
             ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
             ",\"maximumTimeMs\":40000,\"expectedBeverageMassG\":null"
             ",\"status\":\"accepted\"}]}");
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(without_mass, &profile) == PROFILE_MESSAGE_OK);
    assert(!profile.expected_beverage_mass_defined);

    /* Una respuesta sin el campo de propuestas no es un error. */
    assert(profile_message_parse_first("{\"acknowledged\":[\"m-1\"]}", &profile) ==
           PROFILE_MESSAGE_ABSENT);

    /* El corte del purgado es opcional: sin él, el perfil no declara nada y
     * rige el valor compilado. Con él, se lee tal cual. */
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(ACCEPTED_PROFILE, &profile) == PROFILE_MESSAGE_OK);
    assert(!profile.purge_maximum_defined);

    char with_purge[1024];
    snprintf(with_purge, sizeof(with_purge),
             "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":93.0,\"temperatureToleranceC\":0.5"
             ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
             ",\"maximumTimeMs\":40000,\"purgeMaximumTimeMs\":%s"
             ",\"status\":\"accepted\"}]}", "10000");
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(with_purge, &profile) == PROFILE_MESSAGE_OK);
    assert(profile.purge_maximum_defined);
    assert(profile.purge_maximum_ms == 10000U);

    /* Si viene, tiene que ser un entero: ni null, ni texto, ni fracción. Un
     * valor que no se puede leer no se cambia por el compilado en silencio. */
    const char *const unreadable[] = {"null", "\"10000\"", "2500.5", "0", "-4000"};
    for (size_t i = 0; i < sizeof(unreadable) / sizeof(unreadable[0]); ++i) {
        snprintf(with_purge, sizeof(with_purge),
                 "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
                 ",\"temperatureSetpointC\":93.0,\"temperatureToleranceC\":0.5"
                 ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
                 ",\"maximumTimeMs\":40000,\"purgeMaximumTimeMs\":%s"
                 ",\"status\":\"accepted\"}]}", unreadable[i]);
        assert(profile_message_parse_first(with_purge, &profile) ==
               PROFILE_MESSAGE_MALFORMED);
    }

    /* El rango no es cosa del lector sino de la validación, que lo rechaza
     * con causa en vez de recortarlo. */
    const extraction_profile_limits_t limits = extraction_simulation_limits();
    const char *const out_of_range[] = {"999", "30001"};
    for (size_t i = 0; i < sizeof(out_of_range) / sizeof(out_of_range[0]); ++i) {
        snprintf(with_purge, sizeof(with_purge),
                 "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
                 ",\"temperatureSetpointC\":93.0,\"temperatureToleranceC\":0.5"
                 ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
                 ",\"maximumTimeMs\":40000,\"purgeMaximumTimeMs\":%s"
                 ",\"status\":\"accepted\"}]}", out_of_range[i]);
        assert(profile_message_parse_first(with_purge, &profile) == PROFILE_MESSAGE_OK);
        assert(extraction_profile_validate(&profile, &limits) == PROFILE_PURGE_OUT_OF_RANGE);
    }
    profile.purge_maximum_ms = 1000U;
    assert(extraction_profile_validate(&profile, &limits) == PROFILE_VALID);
    profile.purge_maximum_ms = 30000U;
    assert(extraction_profile_validate(&profile, &limits) == PROFILE_VALID);

    /* El desplazamiento de caldera del primer uso es opcional: sin él, cero,
     * que es el comportamiento de antes. */
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(ACCEPTED_PROFILE, &profile) == PROFILE_MESSAGE_OK);
    assert(profile.boiler_offset_c == 0.0F);

    char with_offset[1024];
    snprintf(with_offset, sizeof(with_offset),
             "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
             ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
             ",\"maximumTimeMs\":40000,\"boilerOffsetC\":%s"
             ",\"status\":\"accepted\"}]}", "3.25");
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(with_offset, &profile) == PROFILE_MESSAGE_OK);
    assert(profile.boiler_offset_c == 3.25F);
    /* El agua pedida sigue siendo la del perfil: el desplazamiento no la toca. */
    assert(profile.temperature_setpoint_c == 92.0F);

    /* Si viene, tiene que leerse como número: ni null ni texto. */
    const char *const unreadable_offsets[] = {"null", "\"3\"", "x"};
    for (size_t i = 0; i < sizeof(unreadable_offsets) / sizeof(unreadable_offsets[0]); ++i) {
        snprintf(with_offset, sizeof(with_offset),
                 "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
                 ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
                 ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
                 ",\"maximumTimeMs\":40000,\"boilerOffsetC\":%s"
                 ",\"status\":\"accepted\"}]}", unreadable_offsets[i]);
        assert(profile_message_parse_first(with_offset, &profile) ==
               PROFILE_MESSAGE_MALFORMED);
    }

    /* Un negativo se lee tal cual: el rechazo con causa es de la activación
     * (`first_use_offset_check`), no del lector, que no recorta. La validación
     * del perfil no lo juzga, porque no conoce el máximo del acta. */
    snprintf(with_offset, sizeof(with_offset),
             "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
             ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
             ",\"maximumTimeMs\":40000,\"boilerOffsetC\":-1.5"
             ",\"status\":\"accepted\"}]}");
    assert(profile_message_parse_first(with_offset, &profile) == PROFILE_MESSAGE_OK);
    assert(profile.boiler_offset_c == -1.5F);
    assert(extraction_profile_validate(&profile, &limits) == PROFILE_VALID);
    assert(strcmp(extraction_profile_validation_name(PROFILE_BOILER_OFFSET_OUT_OF_RANGE),
                  "boiler-offset-out-of-range") == 0);

    /* El refuerzo de la prealimentación (enmienda del 2026-10-27): opcional,
     * número, sin null; lo juzga la activación, no el lector. */
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(ACCEPTED_PROFILE, &profile) == PROFILE_MESSAGE_OK);
    assert(profile.first_use_feedforward_gain == 0.0F);
    const char *const gains[] = {"1.25", "0.1"};
    const float read[] = {1.25F, 0.1F};
    for (size_t i = 0; i < sizeof(gains) / sizeof(gains[0]); ++i) {
        snprintf(with_offset, sizeof(with_offset),
                 "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
                 ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
                 ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
                 ",\"maximumTimeMs\":40000,\"firstUseFeedforwardGain\":%s"
                 ",\"status\":\"accepted\"}]}", gains[i]);
        memset(&profile, 0, sizeof(profile));
        assert(profile_message_parse_first(with_offset, &profile) == PROFILE_MESSAGE_OK);
        assert(profile.first_use_feedforward_gain == read[i]);
        assert(profile.boiler_offset_c == 0.0F);
        assert(extraction_profile_validate(&profile, &limits) == PROFILE_VALID);
    }
    for (size_t i = 0; i < sizeof(unreadable_offsets) / sizeof(unreadable_offsets[0]); ++i) {
        snprintf(with_offset, sizeof(with_offset),
                 "{\"profileProposals\":[{\"id\":\"p\",\"name\":\"n\",\"version\":1"
                 ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
                 ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
                 ",\"maximumTimeMs\":40000,\"firstUseFeedforwardGain\":%s"
                 ",\"status\":\"accepted\"}]}", unreadable_offsets[i]);
        assert(profile_message_parse_first(with_offset, &profile) ==
               PROFILE_MESSAGE_MALFORMED);
    }
    assert(strcmp(extraction_profile_validation_name(PROFILE_FIRST_USE_FEEDFORWARD_OUT_OF_RANGE),
                  "first-use-feedforward-out-of-range") == 0);

    /* Los campos opcionales se leen solo de la primera propuesta. Si la
     * primera no pide desplazamiento y la segunda sí, la primera no se lleva
     * el de la otra: sería subir la caldera por la petición de otro perfil. */
    char two_proposals[1024];
    snprintf(two_proposals, sizeof(two_proposals),
             "{\"profileProposals\":["
             "{\"id\":\"a\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
             ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
             ",\"maximumTimeMs\":40000,\"status\":\"accepted\"}"
             ",{\"id\":\"b\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
             ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
             ",\"maximumTimeMs\":40000,\"purgeMaximumTimeMs\":20000"
             ",\"expectedBeverageMassG\":36.0,\"boilerOffsetC\":3.0"
             ",\"status\":\"accepted\"}]}");
    memset(&profile, 0, sizeof(profile));
    assert(profile_message_parse_first(two_proposals, &profile) == PROFILE_MESSAGE_OK);
    assert(strcmp(profile.id, "a") == 0);
    assert(profile.boiler_offset_c == 0.0F);
    assert(!profile.purge_maximum_defined);
    assert(!profile.expected_beverage_mass_defined);

    /* Y un campo obligatorio que falta en la primera tampoco se toma de la
     * segunda. */
    snprintf(two_proposals, sizeof(two_proposals),
             "{\"profileProposals\":["
             "{\"id\":\"a\",\"name\":\"n\",\"version\":1"
             ",\"temperatureSetpointC\":92.0,\"temperatureToleranceC\":0.5"
             ",\"pressureSetpointBar\":9.0,\"pressureToleranceBar\":0.5"
             ",\"status\":\"accepted\"}"
             ",{\"id\":\"b\",\"name\":\"n\",\"version\":1"
             ",\"maximumTimeMs\":40000,\"status\":\"accepted\"}]}");
    assert(profile_message_parse_first(two_proposals, &profile) == PROFILE_MESSAGE_MALFORMED);

    printf("profile_message: 15 casos correctos\n");
    return 0;
}
