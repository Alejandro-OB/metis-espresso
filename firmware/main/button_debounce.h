#pragma once

/*
 * Antirrebote de los pulsadores del mando.
 *
 * Un contacto mecánico no cambia de estado una vez: rebota durante unos
 * milisegundos, y cada rebote leído como pulsación distinta produciría una
 * ráfaga de órdenes. Sobre una máquina cuyo botón de café inicia una extracción,
 * eso no es una molestia estética.
 *
 * El módulo es puro: recibe el nivel ya leído y devuelve si hubo flanco. No sabe
 * qué pin es, ni cuánto tarda entre llamadas, ni qué significa el botón. La
 * lectura de los pines y la publicación de eventos viven en `panel_runtime`.
 *
 * ## La primera muestra no produce flanco, y es lo importante
 *
 * Al arrancar, el primer nivel leído **se adopta como referencia en silencio**.
 * Solo un cambio respecto a él genera un flanco.
 *
 * La razón es concreta y no es de comodidad: si el botón de café estuviera
 * pulsado, encallado o con la pata flotante en el instante del arranque, tratar
 * esa primera lectura como una pulsación **iniciaría una preparación que nadie
 * pidió**. Con esta regla, un botón que arranca pulsado no produce nada hasta
 * que alguien lo suelta y vuelve a pulsarlo, que es una acción física
 * inequívoca.
 *
 * ## Lo que no decide
 *
 * No declara ningún botón averiado. Lleva la cuenta del tiempo que lleva
 * pulsado y la ofrece, pero no fija un umbral a partir del cual sospechar: para
 * el purgado, mantener pulsado es el uso normal, y para los demás no hay medida
 * que respalde un límite concreto. Quien tenga ese criterio lo aplicará sobre el
 * dato: lo hace `button_hold`, que declara un pulsador retenido.
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BUTTON_EDGE_NONE = 0,
    BUTTON_EDGE_PRESSED,
    BUTTON_EDGE_RELEASED,
} button_edge_t;

typedef struct {
    /* Muestras consecutivas de acuerdo que hacen falta para aceptar un cambio. */
    uint32_t required_samples;
    uint32_t sample_period_ms;

    bool candidate;      /* nivel que se está confirmando */
    uint32_t agreeing;   /* cuántas muestras seguidas lo confirman */
    bool pressed;        /* nivel aceptado */
    uint32_t pressed_ms; /* tiempo pulsado, 0 si está suelto */
    bool anchored;       /* ya se tomó la referencia inicial */
} button_debounce_t;

/*
 * Configura el antirrebote.
 *
 * `required_samples` por debajo de dos no filtra nada: aceptar un cambio con una
 * sola muestra es exactamente no tener antirrebote, y se rechaza en lugar de
 * aceptarse en silencio.
 */
bool button_debounce_configure(button_debounce_t *button,
                               uint32_t required_samples,
                               uint32_t sample_period_ms);

/* Olvida lo observado, incluida la referencia inicial. */
void button_debounce_reset(button_debounce_t *button);

/*
 * Entrega una muestra y devuelve el flanco que produce, si produce alguno.
 *
 * `pressed_now` es el nivel **ya interpretado**: verdadero significa pulsado.
 * Los pulsadores del mando son activos a nivel bajo, y esa inversión pertenece a
 * quien lee el pin, no a este módulo.
 */
button_edge_t button_debounce_update(button_debounce_t *button, bool pressed_now);

bool button_debounce_pressed(const button_debounce_t *button);

/* Tiempo que lleva pulsado; 0 si está suelto. Para el purgado, que es de
 * mantener, y para que quien quiera detectar un contacto encallado tenga el
 * dato sin que este módulo invente el umbral. */
uint32_t button_debounce_pressed_ms(const button_debounce_t *button);
