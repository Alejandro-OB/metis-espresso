#pragma once

/*
 * Lectura del mando físico del barista (RF-003, RF-004).
 *
 * Es la envoltura de hardware de `button_debounce`: muestrea los cuatro
 * pulsadores, les aplica el antirrebote y publica los eventos que la máquina de
 * estados entiende. Toda la lógica que puede equivocarse vive en el módulo puro,
 * que se prueba en el anfitrión.
 *
 * ## Los pulsadores son activos a nivel bajo
 *
 * Las entradas llevan *pull-up* interno y el pulsador cierra a masa, de modo que
 * **un nivel bajo significa pulsado**. La inversión se hace aquí y no en el
 * módulo puro, que recibe el nivel ya interpretado: mezclar la convención
 * eléctrica con la lógica de antirrebote obligaría a recordarla en cada prueba.
 *
 * Esa convención tiene además una consecuencia útil: un cable suelto deja la
 * entrada en alto por el *pull-up*, es decir, en «no pulsado». Un mando
 * desconectado no puede ordenar nada.
 *
 * ## Los dos botones de orden, el de mantener y la tara
 *
 * Encendido y café publican su evento **al pulsar**. El purgado publica dos, al
 * pulsar y al soltar, porque es de mantener: el agua sale mientras el botón
 * esté apretado.
 *
 * El pulsador que fue de vapor es ahora la **tara** de la báscula de ensayo
 * (ADR-006, revisión del 2026-10-06), y desde el 2026-10-07 la tara y el
 * purgado intercambian redes para que la tara quede al final del panel. No publica ningún evento, porque
 * la tara no cambia el estado de la máquina: al pulsar, pide la tara a
 * `hx711_runtime` si `operational_fsm_tare_allowed` la admite en el estado del
 * momento. Durante una extracción o una purga la rechaza, porque el agua está
 * cayendo en la taza que se pesa.
 *
 * ## Convive con el canal de banco
 *
 * `external_hil_runtime` inyecta los mismos eventos por el puerto serie para
 * poder ensayar sin mando. Ambos publican en la misma cola y ninguno estorba al
 * otro: la máquina de estados no distingue de dónde viene un evento, y no debe.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Muestreo cada 5 ms y cuatro muestras de acuerdo: un cambio se acepta en 20 ms.
 *
 * Por debajo del rebote típico de un pulsador no se filtraría nada, y por encima
 * de unos 50 ms la pulsación empezaría a sentirse perezosa. No hay medida del
 * pulsador concreto todavía; si al montarlo resultara más ruidoso, el número que
 * hay que subir es el de muestras, no el periodo.
 */
#define PANEL_SAMPLE_PERIOD_MS 5U
#define PANEL_REQUIRED_SAMPLES 4U

/*
 * Pulsado más allá de este tiempo, encendido, café o tara se declaran retenidos
 * (`button_hold`, ADR-006 § 7.9). **Provisional, sin medida detrás**: ninguna
 * pulsación intencionada de esos tres botones dura tanto, y por debajo de unos
 * segundos se confundiría con un dedo que duda. El purgado no lleva límite
 * porque se usa manteniéndolo.
 */
#define PANEL_HELD_LIMIT_MS 10000U

typedef struct {
    bool power;
    bool brew;
    bool tare;
    bool purge;
} panel_buttons_t;

/* Resultado de la última tara pedida desde el mando; el cero es «ninguna». */
typedef enum {
    PANEL_TARE_NONE = 0,
    PANEL_TARE_TAKEN,
    PANEL_TARE_REFUSED,
} panel_tare_result_t;

typedef struct {
    panel_buttons_t pressed;      /* nivel aceptado de cada pulsador */
    panel_buttons_t held;         /* retenido: apretado al arrancar o más allá del límite */
    uint32_t held_occurrences;    /* episodios de retención, de los cuatro */
    uint32_t purge_held_ms;       /* el purgado es de mantener */
    uint32_t published_events;
    uint32_t dropped_events;      /* la cola de la máquina no los admitió */
    uint32_t tares_taken;
    uint32_t tares_refused;       /* por estado, báscula ausente o lectura sin asentar */
    panel_tare_result_t last_tare;
    uint64_t last_tare_us;        /* instante monotónico de la última tara pedida */
    uint64_t last_power_refused_us; /* último encendido negado por falta de red; 0, ninguno */
    uint32_t power_refusals;
    uint32_t cycles;
} panel_status_t;

/*
 * Arranca el muestreo. Los pines ya se configuran como entrada con *pull-up*
 * durante el diagnóstico de arranque; aquí solo se leen.
 *
 * La primera muestra de cada pulsador se adopta como referencia sin publicar
 * nada: un botón pulsado o encallado en el arranque no puede iniciar una
 * preparación.
 */
esp_err_t panel_runtime_start(void);

bool panel_runtime_get_status(panel_status_t *status);
