#pragma once

/*
 * Guardián de las salidas de potencia (ADR-007 § 3.5).
 *
 * Decide, en cada ciclo y con independencia de cualquier controlador, si la
 * bomba y el calentador pueden energizarse. Su respuesta no es un permiso que
 * el control pueda discutir: cuando inhibe, las salidas van a reposo por una
 * ruta propia, anterior a toda ley de regulación.
 *
 * Existe antes que los controladores a propósito. Construir primero el lazo y
 * después su inhibición obligaría a rehacer el lazo, y entretanto dejaría a la
 * máquina con actuación y sin freno.
 *
 * ## El modo restringido
 *
 * Desde la enmienda del 2026-09-21 de ADR-007 § 5 hay **dos veredictos que
 * permiten actuar**: el ordinario y el de identificación. El segundo relaja
 * exactamente una causa —sin poner en servicio—, para una sola salida, y solo
 * si el estímulo pedido cabe bajo el tope que viaja en la petición. Todas las
 * demás causas se evalúan igual que en operación y en el mismo orden.
 *
 * El tope lo impone el guardián y no la secuencia por la misma razón por la que
 * decide antes que cualquier controlador: quien genera el estímulo no puede ser
 * quien juzgue si es admisible. Un estímulo por encima del tope **inhibe**, no se
 * recorta.
 */

#include <stdbool.h>
#include <stdint.h>

#include "operational_fsm.h"

/*
 * Motivo de la decisión. El cero significa «nadie ha decidido», nunca
 * «permitido»: una estructura recién puesta a cero no puede autorizar potencia.
 */
typedef enum {
    ACTUATION_UNDECIDED = 0,
    ACTUATION_ALLOWED,
    ACTUATION_INHIBITED_STARTING_UP,      /* arranque, reinicio o actualización */
    ACTUATION_INHIBITED_NOT_AUTHORIZED,   /* el estado operativo no lo permite */
    ACTUATION_INHIBITED_NOT_COMMISSIONED, /* sin parámetros puestos en servicio */
    ACTUATION_INHIBITED_DEADLINE_MISSED,  /* la adquisición incumplió su plazo */
    ACTUATION_INHIBITED_SIGNAL_NOT_USABLE,
    ACTUATION_INHIBITED_HARD_LIMIT,
    ACTUATION_INHIBITED_ZERO_CROSS,       /* sincronismo de red perdido */
    ACTUATION_INHIBITED_DUTY_LIMIT,       /* servicio continuo agotado */
    ACTUATION_INHIBITED_RESTING,          /* reposo obligatorio en curso */
    /*
     * Los dos que trae el modo restringido (ADR-007 § 5, enmienda del
     * 2026-09-21). Van al final para no mover los códigos que ya existen.
     *
     * El primero **permite**, y es distinto de `ACTUATION_ALLOWED` a propósito:
     * la columna de veredicto del registro dice así por sí sola si una línea se
     * tomó identificando o regulando. Quien pregunte si puede actuar lo hace por
     * `actuation_verdict_allows`, nunca comparando contra el ordinario.
     */
    ACTUATION_ALLOWED_IDENTIFICATION,     /* estímulo de identificación admitido */
    ACTUATION_INHIBITED_STIMULUS_LIMIT,   /* el estímulo excede el tope vigente */
} actuation_verdict_t;

/*
 * Salida sobre la que se pide identificar. El cero es «ninguna»: una petición
 * puesta a cero no relaja nada.
 */
typedef enum {
    IDENTIFICATION_TARGET_NONE = 0,
    IDENTIFICATION_TARGET_PUMP,
    IDENTIFICATION_TARGET_HEATER,
} identification_target_t;

/*
 * Lo que el modo restringido pide al guardián en cada ciclo.
 *
 * Viajan juntos la demanda y el tope, pero no vienen del mismo sitio: la
 * demanda la produce la secuencia de estímulo y el tope lo declara la
 * configuración del binario, aparte. El guardián compara los dos y es el único
 * que decide; la secuencia no se juzga a sí misma.
 */
typedef struct {
    identification_target_t target;
    float demand_percent;   /* lo que la secuencia quiere aplicar ahora */
    float maximum_percent;  /* tope vigente; provisional mientras no se mida */
} identification_request_t;

/*
 * Servicio de la bomba. La JYPC-503A está marcada `ID2/1min`: ADR-007 decide
 * medirlo con tiempo monotónico mientras la bomba esté habilitada, sea cual sea
 * la densidad PSM solicitada, y no confiar en su protector térmico.
 */
#define PUMP_MAXIMUM_CONTINUOUS_MS UINT32_C(120000)
#define PUMP_MINIMUM_REST_MS UINT32_C(60000)

typedef struct {
    uint32_t enabled_ms;  /* habilitación acumulada sin reposo suficiente */
    uint32_t rest_ms;     /* reposo continuo acumulado */
    bool resting;         /* se agotó el servicio y el reposo es obligatorio */
} pump_service_t;

typedef struct {
    pump_service_t pump_service;
    bool initialized;
} actuation_guard_t;

typedef struct {
    bool starting_up;
    bool pressure_commissioned;   /* ADR-007: falso mientras no haya Kp y Ki */
    bool temperature_commissioned;/* falso mientras no exista el ADR térmico */
    operational_authorization_t authorization;

    float temperature_c;
    float pressure_bar;
    bool temperature_usable;
    bool pressure_usable;
    float hard_overtemperature_c;
    float hard_overpressure_bar;

    bool zero_cross_observed;
    bool zero_cross_available;

    uint32_t acquisition_age_ms;
    uint32_t maximum_acquisition_age_ms;

    /*
     * Solo relaja la causa «sin poner en servicio», y solo para la salida que
     * nombra. Cualquier otra inhibición se evalúa igual que en operación: un
     * ensayo de identificación se detiene por las mismas razones por las que se
     * detendría una extracción.
     */
    identification_request_t identification;
} actuation_request_t;

typedef struct {
    actuation_verdict_t pump;
    actuation_verdict_t heater;
} actuation_decision_t;

void actuation_guard_init(actuation_guard_t *guard);

/* Decide sin modificar el estado: la decisión no debe depender de consultarla. */
actuation_decision_t actuation_guard_evaluate(const actuation_guard_t *guard,
                                              const actuation_request_t *request);

/*
 * Avanza el servicio de la bomba con lo que realmente ocurrió.
 *
 * `pump_enabled` es la habilitación efectiva, no la solicitada: el contador
 * mide el tiempo que la bomba estuvo en condiciones de moverse.
 */
void actuation_guard_update_service(actuation_guard_t *guard,
                                    bool pump_enabled,
                                    uint32_t elapsed_ms);

bool actuation_verdict_allows(actuation_verdict_t verdict);
const char *actuation_verdict_name(actuation_verdict_t verdict);

/*
 * Distingue una **avería** de una condición normal de servicio.
 *
 * Los dos grupos inhiben igual, pero no significan lo mismo para quien está
 * delante de la máquina. Que falte poner en servicio el control, o que el estado
 * operativo no autorice regulación, describe una máquina que funciona como debe;
 * que la señal no sea apta, que se supere el límite duro, que la adquisición
 * incumpla su plazo o que se pierda el sincronismo de red describe algo roto.
 *
 * La distinción existe porque el sistema debe **informar al barista** de las
 * condiciones inseguras, y un aviso que estuviera encendido siempre no informa
 * de nada. Hoy `commissioned` es falso y seguirá siéndolo durante toda la puesta
 * en marcha: tratar esa causa como avería dejaría la alarma encendida durante
 * meses y le quitaría todo su significado justo antes de que hiciera falta.
 *
 * El servicio continuo de la bomba tampoco es avería: agotarlo y descansar es el
 * comportamiento diseñado, no un fallo.
 */
bool actuation_verdict_is_fault(actuation_verdict_t verdict);
