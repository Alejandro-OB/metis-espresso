#pragma once

/*
 * Qué enciende cada LED del mando.
 *
 * El requisito no pide un adorno: obliga a **informar al barista** cuando el
 * sistema detecte sobretemperatura, sensor crítico inválido, tarea crítica
 * detenida o condición insegura. Los cuatro LED de `PCB-MANDO` son el único
 * canal que no depende de la red ni de que haya alguien mirando una pantalla.
 *
 * El módulo es puro: recibe el estado operativo y los dos veredictos del
 * guardián, y devuelve qué patrón corresponde a cada LED. No toca GPIO ni conoce
 * el reloj; el parpadeo se resuelve aparte, con el tiempo que le den. Eso permite
 * probar en el anfitrión la tabla completa, que es donde puede haber un error
 * silencioso: un LED mal asignado no rompe nada y nadie se entera hasta que hace
 * falta.
 *
 * ## Seis estados y cuatro LED: un color, un significado
 *
 * Desde el 2026-10-17 (petición del usuario, que no encontraba claro el código
 * anterior) cada estado enciende **un solo color**, como un semáforo, y el
 * barista mira una luz y no una combinación:
 *
 * - **amarillo** es esperar: la caldera está calentando;
 * - **verde** es adelante: lista para preparar;
 * - **azul** es que está preparando: café o purgado en marcha;
 * - **rojo** es que algo pasa.
 *
 * | Estado | verde | azul | amarillo | rojo |
 * |---|---|---|---|---|
 * | `SAFE_INACTIVE`   | — | — | — | — |
 * | `COFFEE_HEATING`  | — | — | lento | — |
 * | `READY`           | fijo | — | — | — |
 * | `EXTRACTION`      | — | fijo | — | — |
 * | `MANUAL_PURGE`    | — | fijo | — | — |
 * | `ALARM`           | — | — | — | fijo |
 * | tara corrida      | — | doble destello cada 15 s | — | — |
 * | firmware nuevo    | — | lento | lento | — |
 *
 * **Descartado: verde fijo solo con el metal en la banda** (2026-10-17 al
 * 2026-10-18). Se puso para la prueba de las esperas, con el verde parpadeando
 * en `READY` fuera de referencia ±0,6 °C. Se retiró (usuario, 2026-10-18)
 * porque esa prueba mostró que la banda del metal no cambia el agua que sale:
 * con esperas de 1 a 20 min y el metal entre 91,1 y 93,0 °C el agua varió
 * dentro de la dispersión entre usos iguales (`temperatura-cierre-2026-10-17.md`).
 * Un aviso que no cambia lo que el barista debe hacer solo lo hace esperar.
 *
 * Los colores son los de `PCB-MANDO` (`fabrication/pcb-mando-2026-09-20`):
 * verde en `LED_SAFE`, azul en `LED_READY`, amarillo en `LED_BREW` y rojo en
 * `LED_ALARM`. Los campos llevan el color y no el nombre de la red, porque el
 * significado de cada red ya no es el de la serigrafía.
 *
 * **Descartado: el código acumulativo anterior**, en que el verde quedaba fijo
 * mientras la máquina estuviera encendida y los demás se sumaban encima. Decía
 * más de una cosa a la vez y había que leer la combinación. Lo que aquel código
 * protegía se conserva: con la caldera caliente el panel nunca está oscuro,
 * porque calentar, lista, preparar y alarma encienden cada uno su color, y solo
 * el reposo lo deja apagado (recién enchufada, la cafetera no debe parecer
 * encendida, como pidió el usuario el 2026-10-07).
 *
 * ## La actualización del firmware se ve en el panel
 *
 * Con la cafetera sellada no hay consola (ADR-021), y quien hizo el gesto no
 * tiene otra forma de saber que la placa está bajando un firmware y que no debe
 * desenchufarla. Mientras dure, **azul y amarillo parpadean juntos**: ningún
 * estado enciende dos colores, de modo que no se confunde con calentar. Si el
 * gesto se rechaza, destella el rojo como ante cualquier botón negado. Al
 * terminar la placa se reinicia y la prueba de los LED del arranque dice que
 * volvió.
 *
 * El parpadeo rápido no lo usa ningún estado: ADR-009 § 3.10 lo reserva al rojo
 * para la conducción sin orden.
 *
 * ## La alarma no es solo el estado de alarma
 *
 * El rojo también parpadea **cuando el guardián inhibe por avería
 * aunque la máquina de estados no haya disparado**: un sensor que se suelta
 * mientras la máquina está lista es exactamente el caso que hay que avisar, y
 * el estado operativo puede tardar en reflejarlo o no reflejarlo nunca.
 *
 * Fijo y parpadeante se distinguen a propósito. Fijo es una alarma declarada,
 * con la máquina detenida en `ALARM`. Parpadeante es «algo va mal y las salidas
 * están cortadas», que es un aviso, no una parada.
 *
 * El mismo parpadeo avisa de un **pulsador retenido**. No corta nada —un botón
 * pegado ya no produce más órdenes que la de soltarse—, pero el barista tiene
 * que saber que ese botón no le va a responder, y sin aviso lo atribuiría a la
 * máquina.
 *
 * ## La tara se confirma con un destello
 *
 * La tara no cambia el estado de la máquina, y sin aviso el barista no sabría si
 * la taza quedó descontada. Hecha, **destella el verde** dos veces (el usuario
 * lo cambió el 2026-10-17, con los colores nuevos: verde es «hecho, adelante»;
 * antes destellaban azul y amarillo); rechazada, destella el **rojo**. El mismo
 * destello rojo dice «no se pudo» ante cualquier botón negado: un encendido
 * sin red o una orden que la máquina rechazó por no estar lista (usuario,
 * 2026-10-17); `indicator_runtime` elige el gesto más reciente. El
 * destello se superpone durante `INDICATOR_TARE_FLASH_MS` y
 * después el panel vuelve a lo que dice el estado. Una alarma declarada, fija, no
 * se interrumpe por una tara.
 *
 * **Lo que no enciende la alarma** importa tanto como lo que la enciende. Que el
 * control no esté puesto en servicio no es una avería, y hoy no lo está ni lo
 * estará en meses: tratarlo como tal dejaría el LED encendido de forma
 * permanente y le quitaría todo significado justo antes de que hiciera falta.
 * `actuation_verdict_is_fault` traza esa frontera.
 */

#include <stdbool.h>
#include <stdint.h>

#include "actuation_guard.h"
#include "operational_fsm.h"

/* Periodos de los dos parpadeos, en milisegundos de ciclo completo. */
#define INDICATOR_SLOW_PERIOD_MS 1000U
#define INDICATOR_FAST_PERIOD_MS 250U

/*
 * Destello de la tara: dos parpadeos en este tiempo, cada uno de la mitad de
 * `INDICATOR_TARE_FLASH_PERIOD_MS`. Lo bastante corto para no confundirse con un
 * estado y lo bastante largo para verse de reojo. Provisional, sin medida.
 */
#define INDICATOR_TARE_FLASH_MS 800U
#define INDICATOR_TARE_FLASH_PERIOD_MS 400U

/*
 * Recordatorio de tara corrida: el mismo doble destello de la tara, en azul,
 * repetido cada este tiempo mientras siga corrida. El usuario no quiso un azul
 * parpadeando sin parar (2026-10-17) y eligió el recordatorio espaciado frente a
 * un solo destello, que se pierde si no se mira en ese instante. Los 15 s los
 * fijó el equipo; no salen de una medida.
 */
#define INDICATOR_TARE_REMINDER_PERIOD_MS 15000U

/*
 * Resultado de la última tara pedida desde el mando. El cero es «ninguna»: un
 * panel recién puesto a cero no anuncia una tara que nadie pidió.
 */
typedef enum {
    INDICATOR_TARE_NONE = 0,
    INDICATOR_TARE_TAKEN,
    INDICATOR_TARE_REFUSED,
} indicator_tare_t;

typedef enum {
    /* El cero es «apagado» a propósito: un panel recién puesto a cero no debe
     * afirmar que la máquina está lista ni que hay una alarma. */
    INDICATOR_OFF = 0,
    INDICATOR_ON,
    INDICATOR_SLOW,
    INDICATOR_FAST,
} indicator_pattern_t;

typedef struct {
    indicator_pattern_t green;   /* LED_SAFE */
    indicator_pattern_t blue;    /* LED_READY */
    indicator_pattern_t yellow;  /* LED_BREW */
    indicator_pattern_t red;     /* LED_ALARM */
} indicator_panel_t;

typedef struct {
    operational_state_t state;
    actuation_verdict_t pump;
    actuation_verdict_t heater;
    /* Algún pulsador del mando retenido (`button_hold`, ADR-006 § 7.9). */
    bool panel_held;
    /* No hay red: el detector de cruce por cero no ve la onda. Sin red no se
     * avisa de averías del guardián: el transductor de presión se alimenta de
     * la red y sin ella marca avería aunque no la haya (usuario, 2026-10-17), y
     * sin red nada puede accionarse. Se declara en negativo a propósito: a
     * cero los avisos se muestran, y callarlos exige que alguien haya visto
     * que falta la red. */
    bool mains_absent;
    /* La tara se corrió desde que se hizo (`tare_watch`), y desde hace cuánto.
     * El azul da el doble destello al detectarlo y lo repite cada
     * `INDICATOR_TARE_REMINDER_PERIOD_MS` hasta la siguiente tara. El azul solo
     * se usa mientras sale agua, así que en reposo o en «lista» no se confunde. */
    bool tare_drifted;
    uint32_t tare_drift_age_ms;
    /* Última tara y cuánto hace que se pidió. */
    indicator_tare_t tare;
    uint32_t tare_age_ms;
    /* Bajando o escribiendo un firmware nuevo (ADR-021). */
    bool firmware_updating;
} indicator_request_t;

/* Decide el patrón de los cuatro LED. No modifica nada. */
indicator_panel_t indicator_panel_evaluate(const indicator_request_t *request);

/*
 * Indica si un patrón está encendido en un instante dado.
 *
 * `elapsed_ms` es tiempo monotónico cualquiera, no el transcurrido desde el
 * inicio del parpadeo: los patrones se calculan por módulo sobre él, de modo que
 * un LED no reinicia su fase al cambiar de estado. Si la reiniciara, un estado
 * que oscilara rápido produciría un LED aparentemente fijo.
 */
bool indicator_pattern_is_lit(indicator_pattern_t pattern, uint64_t elapsed_ms);

const char *indicator_pattern_name(indicator_pattern_t pattern);
