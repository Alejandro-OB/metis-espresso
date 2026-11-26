#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "actuation_guard.h"

/* Una petición que, por sí sola, permitiría ambas salidas. */
static actuation_request_t permissive(void)
{
    return (actuation_request_t){
        .starting_up = false,
        .pressure_commissioned = true,
        .temperature_commissioned = true,
        .authorization = {
            .heater_regulation_allowed = true,
            .pressure_regulation_allowed = true,
            .extraction_pump_allowed = true,
            .purge_pump_allowed = false,
        },
        .temperature_c = 93.0F,
        .pressure_bar = 9.0F,
        .temperature_usable = true,
        .pressure_usable = true,
        .hard_overtemperature_c = 150.0F,
        .hard_overpressure_bar = 12.0F,
        .zero_cross_observed = true,
        .zero_cross_available = true,
        .acquisition_age_ms = 100U,
        .maximum_acquisition_age_ms = 500U,
    };
}

static actuation_decision_t decide(const actuation_request_t *request)
{
    actuation_guard_t guard;
    actuation_guard_init(&guard);
    return actuation_guard_evaluate(&guard, request);
}

/* Una estructura sin inicializar no puede autorizar potencia. */
static void test_zero_value_never_allows(void)
{
    const actuation_decision_t empty = {0};
    assert(!actuation_verdict_allows(empty.pump));
    assert(!actuation_verdict_allows(empty.heater));

    actuation_guard_t uninitialized = {0};
    const actuation_request_t request = permissive();
    const actuation_decision_t decision =
        actuation_guard_evaluate(&uninitialized, &request);
    assert(decision.pump == ACTUATION_UNDECIDED);
    assert(decision.heater == ACTUATION_UNDECIDED);
}

static void test_permissive_case_allows_both(void)
{
    const actuation_request_t request = permissive();
    const actuation_decision_t decision = decide(&request);
    assert(decision.pump == ACTUATION_ALLOWED);
    assert(decision.heater == ACTUATION_ALLOWED);
}

/* Las ocho condiciones de ADR-007 § 3.5, una por una. */
static void test_each_inhibition(void)
{
    actuation_request_t request;

    request = permissive();
    request.starting_up = true;
    assert(decide(&request).pump == ACTUATION_INHIBITED_STARTING_UP);
    assert(decide(&request).heater == ACTUATION_INHIBITED_STARTING_UP);

    request = permissive();
    request.authorization.extraction_pump_allowed = false;
    request.authorization.purge_pump_allowed = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_NOT_AUTHORIZED);
    assert(decide(&request).heater == ACTUATION_ALLOWED);

    request = permissive();
    request.pressure_commissioned = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_NOT_COMMISSIONED);

    request = permissive();
    request.acquisition_age_ms = 501U;
    assert(decide(&request).pump == ACTUATION_INHIBITED_DEADLINE_MISSED);
    assert(decide(&request).heater == ACTUATION_INHIBITED_DEADLINE_MISSED);

    request = permissive();
    request.pressure_usable = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);

    request = permissive();
    request.pressure_bar = 12.0F;
    assert(decide(&request).pump == ACTUATION_INHIBITED_HARD_LIMIT);

    request = permissive();
    request.zero_cross_available = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_ZERO_CROSS);

    /* Un sincronismo que nadie observa todavía no puede condenar la bomba. */
    request = permissive();
    request.zero_cross_observed = false;
    request.zero_cross_available = false;
    assert(decide(&request).pump == ACTUATION_ALLOWED);
}

static void test_heater_inhibitions(void)
{
    actuation_request_t request;

    request = permissive();
    request.temperature_commissioned = false;
    assert(decide(&request).heater == ACTUATION_INHIBITED_NOT_COMMISSIONED);
    assert(decide(&request).pump == ACTUATION_ALLOWED);

    request = permissive();
    request.temperature_usable = false;
    assert(decide(&request).heater == ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);

    request = permissive();
    request.temperature_c = 150.0F;
    assert(decide(&request).heater == ACTUATION_INHIBITED_HARD_LIMIT);

    request = permissive();
    request.authorization.heater_regulation_allowed = false;
    assert(decide(&request).heater == ACTUATION_INHIBITED_NOT_AUTHORIZED);
}

/* El arranque explica cualquier otra causa concurrente, no al revés. */
static void test_most_general_cause_wins(void)
{
    actuation_request_t request = permissive();
    request.starting_up = true;
    request.pressure_usable = false;
    request.pressure_commissioned = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_STARTING_UP);
}

static void test_pump_service_limit(void)
{
    actuation_guard_t guard;
    actuation_guard_init(&guard);
    const actuation_request_t request = permissive();

    /* Dos minutos continuos agotan el servicio. */
    for (uint32_t elapsed = 0U; elapsed < PUMP_MAXIMUM_CONTINUOUS_MS; elapsed += 1000U) {
        assert(actuation_guard_evaluate(&guard, &request).pump == ACTUATION_ALLOWED);
        actuation_guard_update_service(&guard, true, 1000U);
    }
    assert(actuation_guard_evaluate(&guard, &request).pump ==
           ACTUATION_INHIBITED_RESTING);

    /* Un reposo incompleto no devuelve el servicio. */
    actuation_guard_update_service(&guard, false, PUMP_MINIMUM_REST_MS - 1000U);
    assert(actuation_guard_evaluate(&guard, &request).pump ==
           ACTUATION_INHIBITED_RESTING);

    actuation_guard_update_service(&guard, false, 1000U);
    assert(actuation_guard_evaluate(&guard, &request).pump == ACTUATION_ALLOWED);
    assert(guard.pump_service.enabled_ms == 0U);
}

/*
 * Una pausa corta no restablece el servicio: de lo contrario bastaría con
 * detener la bomba un instante para volver a disponer de dos minutos enteros, y
 * la protección de `ID2/1min` quedaría vacía.
 */
static void test_short_pause_does_not_reset_service(void)
{
    actuation_guard_t guard;
    actuation_guard_init(&guard);
    const actuation_request_t request = permissive();

    actuation_guard_update_service(&guard, true, 110000U);
    actuation_guard_update_service(&guard, false, 2000U);  /* pausa breve */
    assert(guard.pump_service.enabled_ms == 110000U);
    assert(actuation_guard_evaluate(&guard, &request).pump == ACTUATION_ALLOWED);

    actuation_guard_update_service(&guard, true, 10000U);
    assert(actuation_guard_evaluate(&guard, &request).pump ==
           ACTUATION_INHIBITED_RESTING);
}

/*
 * Petición de identificación de una salida sin poner en servicio, que por lo
 * demás permitiría actuar. Las dos marcas quedan en falso, como en la máquina.
 */
static actuation_request_t identifying(identification_target_t target, float demand,
                                       float maximum)
{
    actuation_request_t request = permissive();
    request.pressure_commissioned = false;
    request.temperature_commissioned = false;
    request.identification = (identification_request_t){
        .target = target,
        .demand_percent = demand,
        .maximum_percent = maximum,
    };
    return request;
}

/* El veredicto de identificación permite actuar y no es el ordinario. */
static void test_identification_verdict_is_distinct(void)
{
    assert(actuation_verdict_allows(ACTUATION_ALLOWED_IDENTIFICATION));
    assert(ACTUATION_ALLOWED_IDENTIFICATION != ACTUATION_ALLOWED);
    assert(strcmp(actuation_verdict_name(ACTUATION_ALLOWED_IDENTIFICATION),
                  actuation_verdict_name(ACTUATION_ALLOWED)) != 0);
    assert(!actuation_verdict_allows(ACTUATION_INHIBITED_STIMULUS_LIMIT));
    /* Ninguno de los dos es avería: no deben encender la alarma del barista. */
    assert(!actuation_verdict_is_fault(ACTUATION_ALLOWED_IDENTIFICATION));
    assert(!actuation_verdict_is_fault(ACTUATION_INHIBITED_STIMULUS_LIMIT));
}

/* Una petición de identificación puesta a cero no relaja nada. */
static void test_zero_identification_relaxes_nothing(void)
{
    actuation_request_t request = permissive();
    request.pressure_commissioned = false;
    request.temperature_commissioned = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_NOT_COMMISSIONED);
    assert(decide(&request).heater == ACTUATION_INHIBITED_NOT_COMMISSIONED);
}

/* Relaja la causa de puesta en servicio solo para la salida que nombra. */
static void test_identification_relaxes_only_its_output(void)
{
    actuation_request_t request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    assert(decide(&request).pump == ACTUATION_ALLOWED_IDENTIFICATION);
    assert(decide(&request).heater == ACTUATION_INHIBITED_NOT_COMMISSIONED);

    request = identifying(IDENTIFICATION_TARGET_HEATER, 1.0F, 1.0F);
    assert(decide(&request).heater == ACTUATION_ALLOWED_IDENTIFICATION);
    assert(decide(&request).pump == ACTUATION_INHIBITED_NOT_COMMISSIONED);

    /* Con el lazo ya puesto en servicio la petición no cambia nada: el
     * veredicto sigue siendo el ordinario. */
    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    request.pressure_commissioned = true;
    assert(decide(&request).pump == ACTUATION_ALLOWED);
}

/* El tope lo impone el guardián: lo que lo excede inhibe y no se recorta. */
static void test_stimulus_limit(void)
{
    actuation_request_t request = identifying(IDENTIFICATION_TARGET_PUMP, 2.0F, 1.0F);
    assert(decide(&request).pump == ACTUATION_INHIBITED_STIMULUS_LIMIT);

    request = identifying(IDENTIFICATION_TARGET_HEATER, 10.5F, 10.0F);
    assert(decide(&request).heater == ACTUATION_INHIBITED_STIMULUS_LIMIT);

    /* Justo en el tope es admisible, y la demanda nula también. */
    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    assert(decide(&request).pump == ACTUATION_ALLOWED_IDENTIFICATION);
    request = identifying(IDENTIFICATION_TARGET_PUMP, 0.0F, 1.0F);
    assert(decide(&request).pump == ACTUATION_ALLOWED_IDENTIFICATION);

    /* Un tope que no se entiende no es un tope. */
    const float bad_maxima[] = {0.0F, -1.0F, 100.5F, NAN, INFINITY};
    for (size_t index = 0U; index < sizeof(bad_maxima) / sizeof(bad_maxima[0]); index++) {
        request = identifying(IDENTIFICATION_TARGET_PUMP, 0.0F, bad_maxima[index]);
        assert(decide(&request).pump == ACTUATION_INHIBITED_STIMULUS_LIMIT);
    }
    /* Ni una demanda que no es un número. */
    const float bad_demands[] = {-0.5F, NAN};
    for (size_t index = 0U; index < sizeof(bad_demands) / sizeof(bad_demands[0]); index++) {
        request = identifying(IDENTIFICATION_TARGET_PUMP, bad_demands[index], 10.0F);
        assert(decide(&request).pump == ACTUATION_INHIBITED_STIMULUS_LIMIT);
    }
}

/*
 * Un ensayo de identificación se detiene por las mismas razones que una
 * extracción. Es la propiedad que hace admisible el modo restringido, y se
 * comprueba causa por causa.
 */
static void test_identification_keeps_every_other_inhibition(void)
{
    actuation_request_t request;

    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    request.starting_up = true;
    assert(decide(&request).pump == ACTUATION_INHIBITED_STARTING_UP);

    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    request.authorization.extraction_pump_allowed = false;
    request.authorization.purge_pump_allowed = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_NOT_AUTHORIZED);

    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    request.acquisition_age_ms = 501U;
    assert(decide(&request).pump == ACTUATION_INHIBITED_DEADLINE_MISSED);

    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    request.pressure_usable = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);

    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    request.pressure_bar = 12.0F;
    assert(decide(&request).pump == ACTUATION_INHIBITED_HARD_LIMIT);

    request = identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);
    request.zero_cross_available = false;
    assert(decide(&request).pump == ACTUATION_INHIBITED_ZERO_CROSS);

    request = identifying(IDENTIFICATION_TARGET_HEATER, 1.0F, 1.0F);
    request.authorization.heater_regulation_allowed = false;
    assert(decide(&request).heater == ACTUATION_INHIBITED_NOT_AUTHORIZED);

    request = identifying(IDENTIFICATION_TARGET_HEATER, 1.0F, 1.0F);
    request.temperature_usable = false;
    assert(decide(&request).heater == ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);

    request = identifying(IDENTIFICATION_TARGET_HEATER, 1.0F, 1.0F);
    request.temperature_c = 150.0F;
    assert(decide(&request).heater == ACTUATION_INHIBITED_HARD_LIMIT);

    request = identifying(IDENTIFICATION_TARGET_HEATER, 1.0F, 1.0F);
    request.acquisition_age_ms = 501U;
    assert(decide(&request).heater == ACTUATION_INHIBITED_DEADLINE_MISSED);
}

/* El servicio continuo de la bomba cuenta igual identificando. */
static void test_identification_keeps_pump_service(void)
{
    actuation_guard_t guard;
    actuation_guard_init(&guard);
    const actuation_request_t request =
        identifying(IDENTIFICATION_TARGET_PUMP, 1.0F, 1.0F);

    for (uint32_t elapsed = 0U; elapsed < PUMP_MAXIMUM_CONTINUOUS_MS; elapsed += 1000U) {
        const actuation_verdict_t verdict = actuation_guard_evaluate(&guard, &request).pump;
        assert(verdict == ACTUATION_ALLOWED_IDENTIFICATION);
        actuation_guard_update_service(&guard, actuation_verdict_allows(verdict), 1000U);
    }
    assert(actuation_guard_evaluate(&guard, &request).pump ==
           ACTUATION_INHIBITED_RESTING);
}

int main(void)
{
    test_zero_value_never_allows();
    test_permissive_case_allows_both();
    test_each_inhibition();
    test_heater_inhibitions();
    test_most_general_cause_wins();
    test_pump_service_limit();
    test_short_pause_does_not_reset_service();
    test_identification_verdict_is_distinct();
    test_zero_identification_relaxes_nothing();
    test_identification_relaxes_only_its_output();
    test_stimulus_limit();
    test_identification_keeps_every_other_inhibition();
    test_identification_keeps_pump_service();
    puts("PASS: inhibición de salidas, servicio continuo de la bomba y modo restringido");
    return 0;
}
