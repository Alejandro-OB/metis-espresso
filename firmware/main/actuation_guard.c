#include "actuation_guard.h"

#include <stddef.h>

static uint32_t saturating_add_u32(uint32_t value, uint32_t increment)
{
    return UINT32_MAX - value < increment ? UINT32_MAX : value + increment;
}

void actuation_guard_init(actuation_guard_t *guard)
{
    if (guard == NULL) {
        return;
    }
    *guard = (actuation_guard_t){.initialized = true};
}

bool actuation_verdict_allows(actuation_verdict_t verdict)
{
    return verdict == ACTUATION_ALLOWED || verdict == ACTUATION_ALLOWED_IDENTIFICATION;
}

bool actuation_verdict_is_fault(actuation_verdict_t verdict)
{
    switch (verdict) {
    case ACTUATION_INHIBITED_SIGNAL_NOT_USABLE:
    case ACTUATION_INHIBITED_HARD_LIMIT:
    case ACTUATION_INHIBITED_DEADLINE_MISSED:
    case ACTUATION_INHIBITED_ZERO_CROSS:
        return true;
    case ACTUATION_UNDECIDED:
    case ACTUATION_ALLOWED:
    case ACTUATION_INHIBITED_STARTING_UP:
    case ACTUATION_INHIBITED_NOT_AUTHORIZED:
    case ACTUATION_INHIBITED_NOT_COMMISSIONED:
    case ACTUATION_INHIBITED_DUTY_LIMIT:
    case ACTUATION_INHIBITED_RESTING:
    /* Un estímulo por encima del tope es un error de la secuencia declarada, no
     * una avería de la máquina: se corrige la secuencia, no se avisa al barista. */
    case ACTUATION_ALLOWED_IDENTIFICATION:
    case ACTUATION_INHIBITED_STIMULUS_LIMIT:
    default:
        return false;
    }
}

const char *actuation_verdict_name(actuation_verdict_t verdict)
{
    switch (verdict) {
    case ACTUATION_ALLOWED:
        return "allowed";
    case ACTUATION_INHIBITED_STARTING_UP:
        return "starting-up";
    case ACTUATION_INHIBITED_NOT_AUTHORIZED:
        return "not-authorized";
    case ACTUATION_INHIBITED_NOT_COMMISSIONED:
        return "not-commissioned";
    case ACTUATION_INHIBITED_DEADLINE_MISSED:
        return "deadline-missed";
    case ACTUATION_INHIBITED_SIGNAL_NOT_USABLE:
        return "signal-not-usable";
    case ACTUATION_INHIBITED_HARD_LIMIT:
        return "hard-limit";
    case ACTUATION_INHIBITED_ZERO_CROSS:
        return "zero-cross-lost";
    case ACTUATION_INHIBITED_DUTY_LIMIT:
        return "duty-limit";
    case ACTUATION_INHIBITED_RESTING:
        return "resting";
    case ACTUATION_ALLOWED_IDENTIFICATION:
        return "allowed-identification";
    case ACTUATION_INHIBITED_STIMULUS_LIMIT:
        return "stimulus-limit";
    case ACTUATION_UNDECIDED:
    default:
        return "undecided";
    }
}

static bool deadline_missed(const actuation_request_t *request)
{
    return request->maximum_acquisition_age_ms > 0U &&
           request->acquisition_age_ms > request->maximum_acquisition_age_ms;
}

/*
 * El estímulo es admisible si cabe bajo un tope que tenga sentido.
 *
 * Un tope no finito, nulo o por encima del 100 % no se interpreta: se trata
 * como ausente, y sin tope no hay estímulo admisible. Tampoco se recorta una
 * demanda excesiva para dejarla pasar: recortarla cambiaría en silencio la
 * secuencia declarada, y lo que hay que revisar en ese caso es la secuencia.
 */
static bool stimulus_admissible(const identification_request_t *identification)
{
    const float demand = identification->demand_percent;
    const float maximum = identification->maximum_percent;
    /* Las comparaciones con NaN son falsas, de modo que un valor no finito no
     * pasa ninguna de las tres y cae en la inhibición. */
    if (!(maximum > 0.0F && maximum <= 100.0F)) {
        return false;
    }
    return demand >= 0.0F && demand <= maximum;
}

/*
 * Decide la causa «sin poner en servicio» para una salida.
 *
 * Devuelve `ACTUATION_UNDECIDED` cuando esta causa no inhibe y la evaluación
 * debe seguir, y en `identifying` si sigue gracias al modo restringido. Es la
 * única causa que ese modo puede relajar, y por eso es la única que pasa por
 * aquí.
 */
static actuation_verdict_t commissioning_verdict(bool commissioned,
                                                 identification_target_t output,
                                                 const actuation_request_t *request,
                                                 bool *identifying)
{
    *identifying = false;
    if (commissioned) {
        return ACTUATION_UNDECIDED;
    }
    if (request->identification.target != output) {
        return ACTUATION_INHIBITED_NOT_COMMISSIONED;
    }
    if (!stimulus_admissible(&request->identification)) {
        return ACTUATION_INHIBITED_STIMULUS_LIMIT;
    }
    *identifying = true;
    return ACTUATION_UNDECIDED;
}

/*
 * El orden va de la causa más general a la más específica de la máquina, para
 * que el motivo registrado explique el fallo y no su consecuencia: una bomba
 * inhibida durante el arranque no debe aparecer como «sin autorización».
 */
static actuation_verdict_t evaluate_pump(const actuation_guard_t *guard,
                                         const actuation_request_t *request)
{
    if (request->starting_up) {
        return ACTUATION_INHIBITED_STARTING_UP;
    }
    if (!request->authorization.extraction_pump_allowed &&
        !request->authorization.purge_pump_allowed) {
        return ACTUATION_INHIBITED_NOT_AUTHORIZED;
    }
    /* Sin parámetros identificados no hay lazo que energizar: aplazar la
     * sintonía no puede traducirse en ganancias inventadas ni en un arranque a
     * ciegas (ADR-007 § 3.5). La única excepción es el estímulo de
     * identificación, que sustituye al controlador en vez de habilitarlo. */
    bool identifying = false;
    const actuation_verdict_t commissioning = commissioning_verdict(
        request->pressure_commissioned, IDENTIFICATION_TARGET_PUMP, request, &identifying);
    if (commissioning != ACTUATION_UNDECIDED) {
        return commissioning;
    }
    if (deadline_missed(request)) {
        return ACTUATION_INHIBITED_DEADLINE_MISSED;
    }
    if (!request->pressure_usable) {
        return ACTUATION_INHIBITED_SIGNAL_NOT_USABLE;
    }
    if (request->pressure_bar >= request->hard_overpressure_bar) {
        return ACTUATION_INHIBITED_HARD_LIMIT;
    }
    /* Un sincronismo que nadie observa no condena la preparación; uno observado
     * y perdido deja al PSM sin oportunidades y sí la condena. */
    if (request->zero_cross_observed && !request->zero_cross_available) {
        return ACTUATION_INHIBITED_ZERO_CROSS;
    }
    if (guard->pump_service.resting) {
        return ACTUATION_INHIBITED_RESTING;
    }
    if (guard->pump_service.enabled_ms >= PUMP_MAXIMUM_CONTINUOUS_MS) {
        return ACTUATION_INHIBITED_DUTY_LIMIT;
    }
    return identifying ? ACTUATION_ALLOWED_IDENTIFICATION : ACTUATION_ALLOWED;
}

static actuation_verdict_t evaluate_heater(const actuation_request_t *request)
{
    if (request->starting_up) {
        return ACTUATION_INHIBITED_STARTING_UP;
    }
    if (!request->authorization.heater_regulation_allowed) {
        return ACTUATION_INHIBITED_NOT_AUTHORIZED;
    }
    bool identifying = false;
    const actuation_verdict_t commissioning = commissioning_verdict(
        request->temperature_commissioned, IDENTIFICATION_TARGET_HEATER, request,
        &identifying);
    if (commissioning != ACTUATION_UNDECIDED) {
        return commissioning;
    }
    if (deadline_missed(request)) {
        return ACTUATION_INHIBITED_DEADLINE_MISSED;
    }
    if (!request->temperature_usable) {
        return ACTUATION_INHIBITED_SIGNAL_NOT_USABLE;
    }
    if (request->temperature_c >= request->hard_overtemperature_c) {
        return ACTUATION_INHIBITED_HARD_LIMIT;
    }
    return identifying ? ACTUATION_ALLOWED_IDENTIFICATION : ACTUATION_ALLOWED;
}

actuation_decision_t actuation_guard_evaluate(const actuation_guard_t *guard,
                                              const actuation_request_t *request)
{
    actuation_decision_t decision = {
        .pump = ACTUATION_UNDECIDED,
        .heater = ACTUATION_UNDECIDED,
    };
    if (guard == NULL || !guard->initialized || request == NULL) {
        return decision;
    }
    decision.pump = evaluate_pump(guard, request);
    decision.heater = evaluate_heater(request);
    return decision;
}

/*
 * Una pausa corta no restablece el servicio.
 *
 * El texto de ADR-007 exige reposo «después de ese límite», lo que admitiría
 * reiniciar la cuenta con cualquier interrupción breve: dos minutos de bomba,
 * un segundo parada y otros dos minutos. Eso vaciaría la protección, así que
 * solo un reposo completo devuelve el servicio íntegro. Es la lectura más
 * conservadora, coherente con que el propio ADR trate `ID2/1min` de forma
 * conservadora mientras no exista una fuente del fabricante.
 */
void actuation_guard_update_service(actuation_guard_t *guard,
                                    bool pump_enabled,
                                    uint32_t elapsed_ms)
{
    if (guard == NULL || !guard->initialized || elapsed_ms == 0U) {
        return;
    }
    pump_service_t *service = &guard->pump_service;

    if (pump_enabled) {
        service->enabled_ms = saturating_add_u32(service->enabled_ms, elapsed_ms);
        service->rest_ms = 0U;
        if (service->enabled_ms >= PUMP_MAXIMUM_CONTINUOUS_MS) {
            service->resting = true;
        }
        return;
    }

    service->rest_ms = saturating_add_u32(service->rest_ms, elapsed_ms);
    if (service->rest_ms >= PUMP_MINIMUM_REST_MS) {
        service->enabled_ms = 0U;
        service->resting = false;
    }
}
