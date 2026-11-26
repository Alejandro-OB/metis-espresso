/*
 * Pruebas del panel de indicadores.
 *
 * Un LED mal asignado no rompe nada: el firmware sigue funcionando, las pruebas
 * de los demás módulos siguen pasando, y el error solo se descubre el día en que
 * alguien necesita leerlo. Por eso la tabla completa se comprueba aquí, estado
 * por estado, en lugar de confiar en una inspección visual del banco.
 *
 * Dos propiedades importan más que la tabla:
 *
 * **Que un aviso de avería llegue aunque la máquina de estados no dispare.** Es
 * el aviso que el barista necesita, y el caso que lo motiva —un sensor que se
 * suelta con la cafetera en `READY`— no produce ninguna transición.
 *
 * **Que lo que no es una avería no encienda la alarma.** Hoy el control no está
 * puesto en servicio y no lo estará en meses. Si esa causa encendiera el aviso,
 * estaría encendido de forma permanente y no informaría de nada.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "indicator_panel.h"

static indicator_panel_t evaluate(operational_state_t state,
                                  actuation_verdict_t pump,
                                  actuation_verdict_t heater)
{
    const indicator_request_t request = {.state = state, .pump = pump, .heater = heater};
    return indicator_panel_evaluate(&request);
}

/* Sin ninguna avería, cada estado enciende lo que dice la tabla de la cabecera. */
static void test_every_state_lights_what_the_table_says(void)
{
    const actuation_verdict_t ok = ACTUATION_ALLOWED;

    struct {
        operational_state_t state;
        indicator_pattern_t green;
        indicator_pattern_t blue;
        indicator_pattern_t yellow;
        indicator_pattern_t red;
    } const expected[] = {
        {OP_STATE_SAFE_INACTIVE, INDICATOR_OFF, INDICATOR_OFF, INDICATOR_OFF, INDICATOR_OFF},
        {OP_STATE_COFFEE_HEATING, INDICATOR_OFF, INDICATOR_OFF, INDICATOR_SLOW, INDICATOR_OFF},
        {OP_STATE_READY, INDICATOR_ON, INDICATOR_OFF, INDICATOR_OFF, INDICATOR_OFF},
        {OP_STATE_EXTRACTION, INDICATOR_OFF, INDICATOR_ON, INDICATOR_OFF, INDICATOR_OFF},
        {OP_STATE_MANUAL_PURGE, INDICATOR_OFF, INDICATOR_ON, INDICATOR_OFF, INDICATOR_OFF},
        {OP_STATE_ALARM, INDICATOR_OFF, INDICATOR_OFF, INDICATOR_OFF, INDICATOR_ON},
    };

    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const indicator_panel_t panel = evaluate(expected[index].state, ok, ok);
        assert(panel.green == expected[index].green);
        assert(panel.blue == expected[index].blue);
        assert(panel.yellow == expected[index].yellow);
        assert(panel.red == expected[index].red);
    }
}

/*
 * Un color, un significado: ningún estado enciende dos LED a la vez, y con la
 * caldera en uso el panel nunca está oscuro.
 *
 * Lo segundo es lo que protegía el código anterior con el verde siempre
 * encendido: un panel oscuro con la máquina calentando invita a meter la mano.
 * Solo el reposo lo deja apagado, como pidió el usuario el 2026-10-07.
 */
static void test_one_colour_per_state_and_never_dark_while_in_use(void)
{
    for (int state = 0; state <= (int)OP_STATE_ALARM; state++) {
        const indicator_panel_t panel = evaluate((operational_state_t)state,
                                                 ACTUATION_ALLOWED, ACTUATION_ALLOWED);
        const int lit = (panel.green != INDICATOR_OFF) + (panel.blue != INDICATOR_OFF) +
                        (panel.yellow != INDICATOR_OFF) + (panel.red != INDICATOR_OFF);
        assert(lit == (state == (int)OP_STATE_SAFE_INACTIVE ? 0 : 1));
    }
}

/*
 * Ningún estado operativo usa el parpadeo rápido.
 *
 * Era el «caliente de más para café» del vapor. Retirado el vapor, ADR-009
 * § 3.10 lo reserva al LED de alarma para la conducción sin orden, y su valor
 * está en que nada más lo use: si un estado lo encendiera, ese aviso dejaría de
 * distinguirse a distancia.
 */
static void test_no_state_blinks_fast(void)
{
    for (int state = 0; state <= (int)OP_STATE_ALARM; state++) {
        const indicator_panel_t panel = evaluate((operational_state_t)state,
                                                 ACTUATION_ALLOWED, ACTUATION_ALLOWED);
        assert(panel.green != INDICATOR_FAST);
        assert(panel.blue != INDICATOR_FAST);
        assert(panel.yellow != INDICATOR_FAST);
        assert(panel.red != INDICATOR_FAST);
    }

    /* Esperar parpadea; lista para preparar es fijo. */
    const indicator_panel_t lista =
        evaluate(OP_STATE_READY, ACTUATION_ALLOWED, ACTUATION_ALLOWED);
    assert(lista.green == INDICATOR_ON);
    const indicator_panel_t calentando =
        evaluate(OP_STATE_COFFEE_HEATING, ACTUATION_ALLOWED, ACTUATION_ALLOWED);
    assert(calentando.yellow == INDICATOR_SLOW);
}

/*
 * El aviso llega aunque la máquina de estados no haya disparado.
 *
 * Es el caso que motiva el aviso: un sensor que se suelta con la cafetera en
 * `READY` corta las salidas por la ruta del guardián y deja el estado intacto.
 */
static void test_a_fault_warns_even_while_the_state_machine_is_calm(void)
{
    const actuation_verdict_t averias[] = {
        ACTUATION_INHIBITED_SIGNAL_NOT_USABLE,
        ACTUATION_INHIBITED_HARD_LIMIT,
        ACTUATION_INHIBITED_DEADLINE_MISSED,
        ACTUATION_INHIBITED_ZERO_CROSS,
    };
    for (size_t index = 0U; index < sizeof(averias) / sizeof(averias[0]); index++) {
        /* Da igual cuál de las dos salidas la declare. */
        const indicator_panel_t por_bomba =
            evaluate(OP_STATE_READY, averias[index], ACTUATION_ALLOWED);
        assert(por_bomba.red == INDICATOR_SLOW);

        const indicator_panel_t por_calentador =
            evaluate(OP_STATE_READY, ACTUATION_ALLOWED, averias[index]);
        assert(por_calentador.red == INDICATOR_SLOW);
    }

    /* Y el resto del panel sigue describiendo el estado. */
    const indicator_panel_t panel =
        evaluate(OP_STATE_READY, ACTUATION_INHIBITED_HARD_LIMIT, ACTUATION_ALLOWED);
    assert(panel.green == INDICATOR_ON);
}

/*
 * Lo que no es una avería no enciende la alarma.
 *
 * Es la mitad que se olvida. `not-commissioned` inhibe hoy y seguirá inhibiendo
 * durante toda la puesta en marcha: si encendiera el aviso, este estaría
 * encendido durante meses y habría dejado de significar algo mucho antes de que
 * ocurriera una avería de verdad.
 */
static void test_normal_service_conditions_never_raise_the_alarm(void)
{
    const actuation_verdict_t normales[] = {
        ACTUATION_ALLOWED,
        ACTUATION_INHIBITED_STARTING_UP,
        ACTUATION_INHIBITED_NOT_AUTHORIZED,
        ACTUATION_INHIBITED_NOT_COMMISSIONED,
        ACTUATION_INHIBITED_DUTY_LIMIT,
        ACTUATION_INHIBITED_RESTING,
    };
    for (size_t index = 0U; index < sizeof(normales) / sizeof(normales[0]); index++) {
        const indicator_panel_t panel =
            evaluate(OP_STATE_READY, normales[index], normales[index]);
        assert(panel.red == INDICATOR_OFF);
        assert(!actuation_verdict_is_fault(normales[index]));
    }
}

/*
 * Una alarma declarada no se degrada a aviso.
 *
 * Fijo significa que la máquina está detenida en `ALARM`; parpadeante, que algo
 * va mal y las salidas están cortadas. Superponer el segundo al primero diría
 * menos de lo que se sabe.
 */
static void test_a_declared_alarm_stays_solid(void)
{
    const indicator_panel_t panel =
        evaluate(OP_STATE_ALARM, ACTUATION_INHIBITED_HARD_LIMIT,
                 ACTUATION_INHIBITED_SIGNAL_NOT_USABLE);
    assert(panel.red == INDICATOR_ON);
}

/* Los dos parpadeos tienen el periodo y el ciclo de trabajo declarados. */
static void test_the_blink_patterns_have_the_declared_timing(void)
{
    assert(indicator_pattern_is_lit(INDICATOR_ON, 0U));
    assert(indicator_pattern_is_lit(INDICATOR_ON, 123456U));
    assert(!indicator_pattern_is_lit(INDICATOR_OFF, 0U));
    assert(!indicator_pattern_is_lit(INDICATOR_OFF, 123456U));

    /* Lento: 1 s de periodo, medio encendido. */
    assert(indicator_pattern_is_lit(INDICATOR_SLOW, 0U));
    assert(indicator_pattern_is_lit(INDICATOR_SLOW, 499U));
    assert(!indicator_pattern_is_lit(INDICATOR_SLOW, 500U));
    assert(!indicator_pattern_is_lit(INDICATOR_SLOW, 999U));
    assert(indicator_pattern_is_lit(INDICATOR_SLOW, 1000U));

    /* Rápido: 250 ms de periodo, cuatro veces más deprisa. */
    assert(indicator_pattern_is_lit(INDICATOR_FAST, 0U));
    assert(indicator_pattern_is_lit(INDICATOR_FAST, 124U));
    assert(!indicator_pattern_is_lit(INDICATOR_FAST, 125U));
    assert(indicator_pattern_is_lit(INDICATOR_FAST, 250U));

    /* El rápido se distingue del lento a simple vista: en un segundo, el lento
     * enciende una vez y el rápido cuatro. */
    uint32_t lento = 0U;
    uint32_t rapido = 0U;
    bool lento_previo = false;
    bool rapido_previo = false;
    for (uint64_t ms = 0U; ms < 1000U; ms++) {
        const bool l = indicator_pattern_is_lit(INDICATOR_SLOW, ms);
        const bool r = indicator_pattern_is_lit(INDICATOR_FAST, ms);
        if (l && !lento_previo) {
            lento++;
        }
        if (r && !rapido_previo) {
            rapido++;
        }
        lento_previo = l;
        rapido_previo = r;
    }
    assert(lento == 1U);
    assert(rapido == 4U);
}

/*
 * La fase del parpadeo no depende del instante en que cambió el estado.
 *
 * Se calcula por módulo sobre tiempo monotónico a propósito. Si se reiniciara en
 * cada transición, un estado que oscilara rápido produciría un LED aparentemente
 * fijo, que es justo lo contrario de lo que debe comunicar.
 */
static void test_the_blink_phase_is_absolute(void)
{
    /* El mismo instante da el mismo resultado, se haya llegado como se haya
     * llegado: la función no tiene memoria. */
    for (uint64_t ms = 0U; ms < 4000U; ms += 37U) {
        assert(indicator_pattern_is_lit(INDICATOR_SLOW, ms) ==
               indicator_pattern_is_lit(INDICATOR_SLOW, ms + INDICATOR_SLOW_PERIOD_MS));
        assert(indicator_pattern_is_lit(INDICATOR_FAST, ms) ==
               indicator_pattern_is_lit(INDICATOR_FAST, ms + INDICATOR_FAST_PERIOD_MS));
    }
}

/* Una petición inexistente avisa, no calla. */
/* Un pulsador retenido avisa con el parpadeo, sin pisar una alarma declarada ni
 * apagar lo que dice el estado. */
static void test_a_held_button_warns_without_hiding_the_state(void)
{
    const indicator_request_t ready = {.state = OP_STATE_READY,
                                       .pump = ACTUATION_ALLOWED,
                                       .heater = ACTUATION_ALLOWED,
                                       .panel_held = true};
    const indicator_panel_t panel = indicator_panel_evaluate(&ready);
    assert(panel.red == INDICATOR_SLOW);
    assert(panel.green == INDICATOR_ON);

    const indicator_request_t alarm = {.state = OP_STATE_ALARM,
                                       .pump = ACTUATION_ALLOWED,
                                       .heater = ACTUATION_ALLOWED,
                                       .panel_held = true};
    assert(indicator_panel_evaluate(&alarm).red == INDICATOR_ON);
}

/* La tara hecha destella el verde, la rechazada destella rojo, y pasado
 * el destello el panel vuelve a lo que dice el estado. */
static void test_the_tare_flashes_and_then_gets_out_of_the_way(void)
{
    indicator_request_t request = {.state = OP_STATE_READY,
                                   .pump = ACTUATION_ALLOWED,
                                   .heater = ACTUATION_ALLOWED,
                                   .tare = INDICATOR_TARE_TAKEN,
                                   .tare_age_ms = 0U};
    indicator_panel_t panel = indicator_panel_evaluate(&request);
    assert(panel.green == INDICATOR_ON);
    assert(panel.blue == INDICATOR_OFF && panel.yellow == INDICATOR_OFF);
    assert(panel.red == INDICATOR_OFF);

    request.tare_age_ms = INDICATOR_TARE_FLASH_PERIOD_MS / 2U;
    panel = indicator_panel_evaluate(&request);
    assert(panel.green == INDICATOR_OFF); /* el destello apaga el verde fijo de «lista» */

    request.tare_age_ms = INDICATOR_TARE_FLASH_MS;
    panel = indicator_panel_evaluate(&request);
    assert(panel.green == INDICATOR_ON && panel.blue == INDICATOR_OFF &&
           panel.yellow == INDICATOR_OFF);

    request.tare = INDICATOR_TARE_REFUSED;
    request.tare_age_ms = 0U;
    panel = indicator_panel_evaluate(&request);
    assert(panel.red == INDICATOR_ON);
    assert(panel.green == INDICATOR_ON && panel.yellow == INDICATOR_OFF);

    /* Una alarma declarada no se interrumpe por una tara rechazada. */
    request.state = OP_STATE_ALARM;
    request.tare_age_ms = INDICATOR_TARE_FLASH_PERIOD_MS / 2U;
    assert(indicator_panel_evaluate(&request).red == INDICATOR_ON);
}

static void test_a_missing_request_warns(void)
{
    const indicator_panel_t panel = indicator_panel_evaluate(NULL);
    assert(panel.red == INDICATOR_SLOW);
    assert(panel.green == INDICATOR_OFF);
    assert(panel.blue == INDICATOR_OFF);
    assert(panel.yellow == INDICATOR_OFF);
}

/*
 * Un panel recién puesto a ceros está apagado.
 *
 * El cero de `indicator_pattern_t` es «apagado» a propósito: una estructura sin
 * escribir no puede afirmar que la máquina está lista ni que hay una alarma.
 */
static void test_a_zeroed_panel_claims_nothing(void)
{
    indicator_panel_t panel;
    memset(&panel, 0, sizeof(panel));
    assert(panel.green == INDICATOR_OFF);
    assert(panel.blue == INDICATOR_OFF);
    assert(panel.yellow == INDICATOR_OFF);
    assert(panel.red == INDICATOR_OFF);
    assert(!indicator_pattern_is_lit(panel.green, 0U));
}


/* Sin red no se avisa de averías del guardián (el transductor de presión,
 * alimentado de la red, marca avería sin haberla), pero un botón retenido y una
 * alarma declarada se siguen viendo. A cero, «hay red»: los avisos se muestran. */
static void test_without_mains_the_guard_faults_stay_quiet(void)
{
    indicator_request_t request = {.state = OP_STATE_READY,
                                   .pump = ACTUATION_INHIBITED_SIGNAL_NOT_USABLE,
                                   .heater = ACTUATION_ALLOWED,
                                   .mains_absent = true};
    assert(indicator_panel_evaluate(&request).red == INDICATOR_OFF);
    request.mains_absent = false;
    assert(indicator_panel_evaluate(&request).red == INDICATOR_SLOW);

    request.mains_absent = true;
    request.panel_held = true;
    assert(indicator_panel_evaluate(&request).red == INDICATOR_SLOW);

    const indicator_request_t alarm = {.state = OP_STATE_ALARM, .mains_absent = true};
    assert(indicator_panel_evaluate(&alarm).red == INDICATOR_ON);
}



/* Tara corrida: doble destello azul al detectarla y repetido cada 15 s, sin
 * quedar parpadeando entre medio, y sin pisar el azul fijo de un café. */
static void test_a_drifted_tare_reminds_in_blue(void)
{
    indicator_request_t request = {.state = OP_STATE_READY, .pump = ACTUATION_ALLOWED,
                                   .heater = ACTUATION_ALLOWED,
                                   .tare_drifted = true};
    const uint32_t on_at[] = {0U, INDICATOR_TARE_FLASH_PERIOD_MS,
                              INDICATOR_TARE_REMINDER_PERIOD_MS,
                              INDICATOR_TARE_REMINDER_PERIOD_MS + INDICATOR_TARE_FLASH_PERIOD_MS};
    for (size_t i = 0U; i < sizeof(on_at) / sizeof(on_at[0]); ++i) {
        request.tare_drift_age_ms = on_at[i];
        assert(indicator_panel_evaluate(&request).blue == INDICATOR_ON);
    }
    const uint32_t off_at[] = {INDICATOR_TARE_FLASH_PERIOD_MS / 2U, INDICATOR_TARE_FLASH_MS,
                               5000U, INDICATOR_TARE_REMINDER_PERIOD_MS - 1U};
    for (size_t i = 0U; i < sizeof(off_at) / sizeof(off_at[0]); ++i) {
        request.tare_drift_age_ms = off_at[i];
        assert(indicator_panel_evaluate(&request).blue == INDICATOR_OFF);
    }
    request.tare_drift_age_ms = 5000U;
    request.state = OP_STATE_EXTRACTION;
    assert(indicator_panel_evaluate(&request).blue == INDICATOR_ON);
    assert(indicator_panel_evaluate(&request).green == INDICATOR_OFF);
}

/* Con un firmware bajando, azul y amarillo parpadean juntos —ningún estado
 * enciende dos colores— salvo con la alarma declarada, que sigue fija; un
 * rechazo destella el rojo encima. */
static void test_a_firmware_update_shows_two_colours(void)
{
    indicator_request_t request = {.state = OP_STATE_SAFE_INACTIVE,
                                   .pump = ACTUATION_INHIBITED_STARTING_UP,
                                   .heater = ACTUATION_INHIBITED_STARTING_UP,
                                   .firmware_updating = true,
                                   .tare_drifted = true,
                                   .tare_drift_age_ms = 0U};
    indicator_panel_t panel = indicator_panel_evaluate(&request);
    assert(panel.blue == INDICATOR_SLOW && panel.yellow == INDICATOR_SLOW);
    assert(panel.green == INDICATOR_OFF && panel.red == INDICATOR_OFF);

    request.state = OP_STATE_ALARM;
    panel = indicator_panel_evaluate(&request);
    assert(panel.red == INDICATOR_ON);
    assert(panel.blue == INDICATOR_OFF && panel.yellow == INDICATOR_OFF);

    request.state = OP_STATE_SAFE_INACTIVE;
    request.tare = INDICATOR_TARE_REFUSED;
    request.tare_age_ms = 0U;
    panel = indicator_panel_evaluate(&request);
    assert(panel.red == INDICATOR_ON);
    assert(panel.blue == INDICATOR_SLOW && panel.yellow == INDICATOR_SLOW);
}

int main(void)
{
    test_every_state_lights_what_the_table_says();
    test_one_colour_per_state_and_never_dark_while_in_use();
    test_no_state_blinks_fast();
    test_a_fault_warns_even_while_the_state_machine_is_calm();
    test_normal_service_conditions_never_raise_the_alarm();
    test_a_declared_alarm_stays_solid();
    test_the_blink_patterns_have_the_declared_timing();
    test_the_blink_phase_is_absolute();
    test_a_held_button_warns_without_hiding_the_state();
    test_the_tare_flashes_and_then_gets_out_of_the_way();
    test_a_missing_request_warns();
    test_a_zeroed_panel_claims_nothing();
    test_without_mains_the_guard_faults_stay_quiet();
    test_a_drifted_tare_reminds_in_blue();
    test_a_firmware_update_shows_two_colours();
    printf("test_indicator_panel: 15 pruebas correctas\n");
    return 0;
}
