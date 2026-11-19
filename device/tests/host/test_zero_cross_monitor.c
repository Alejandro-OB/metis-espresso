/*
 * Pruebas de la vigilancia de cruce por cero (H-09).
 *
 * Aquí se comprueba el papel de `PUMP_ZC` como condición de inhibición: que una
 * señal sana autorice, que una perdida deje de autorizar aunque antes fuera
 * buena, y que un rebote no se confunda con un cruce. Son secuencias que en
 * banco costaría provocar de forma repetible y que, dentro de una rutina de
 * interrupción, no podrían verificarse.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "zero_cross_monitor.h"

/* Un pulso por semiciclo de una red de 60 Hz: 120 Hz, periodo de 8333 µs. La
 * cifra es la hipótesis de trabajo mientras no se inspeccione el dimmer; lo que
 * se prueba es el mecanismo, no ese número. */
#define PERIODO_NOMINAL_US 8333U

static zero_cross_limits_t limites_de_prueba(void)
{
    return zero_cross_limits_for(120.0F, 0.10F, 5U);
}

static void test_limits_are_derived_from_the_nominal_frequency(void)
{
    const zero_cross_limits_t limites = limites_de_prueba();
    /* ±10 % sobre un periodo de 8333 µs. */
    assert(limites.minimum_period_us >= 7400U && limites.minimum_period_us <= 7600U);
    assert(limites.maximum_period_us >= 9100U && limites.maximum_period_us <= 9250U);
    assert(limites.timeout_us >= 24000U && limites.timeout_us <= 25500U);
    assert(limites.settling_events == 5U);

    /* Una configuración imposible no produce límites utilizables. */
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t nulos = zero_cross_limits_for(0.0F, 0.1F, 5U);
    assert(!zero_cross_monitor_configure(&monitor, &nulos));
    const zero_cross_limits_t no_finitos = zero_cross_limits_for(NAN, 0.1F, 5U);
    assert(!zero_cross_monitor_configure(&monitor, &no_finitos));
}

/*
 * Una señal sana autoriza, pero no desde el primer flanco.
 *
 * El primer flanco no define ningún periodo —solo fija el origen— y hacen falta
 * varios periodos buenos seguidos antes de declarar la señal apta. Habilitar la
 * bomba con un único intervalo medido sería fiarse de una sola muestra para
 * energizar una carga inductiva.
 */
static void test_a_healthy_signal_becomes_usable_after_settling(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t ahora = 1000000U;
    assert(!zero_cross_monitor_event(&monitor, ahora));   /* el primero no cuenta */
    assert(!zero_cross_monitor_usable(&monitor, ahora));

    for (unsigned i = 0U; i < limites.settling_events; ++i) {
        ahora += PERIODO_NOMINAL_US;
        assert(zero_cross_monitor_event(&monitor, ahora));
        /* Antes del último, todavía no basta. */
        if (i + 1U < limites.settling_events) {
            assert(!zero_cross_monitor_usable(&monitor, ahora));
        }
    }
    assert(zero_cross_monitor_usable(&monitor, ahora));

    const float frecuencia = zero_cross_monitor_frequency_hz(&monitor);
    assert(fabsf(frecuencia - 120.0F) < 1.0F);
    assert(zero_cross_monitor_rejected_events(&monitor) == 0U);
}

/*
 * La señal perdida deja de autorizar, por buena que fuera antes.
 *
 * Es la inhibición que exige ADR-007 § 3.5 y la razón de que la consulta reciba
 * el instante actual. Sin él, lo observado antes de que la señal muriera seguiría
 * pareciendo válido para siempre.
 */
static void test_a_lost_signal_stops_authorising(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t ahora = 5000000U;
    zero_cross_monitor_event(&monitor, ahora);
    for (int i = 0; i < 20; ++i) {
        ahora += PERIODO_NOMINAL_US;
        zero_cross_monitor_event(&monitor, ahora);
    }
    assert(zero_cross_monitor_usable(&monitor, ahora));

    /* Justo dentro del plazo todavía autoriza. */
    assert(zero_cross_monitor_usable(&monitor, ahora + limites.timeout_us));
    /* Pasado el plazo, no. Y no hace falta ningún evento nuevo para saberlo. */
    assert(!zero_cross_monitor_usable(&monitor, ahora + limites.timeout_us + 1U));
    assert(!zero_cross_monitor_usable(&monitor, ahora + 10U * limites.timeout_us));
}

/*
 * Un rebote se rechaza y no arrastra al periodo siguiente.
 *
 * Esta es la parte delicada. Si el rebote se adoptara como nuevo origen, el
 * flanco real posterior se mediría contra él y produciría también un intervalo
 * fuera de rango: un único rebote invalidaría dos periodos. Conservando el
 * origen, el siguiente flanco real mide contra el cruce verdadero y vuelve a ser
 * bueno de inmediato.
 */
static void test_a_bounce_is_rejected_without_spoiling_the_next_period(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t origen = 2000000U;
    zero_cross_monitor_event(&monitor, origen);

    /* Rebote: 200 µs después del flanco, muy por debajo del mínimo. */
    assert(!zero_cross_monitor_event(&monitor, origen + 200U));
    assert(zero_cross_monitor_rejected_events(&monitor) == 1U);
    /* El diagnóstico dice a qué distancia del cruce llegó. */
    assert(zero_cross_monitor_short_rejections(&monitor) == 1U);
    assert(zero_cross_monitor_shortest_short_us(&monitor) == 200U);
    assert(zero_cross_monitor_longest_short_us(&monitor) == 200U);
    assert(!zero_cross_monitor_event(&monitor, origen + 900U));
    assert(zero_cross_monitor_shortest_short_us(&monitor) == 200U);
    assert(zero_cross_monitor_longest_short_us(&monitor) == 900U);

    /* El flanco real siguiente llega un periodo después del cruce verdadero, no
     * del rebote, y debe aceptarse. */
    origen += PERIODO_NOMINAL_US;
    assert(zero_cross_monitor_event(&monitor, origen));
    assert(zero_cross_monitor_rejected_events(&monitor) == 2U);
}

/* Un periodo demasiado largo también se rechaza: la frecuencia salió de rango. */
static void test_a_period_that_is_too_long_is_rejected(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t ahora = 3000000U;
    zero_cross_monitor_event(&monitor, ahora);
    for (int i = 0; i < 10; ++i) {
        ahora += PERIODO_NOMINAL_US;
        zero_cross_monitor_event(&monitor, ahora);
    }
    assert(zero_cross_monitor_usable(&monitor, ahora));

    /* Un intervalo del doble del nominal queda fuera del ±10 %. */
    ahora += 2U * PERIODO_NOMINAL_US;
    assert(!zero_cross_monitor_event(&monitor, ahora));
    /* Y el rechazo rompe la racha, de modo que deja de autorizar aunque el
     * flanco acabe de llegar. */
    assert(!zero_cross_monitor_usable(&monitor, ahora));
}

/*
 * Tras una interrupción de la señal, hace falta volver a estabilizar.
 *
 * Una racha rota no se recupera con un solo periodo bueno: se exigen otra vez
 * tantos como diga la configuración. Reanudar la bomba al primer flanco de una
 * señal que acaba de fallar sería confiar en lo que aún no se ha comprobado.
 */
static void test_after_a_disturbance_it_must_settle_again(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t ahora = 4000000U;
    zero_cross_monitor_event(&monitor, ahora);
    for (int i = 0; i < 10; ++i) {
        ahora += PERIODO_NOMINAL_US;
        zero_cross_monitor_event(&monitor, ahora);
    }
    assert(zero_cross_monitor_usable(&monitor, ahora));

    /* Un periodo fuera de rango rompe la racha. */
    ahora += 3U * PERIODO_NOMINAL_US;
    assert(!zero_cross_monitor_event(&monitor, ahora));
    assert(!zero_cross_monitor_usable(&monitor, ahora));

    /* Un solo periodo bueno no basta para volver. */
    ahora += PERIODO_NOMINAL_US;
    assert(zero_cross_monitor_event(&monitor, ahora));
    assert(!zero_cross_monitor_usable(&monitor, ahora));

    /* Completar la cuenta sí. */
    for (unsigned i = 1U; i < limites.settling_events; ++i) {
        ahora += PERIODO_NOMINAL_US;
        assert(zero_cross_monitor_event(&monitor, ahora));
    }
    assert(zero_cross_monitor_usable(&monitor, ahora));
}

/*
 * Una marca de tiempo que no avanza no produce un periodo enorme.
 *
 * La resta de enteros sin signo desbordaría y daría un intervalo astronómico que
 * pasaría por «fuera de rango» por accidente en lugar de por diagnóstico. Se
 * trata explícitamente como rechazo y se reancla el origen.
 */
static void test_a_non_advancing_timestamp_is_handled_explicitly(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    const uint64_t ahora = 6000000U;
    zero_cross_monitor_event(&monitor, ahora);
    assert(!zero_cross_monitor_event(&monitor, ahora));            /* mismo instante */
    assert(!zero_cross_monitor_event(&monitor, ahora - 1000U));    /* hacia atrás */
    assert(zero_cross_monitor_rejected_events(&monitor) == 2U);
    assert(zero_cross_monitor_last_period_us(&monitor) == 0U);
}

/* Reset olvida lo observado y conserva los límites. */
static void test_reset_forgets_observations_and_keeps_limits(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t ahora = 7000000U;
    zero_cross_monitor_event(&monitor, ahora);
    for (int i = 0; i < 10; ++i) {
        ahora += PERIODO_NOMINAL_US;
        zero_cross_monitor_event(&monitor, ahora);
    }
    assert(zero_cross_monitor_usable(&monitor, ahora));

    zero_cross_monitor_reset(&monitor);
    assert(!zero_cross_monitor_usable(&monitor, ahora));
    assert(zero_cross_monitor_total_events(&monitor) == 0U);
    assert(zero_cross_monitor_last_period_us(&monitor) == 0U);

    /* Y sigue configurado: vuelve a estabilizar sin reconfigurar. */
    zero_cross_monitor_event(&monitor, ahora);
    for (unsigned i = 0U; i < limites.settling_events; ++i) {
        ahora += PERIODO_NOMINAL_US;
        assert(zero_cross_monitor_event(&monitor, ahora));
    }
    assert(zero_cross_monitor_usable(&monitor, ahora));
}

/*
 * El módulo no presupone la frecuencia del detector.
 *
 * El dimmer es una unidad externa y el esquema deja abierto si entrega un pulso
 * por ciclo o por semiciclo. La misma lógica debe servir para ambos, de modo que
 * confirmar el dato en banco sea cambiar una configuración y no el código.
 */
static void test_it_serves_either_pulse_per_cycle_or_per_half_cycle(void)
{
    const float nominales[] = {60.0F, 120.0F};
    for (unsigned n = 0U; n < 2U; ++n) {
        zero_cross_monitor_t monitor = {0};
        const zero_cross_limits_t limites = zero_cross_limits_for(nominales[n], 0.10F, 4U);
        assert(zero_cross_monitor_configure(&monitor, &limites));

        const uint64_t periodo = (uint64_t)(1000000.0F / nominales[n]);
        uint64_t ahora = 100000U;
        zero_cross_monitor_event(&monitor, ahora);
        for (unsigned i = 0U; i < limites.settling_events; ++i) {
            ahora += periodo;
            assert(zero_cross_monitor_event(&monitor, ahora));
        }
        assert(zero_cross_monitor_usable(&monitor, ahora));
        assert(fabsf(zero_cross_monitor_frequency_hz(&monitor) - nominales[n]) < 1.0F);
    }
}

/*
 * Una señal fuera de rango sigue siendo medible.
 *
 * Es la prueba que sostiene el procedimiento de puesta en marcha. Si el detector
 * entregara un pulso por ciclo cuando se esperaba uno por semiciclo, todos sus
 * periodos se rechazarían: la señal no autorizaría —que es lo correcto— pero el
 * diagnóstico debe poder decir **qué** está llegando, porque ese es el dato con
 * el que se corrige la hipótesis. Sin esto, solo se sabría que algo falla.
 */
static void test_an_out_of_range_signal_is_still_measurable(void)
{
    zero_cross_monitor_t monitor = {0};
    /* Configurado para 120 Hz, pero la señal real llega a 60 Hz. */
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    const uint64_t periodo_real = 16667U;  /* 60 Hz */
    uint64_t ahora = 900000U;
    zero_cross_monitor_event(&monitor, ahora);
    for (int i = 0; i < 20; ++i) {
        ahora += periodo_real;
        assert(!zero_cross_monitor_event(&monitor, ahora));  /* rechazado, correcto */
    }

    /* No autoriza, que es lo que debe ocurrir. */
    assert(!zero_cross_monitor_usable(&monitor, ahora));
    /* Y no hay periodo aceptado del que informar. */
    assert(zero_cross_monitor_last_period_us(&monitor) == 0U);
    assert(zero_cross_monitor_frequency_hz(&monitor) == 0.0F);

    /* Pero el intervalo observado sí está disponible, y delata la frecuencia
     * real: con él se corrige la hipótesis sin instrumentación adicional. */
    const uint32_t intervalo = zero_cross_monitor_last_interval_us(&monitor);
    assert(intervalo >= 16000U && intervalo <= 17500U);
    const float observada = zero_cross_monitor_observed_frequency_hz(&monitor);
    assert(fabsf(observada - 60.0F) < 1.0F);
}

/* Un rebote no se registra como intervalo observado: describe el ruido, no la
 * señal, y contaminaría el dato con el que se decide la frecuencia real. */
static void test_a_bounce_does_not_pollute_the_observed_interval(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t ahora = 800000U;
    zero_cross_monitor_event(&monitor, ahora);
    ahora += PERIODO_NOMINAL_US;
    assert(zero_cross_monitor_event(&monitor, ahora));
    const uint32_t bueno = zero_cross_monitor_last_interval_us(&monitor);

    /* Rebote 150 us después: se rechaza y no reemplaza el intervalo observado. */
    assert(!zero_cross_monitor_event(&monitor, ahora + 150U));
    assert(zero_cross_monitor_last_interval_us(&monitor) == bueno);
}

/*
 * Un rebote pegado al cruce —lo que se midió en la placa, 48 a 65 µs después—
 * se descarta sin declarar la señal inestable. Antes reiniciaba la cuenta de
 * periodos buenos y la señal alternaba entre apta y no apta.
 */
static void test_a_bounce_next_to_the_crossing_keeps_the_signal_usable(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t t = 1000000U;
    for (int i = 0; i < 8; i++) {
        zero_cross_monitor_event(&monitor, t);
        t += PERIODO_NOMINAL_US;
    }
    t -= PERIODO_NOMINAL_US;
    assert(zero_cross_monitor_usable(&monitor, t + 100U));

    assert(!zero_cross_monitor_event(&monitor, t + 55U));
    assert(zero_cross_monitor_last_was_bounce(&monitor));
    assert(zero_cross_monitor_usable(&monitor, t + 100U));

    /* El cruce siguiente se mide contra el bueno y deja de ser rebote. */
    assert(zero_cross_monitor_event(&monitor, t + PERIODO_NOMINAL_US));
    assert(!zero_cross_monitor_last_was_bounce(&monitor));
    assert(zero_cross_monitor_usable(&monitor, t + PERIODO_NOMINAL_US + 100U));
}

/* Un flanco de más lejos del cruce no es rebote: sí declara la señal inestable. */
static void test_a_short_interval_far_from_the_crossing_still_spoils_the_signal(void)
{
    zero_cross_monitor_t monitor = {0};
    const zero_cross_limits_t limites = limites_de_prueba();
    assert(zero_cross_monitor_configure(&monitor, &limites));

    uint64_t t = 1000000U;
    for (int i = 0; i < 8; i++) {
        zero_cross_monitor_event(&monitor, t);
        t += PERIODO_NOMINAL_US;
    }
    t -= PERIODO_NOMINAL_US;
    assert(!zero_cross_monitor_event(&monitor, t + 3000U));
    assert(!zero_cross_monitor_last_was_bounce(&monitor));
    assert(!zero_cross_monitor_usable(&monitor, t + 3100U));
}

/* Una ventana que alcanzara el periodo mínimo confundiría cruces con rebotes. */
static void test_a_bounce_window_as_long_as_a_period_is_refused(void)
{
    zero_cross_monitor_t monitor = {0};
    zero_cross_limits_t limites = limites_de_prueba();
    limites.bounce_window_us = limites.minimum_period_us;
    assert(!zero_cross_monitor_configure(&monitor, &limites));
    limites.bounce_window_us = 0U;
    assert(zero_cross_monitor_configure(&monitor, &limites));
}

int main(void)
{
    test_a_bounce_next_to_the_crossing_keeps_the_signal_usable();
    test_a_short_interval_far_from_the_crossing_still_spoils_the_signal();
    test_a_bounce_window_as_long_as_a_period_is_refused();
    test_limits_are_derived_from_the_nominal_frequency();
    test_a_healthy_signal_becomes_usable_after_settling();
    test_a_lost_signal_stops_authorising();
    test_a_bounce_is_rejected_without_spoiling_the_next_period();
    test_a_period_that_is_too_long_is_rejected();
    test_after_a_disturbance_it_must_settle_again();
    test_a_non_advancing_timestamp_is_handled_explicitly();
    test_reset_forgets_observations_and_keeps_limits();
    test_it_serves_either_pulse_per_cycle_or_per_half_cycle();
    test_an_out_of_range_signal_is_still_measurable();
    test_a_bounce_does_not_pollute_the_observed_interval();
    printf("test_zero_cross_monitor: todas las comprobaciones pasaron\n");
    return 0;
}
