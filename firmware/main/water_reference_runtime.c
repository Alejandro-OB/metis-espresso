#include "water_reference_runtime.h"

#include "commissioned_plant.h"

water_reference_t water_reference_commissioned(void)
{
#if HAVE_COMMISSIONED_TEMPERATURE && defined(COMMISSIONED_TEMPERATURE_WATER_GAIN)
    return (water_reference_t) {
        .defined = true,
        .gain = COMMISSIONED_TEMPERATURE_WATER_GAIN,
        .offset_c = COMMISSIONED_TEMPERATURE_WATER_OFFSET_C,
        .boiler_minimum_c = COMMISSIONED_TEMPERATURE_WATER_BOILER_MINIMUM_C,
        .boiler_maximum_c = COMMISSIONED_TEMPERATURE_WATER_BOILER_MAXIMUM_C,
    };
#else
    return (water_reference_t) {0};
#endif
}
