/*
 * La planta identificada se fija al compilar (ADR-020 § 4.2).
 *
 * Se compila dos veces. Sin archivo de planta, como está el repositorio, ningún
 * lazo puede salir puesto en servicio. Con el archivo que el generador produce
 * desde un acta **de prueba** —valores copiados del modelo de banco, no medidos,
 * y marcados así en el acta—, solo el lazo de esa acta sale puesto en servicio,
 * con las ganancias de SIMC sobre esa planta y su límite duro.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "commissioned_plant.h"
#include "control_tuning.h"
#include "process_supervisor.h"

/* Una planta de prueba, declarada como tal. */
static control_identified_plant_t test_plant(void)
{
    return (control_identified_plant_t){
        .identifier = "planta-de-prueba-v1",
        .gain = 0.09F,
        .time_constant_s = 0.40F,
        .dead_time_s = 0.05F,
        .lambda_s = 0.15F,
        .filter_time_constant_s = 0.05F,
        .setpoint_rate = 3.0F,
        .output_maximum = 100.0F,
    };
}

static void test_identified_plant_is_tuned_with_its_own_lambda(void)
{
    const control_identified_plant_t plant = test_plant();
    const control_tuning_t tuning = control_tuning_from_identified(&plant, 0.020F);
    assert(tuning.commissioned);
    assert(strcmp(tuning.identifier, "planta-de-prueba-v1") == 0);
    float kp = 0.0F;
    float ki = 0.0F;
    assert(control_tuning_simc(0.09F, 0.40F, 0.05F, 0.15F, &kp, &ki));
    assert(tuning.pi.proportional_gain == kp);
    assert(tuning.pi.integral_gain == ki);
    assert(tuning.filter_time_constant_s == 0.05F);
    assert(tuning.pi.setpoint_rate == 3.0F);
    assert(tuning.pi.output_minimum == 0.0F && tuning.pi.output_maximum == 100.0F);
}

/* Una planta que no admite sintonía no pone nada en servicio. */
static void test_unusable_plant_is_not_commissioned(void)
{
    assert(!control_tuning_from_identified(NULL, 0.020F).commissioned);

    control_identified_plant_t plant = test_plant();
    plant.gain = 0.0F;
    control_tuning_t tuning = control_tuning_from_identified(&plant, 0.020F);
    assert(!tuning.commissioned);
    assert(tuning.pi.proportional_gain == 0.0F && tuning.pi.integral_gain == 0.0F);

    plant = test_plant();
    plant.time_constant_s = NAN;
    assert(!control_tuning_from_identified(&plant, 0.020F).commissioned);

    plant = test_plant();
    plant.filter_time_constant_s = 0.0F;
    assert(!control_tuning_from_identified(&plant, 0.020F).commissioned);

    plant = test_plant();
    plant.identifier = "con,coma";
    assert(!control_tuning_from_identified(&plant, 0.020F).commissioned);

    plant = test_plant();
    plant.identifier = "";
    assert(!control_tuning_from_identified(&plant, 0.020F).commissioned);

    plant = test_plant();
    plant.identifier = "identificador-demasiado-largo-para-el-registro";
    assert(!control_tuning_from_identified(&plant, 0.020F).commissioned);

    plant = test_plant();
    assert(!control_tuning_from_identified(&plant, 0.0F).commissioned);

    /* Sin techo declarado no hay sintonía, y un techo por encima del 100 % no
     * se interpreta. */
    plant = test_plant();
    plant.output_maximum = 0.0F;
    assert(!control_tuning_from_identified(&plant, 0.020F).commissioned);
    plant.output_maximum = 150.0F;
    assert(!control_tuning_from_identified(&plant, 0.020F).commissioned);
}

static void test_compiled_plants(void)
{
    const control_tuning_t pressure = control_tuning_pressure();
    const control_tuning_t temperature = control_tuning_temperature();
    const process_limits_t limits = process_limits_default();
    assert(process_limits_are_valid(&limits));

#if HAVE_COMMISSIONED_TEMPERATURE
    /* El acta térmica firmada el 2026-10-10: el lazo térmico, y solo él, sale
     * puesto en servicio con su identificador y su límite duro. */
    assert(temperature.commissioned);
    assert(strcmp(temperature.identifier, COMMISSIONED_TEMPERATURE_IDENTIFIER) == 0);
    assert(limits.hard_overtemperature_c == COMMISSIONED_TEMPERATURE_HARD_LIMIT);
    assert(temperature.pi.sample_period_s == 0.200F);
    assert(temperature.pi.output_maximum == COMMISSIONED_TEMPERATURE_OUTPUT_MAXIMUM);
#else
    assert(!temperature.commissioned);
    assert(strcmp(temperature.identifier, CONTROL_TUNING_IDENTIFIER) == 0);
    assert(limits.hard_overtemperature_c == 150.0F);
#endif

#if HAVE_COMMISSIONED_PRESSURE
    /* El acta firmada el 2026-10-12 o, en la segunda compilación, la de prueba
     * que se pone delante en la ruta de inclusión: en los dos casos el lazo
     * sale puesto en servicio con el identificador, el techo y el límite duro
     * de su acta. */
    assert(pressure.commissioned);
    assert(strcmp(pressure.identifier, COMMISSIONED_PRESSURE_IDENTIFIER) == 0);
    assert(pressure.pi.sample_period_s == 0.020F);
    assert(pressure.pi.output_maximum == COMMISSIONED_PRESSURE_OUTPUT_MAXIMUM);
    assert(limits.hard_overpressure_bar == COMMISSIONED_PRESSURE_HARD_LIMIT);
    printf("  presión puesta en servicio con %s\n", COMMISSIONED_PRESSURE_IDENTIFIER);
#else
    /* Sin archivo de planta ningún binario puede reportar `commissioned`
     * verdadero (ADR-020 § 9). */
    assert(!pressure.commissioned);
    assert(strcmp(pressure.identifier, CONTROL_TUNING_IDENTIFIER) == 0);
    assert(limits.hard_overpressure_bar == 12.0F);
    puts("  sin archivo de planta: ningún lazo puesto en servicio");
#endif
}

int main(void)
{
    test_identified_plant_is_tuned_with_its_own_lambda();
    test_unusable_plant_is_not_commissioned();
    test_compiled_plants();
    puts("PASS: planta identificada fijada al compilar");
    return 0;
}
