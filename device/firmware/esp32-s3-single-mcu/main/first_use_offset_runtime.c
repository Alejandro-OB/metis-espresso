#include "first_use_offset_runtime.h"

#include <stddef.h>

#include "sdkconfig.h"

#include "commissioned_plant.h"

float first_use_offset_maximum_c(first_use_offset_source_t *source)
{
#if defined(CONFIG_CAFE_FIRST_USE_OFFSET_PROVISIONAL)
    if (source != NULL) {
        *source = FIRST_USE_OFFSET_SOURCE_PROVISIONAL;
    }
    return (float)CONFIG_CAFE_FIRST_USE_OFFSET_MAX_DECI_C / 10.0F;
#elif HAVE_COMMISSIONED_TEMPERATURE && defined(COMMISSIONED_TEMPERATURE_FIRST_USE_MAX_OFFSET_C)
    if (source != NULL) {
        *source = FIRST_USE_OFFSET_SOURCE_ACTA;
    }
    return COMMISSIONED_TEMPERATURE_FIRST_USE_MAX_OFFSET_C;
#else
    if (source != NULL) {
        *source = FIRST_USE_OFFSET_SOURCE_NONE;
    }
    return 0.0F;
#endif
}

float heater_feedforward_configured_gain(const char **source)
{
    /* La ganancia sale del acta, como `δ`; la opción provisional solo existe
     * en el binario de identificación (ADR-009, enmienda del 2026-10-12). */
#if defined(CONFIG_CAFE_HEATER_FEEDFORWARD_MILLI) && CONFIG_CAFE_HEATER_FEEDFORWARD_MILLI > 0
    if (source != NULL) {
        *source = "provisional (ensayo supervisado)";
    }
    return (float)CONFIG_CAFE_HEATER_FEEDFORWARD_MILLI / 1000.0F;
#elif HAVE_COMMISSIONED_TEMPERATURE && defined(COMMISSIONED_TEMPERATURE_FEEDFORWARD_GAIN)
    if (source != NULL) {
        *source = COMMISSIONED_TEMPERATURE_IDENTIFIER;
    }
    return COMMISSIONED_TEMPERATURE_FEEDFORWARD_GAIN;
#else
    if (source != NULL) {
        *source = "sin ganancia en el acta";
    }
    return 0.0F;
#endif
}

float first_use_feedforward_maximum_gain(void)
{
#if HAVE_COMMISSIONED_TEMPERATURE && defined(COMMISSIONED_TEMPERATURE_FIRST_USE_MAX_FEEDFORWARD_GAIN)
    return COMMISSIONED_TEMPERATURE_FIRST_USE_MAX_FEEDFORWARD_GAIN;
#else
    return heater_feedforward_configured_gain(NULL);
#endif
}

const char *first_use_offset_source_name(first_use_offset_source_t source)
{
    switch (source) {
    case FIRST_USE_OFFSET_SOURCE_PROVISIONAL:
        return "provisional (ensayo supervisado)";
    case FIRST_USE_OFFSET_SOURCE_ACTA:
#if HAVE_COMMISSIONED_TEMPERATURE
        return COMMISSIONED_TEMPERATURE_IDENTIFIER;
#else
        return "acta";
#endif
    case FIRST_USE_OFFSET_SOURCE_NONE:
    default:
        return "sin máximo en el acta";
    }
}
