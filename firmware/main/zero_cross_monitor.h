#pragma once

/*
 * Vigilancia de la señal de cruce por cero de la bomba (ADR-007 § 3.5).
 *
 * `PUMP_ZC` cumple dos papeles que conviene no confundir. Ofrece las
 * oportunidades sobre las que el planificador PSM decide actuar u omitir, y es
 * además una **condición de inhibición**: perderla, o que su frecuencia salga
 * del intervalo permitido, obliga a llevar `PUMP_CMD` a bajo. Este módulo
 * atiende el segundo papel; el primero lo resuelve `psm_scheduler`.
 *
 * Es puro: recibe marcas de tiempo y responde, sin tocar GPIO ni conocer el
 * reloj del sistema. Eso permite probarlo en el anfitrión con secuencias que en
 * banco costaría provocar —una señal que desaparece a mitad, un rebote, una
 * frecuencia que deriva— y hace que la parte difícil de acertar no viva dentro
 * de una rutina de interrupción.
 *
 * **La frecuencia esperada no se fija aquí, y es deliberado.** El módulo dimmer
 * es una unidad externa cuyo detector puede entregar un pulso por ciclo de red o
 * uno por semiciclo, y el esquema de interconexión deja ese punto abierto hasta
 * inspeccionar la unidad comprada. Codificar 60 o 120 Hz sería inventar un dato
 * que nadie ha medido, así que los límites se reciben y `zero_cross_limits_for`
 * los construye desde la frecuencia nominal que se confirme en banco.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t minimum_period_us;  /* por debajo, es un rebote y no un cruce */
    uint32_t maximum_period_us;  /* por encima, la frecuencia salió de rango */
    uint32_t timeout_us;         /* sin eventos durante este tiempo, señal perdida */
    uint32_t settling_events;    /* periodos buenos seguidos antes de declararla apta */
    /* Un rechazo por demasiado corto dentro de esta ventana tras un cruce
     * aceptado es un rebote del mismo cruce y no declara la señal inestable.
     * 0 desactiva la ventana: todo rechazo reinicia la cuenta, como antes. */
    uint32_t bounce_window_us;
} zero_cross_limits_t;

/*
 * Ventana de rebote por omisión.
 *
 * Medida el 2026-10-08 en PCB-CONTROL con la red real: **todos** los rechazos
 * llegaban entre 48 y 65 µs después del cruce aceptado, cerca del 7 % de los
 * flancos, dentro del pulso bajo de unos 100 µs que marca cada cruce. Cada uno
 * reiniciaba la cuenta de periodos buenos y la señal alternaba entre apta y no
 * apta de un latido a otro. 1 ms deja más de quince veces de margen sobre lo
 * medido y sigue muy por debajo del semiciclo de 8,3 ms, de modo que nada que
 * llegue después puede confundirse con un rebote. Provisional: sale de una sola
 * sesión de medida.
 */
#define ZERO_CROSS_BOUNCE_WINDOW_US 1000U

typedef struct {
    zero_cross_limits_t limits;
    uint64_t last_event_us;
    uint32_t last_period_us;    /* último periodo **aceptado** */
    uint32_t last_interval_us;  /* último intervalo medido, aceptado o no */
    uint32_t consecutive_good;
    uint32_t total_events;
    uint32_t rejected_events;
    /* Diagnóstico de los rechazados por demasiado cortos (puesta en servicio,
     * 3.2): cuántos hubo y a qué distancia del último cruce aceptado llegaron.
     * Distingue un rebote pegado al cruce de ruido repartido por el semiciclo,
     * que piden remedios distintos. No intervienen en ninguna decisión. */
    uint32_t short_rejections;
    uint32_t shortest_short_us;  /* 0 mientras no haya ninguno */
    uint32_t longest_short_us;
    bool last_was_bounce;
    bool has_previous;
} zero_cross_monitor_t;

/*
 * Construye límites desde la frecuencia nominal y una tolerancia relativa.
 *
 * `tolerance_fraction` de 0,1 admite un ±10 % alrededor del nominal. El plazo de
 * pérdida se fija en tres periodos nominales: lo bastante corto para cortar
 * pronto y lo bastante largo para no disparar por un solo pulso perdido.
*
 * La ventana de rebote es `ZERO_CROSS_BOUNCE_WINDOW_US`.
 */
zero_cross_limits_t zero_cross_limits_for(float nominal_hz,
                                          float tolerance_fraction,
                                          uint32_t settling_events);

bool zero_cross_monitor_configure(zero_cross_monitor_t *monitor,
                                  const zero_cross_limits_t *limits);

/* Olvida lo observado y conserva los límites. */
void zero_cross_monitor_reset(zero_cross_monitor_t *monitor);

/*
 * Registra un flanco. Pensada para llamarse desde una rutina de interrupción:
 * solo aritmética entera, sin bloqueos ni coma flotante.
 *
 * Devuelve true si el intervalo respecto al flanco anterior cae dentro de los
 * límites. Un intervalo demasiado corto se cuenta como rechazado y **no**
 * reemplaza la referencia temporal: un rebote no debe desplazar el instante
 * contra el que se mide el siguiente periodo real.
 */
bool zero_cross_monitor_event(zero_cross_monitor_t *monitor, uint64_t timestamp_us);

/*
 * Verdadero si el último flanco registrado fue un rebote dentro de la ventana:
 * no es un cruce, no cambia nada y quien actúa en cada cruce debe ignorarlo.
 */
bool zero_cross_monitor_last_was_bounce(const zero_cross_monitor_t *monitor);

/*
 * Decide si la señal puede autorizar actuación en este instante.
 *
 * Exige dos cosas a la vez: que se hayan encadenado suficientes periodos buenos
 * y que no haya transcurrido el plazo de pérdida desde el último flanco. La
 * segunda es la que detiene la bomba cuando la señal desaparece, y por eso
 * necesita el instante actual: sin él, una señal muerta seguiría pareciendo
 * válida indefinidamente por lo que se observó antes de morir.
 */
bool zero_cross_monitor_usable(const zero_cross_monitor_t *monitor, uint64_t now_us);

/*
 * Frecuencia del último periodo aceptado, en hercios; 0 si no hay ninguno.
 *
 * Es el último periodo y no una media: lo que se vigila es que cada intervalo
 * esté en rango, y una media podría quedar dentro mientras los periodos
 * individuales alternan fuera de él.
 */
float zero_cross_monitor_frequency_hz(const zero_cross_monitor_t *monitor);

uint32_t zero_cross_monitor_last_period_us(const zero_cross_monitor_t *monitor);

/*
 * Último intervalo medido, esté o no dentro de los límites, y la frecuencia que
 * le corresponde. Existen para la puesta en marcha.
 *
 * Sin ellos, una señal cuya frecuencia no coincide con la esperada se rechazaría
 * entera y el diagnóstico solo podría decir «no es apta», sin decir qué llega.
 * Eso deja sin el único dato que permite corregir la hipótesis sobre el
 * detector: si entrega un pulso por ciclo o por semiciclo. Un intervalo
 * rechazado por demasiado corto —un rebote— tampoco se registra aquí, porque no
 * describe la señal sino su ruido.
 */
uint32_t zero_cross_monitor_last_interval_us(const zero_cross_monitor_t *monitor);
float zero_cross_monitor_observed_frequency_hz(const zero_cross_monitor_t *monitor);

uint32_t zero_cross_monitor_total_events(const zero_cross_monitor_t *monitor);
uint32_t zero_cross_monitor_rejected_events(const zero_cross_monitor_t *monitor);
uint32_t zero_cross_monitor_short_rejections(const zero_cross_monitor_t *monitor);
uint32_t zero_cross_monitor_shortest_short_us(const zero_cross_monitor_t *monitor);
uint32_t zero_cross_monitor_longest_short_us(const zero_cross_monitor_t *monitor);
