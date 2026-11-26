#pragma once

/*
 * Lectura del HX711 de la báscula de ensayo.
 *
 * Es la envoltura de hardware de `scale_reading`: habla el protocolo de dos
 * hilos, publica lo leído y ofrece la tara. Toda la interpretación —extensión de
 * signo, saturación, conversión a masa, estabilidad— vive en el módulo puro, que
 * se prueba en el anfitrión.
 *
 * ## La báscula no es parte de la cafetera
 *
 * Es instrumentación para la campaña de caudal de ADR-006. **Su ausencia no
 * afecta a nada**: no entra en el guardián de actuación, no inhibe salidas y no
 * participa en ninguna decisión de control. Si no está conectada, la tarea
 * publica lecturas «no adquiridas», que es información honesta, y el resto del
 * firmware se comporta igual que si no existiera.
 *
 * ## El detalle del protocolo que hay que respetar
 *
 * El HX711 no tiene reloj propio: se le entrega con `SCK` y responde por `DOUT`,
 * un bit por pulso, veinticuatro bits con el más significativo primero. `DOUT`
 * en nivel bajo anuncia que hay conversión lista.
 *
 * Lo que obliga a tener cuidado es que **`SCK` en alto durante más de 60 µs
 * apaga el integrado**. Como los pulsos los genera una tarea, cualquier
 * expropiación a mitad de un pulso podría superar ese tiempo: la lectura se
 * corrompería y el chip se dormiría, y desde fuera parecería una báscula
 * averiada. Por eso la ráfaga de veinticinco pulsos se emite dentro de una
 * sección crítica. Dura unos cien microsegundos, que a 10 muestras por segundo
 * es un coste despreciable.
 *
 * Tras los veinticuatro bits se emiten pulsos adicionales que eligen canal y
 * ganancia **de la conversión siguiente**: uno para el canal A con ganancia 128,
 * que es el que quiere una célula de carga.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "scale_reading.h"

/*
 * Periodo de sondeo.
 *
 * El HX711 de las tarjetas habituales entrega 10 muestras por segundo, de modo
 * que sondear cada 50 ms encuentra cada conversión con holgura sin ocupar el
 * procesador esperando.
 */
#define SCALE_POLL_PERIOD_MS 50U

/* Cinco periodos de conversión: lo bastante para no marcar vieja una lectura
 * por un sondeo perdido, y lo bastante corto para notar una báscula muerta. */
#define SCALE_MAXIMUM_AGE_MS 500U

typedef struct {
    bool present;              /* ha habido al menos una conversión */
    scale_reading_t reading;
    bool stable;               /* la ventana declara la lectura asentada */
    uint32_t spread_counts;    /* recorrido de la ventana, para el diagnóstico */
    uint32_t conversions;      /* conversiones leídas desde el arranque */
    uint32_t timeouts;         /* sondeos sin conversión lista */
    const char *calibration_id;
} scale_status_t;

/*
 * Configura los pines y arranca el sondeo.
 *
 * `SCALE_SCK` queda como salida en bajo y `SCALE_DOUT` como entrada. No se
 * activa *pull-up*: la salida del HX711 es totémica y forzarla sería cargarla
 * sin motivo. Si no hay báscula conectada, `DOUT` queda flotante y la tarea
 * publicará lecturas no adquiridas, que es lo correcto.
 */
esp_err_t hx711_runtime_start(void);

bool hx711_runtime_get_status(scale_status_t *status);

/*
 * Toma la tara con la media de la ventana de estabilidad.
 *
 * Devuelve `ESP_ERR_INVALID_STATE` si la lectura no está asentada, en lugar de
 * tomarla igualmente: una tara mal tomada desplaza en silencio todo lo que se
 * pese después, y ese error es indistinguible de una célula descalibrada.
 */
esp_err_t hx711_runtime_tare(void);

/*
 * Fija el factor de conversión medido con una masa patrón.
 *
 * `counts_per_gram` no positivo deja la báscula sin calibrar, que es el estado
 * de partida: entonces se publican cuentas y se declara que la masa no está
 * disponible. `identifier` viaja al registro para que se sepa con qué se
 * convirtió.
 */
esp_err_t hx711_runtime_set_calibration(float counts_per_gram, const char *identifier);
