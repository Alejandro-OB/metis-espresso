#pragma once

/*
 * Captura de los flancos de `PUMP_ZC` y publicación de su estado.
 *
 * Es la envoltura de hardware de `zero_cross_monitor` y de `psm_scheduler`:
 * instala la rutina de interrupción sobre el pin, le entrega cada flanco con su
 * marca de tiempo, y en el mismo instante decide con el planificador si esa
 * oportunidad se aprovecha y acciona `PUMP_CMD` en consecuencia. Toda la lógica
 * que puede equivocarse vive en los dos módulos puros, que se prueban en el
 * anfitrión; aquí solo queda lo que obliga a tener hardware delante.
 *
 * Que el planificador viva dentro de la interrupción no es comodidad: las
 * oportunidades llegan cada 8,33 ms y el lazo de control corre cada 20 ms, de
 * modo que una tarea no podría decidir sobre cada cruce por separado, que es
 * justo lo que ADR-007 § 3.4 pide.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "zero_cross_monitor.h"

typedef struct {
    bool usable;              /* la señal autoriza actuación en este instante */
    float frequency_hz;       /* del último periodo aceptado; 0 si no hay */
    /* Del último intervalo medido, aceptado o no. Es el dato de la puesta en
     * marcha: si la hipótesis de frecuencia fuera errónea, `frequency_hz` sería
     * cero y solo este diría qué está llegando. */
    float observed_frequency_hz;
    uint32_t last_interval_us;
    uint32_t last_period_us;
    uint32_t total_events;
    uint32_t rejected_events;
    /* Rechazados por demasiado cortos y su distancia al cruce anterior. */
    uint32_t short_rejections;
    uint32_t shortest_short_us;
    uint32_t longest_short_us;
    uint32_t age_ms;          /* desde el último flanco */
} zero_cross_snapshot_t;

/*
 * Límites de partida: **un pulso por semiciclo** de una red de 60 Hz, 120 Hz.
 *
 * Confirmado el 2026-09-15 con la documentación del módulo adquirido, que
 * declara 8,23 ms en alto y unos 100 us en bajo por pulso. La comprobación en
 * banco sigue siendo necesaria para verificar la unidad concreta, y para eso el
 * diagnóstico publica la frecuencia observada además de la aceptada: si los
 * periodos quedaran fuera de rango, la señal no autorizaría —que es lo
 * correcto— pero el dato seguiría siendo legible.
 */
zero_cross_limits_t zero_cross_runtime_default_limits(void);

/*
 * Habilita la interrupción sobre `PUMP_ZC` con los límites dados.
 *
 * El pin ya está configurado como entrada con *pull-down* durante el arranque;
 * aquí solo se le añade la interrupción por flanco de subida. Si el módulo real
 * marcara el cruce con flanco de bajada, es el único punto que habría que
 * cambiar.
 */
esp_err_t zero_cross_runtime_start(const zero_cross_limits_t *limits);

/* Respuesta directa a la pregunta que hace el guardián de actuación. */
bool zero_cross_runtime_usable(void);

/*
 * Entrega al planificador PSM la densidad que pide el controlador.
 *
 * `allowed` es el veredicto del guardián, no una preferencia del lazo: mientras
 * sea falso, la rutina de interrupción lleva `PUMP_CMD` a bajo en cada cruce y
 * no ofrece la oportunidad al planificador. Una oportunidad inhibida no es una
 * oportunidad, y contarla falsearía la correspondencia entre densidad pedida y
 * actuaciones emitidas que ADR-007 § 7.2 obliga a verificar.
 */
void zero_cross_runtime_set_pump_demand(uint8_t density, bool allowed);

/*
 * Estado del planificador, para el registro del lazo.
 *
 * Se lee bajo la misma sección crítica que usa la interrupción, de modo que los
 * tres contadores corresponden al mismo instante y no a tres cruces distintos.
 */
bool zero_cross_runtime_get_pump_statistics(uint32_t *opportunities,
                                            uint32_t *emitted,
                                            uint32_t *residue,
                                            uint8_t *density);

/* Olvida el acumulador y los contadores. Se invoca al habilitar el lazo. */
void zero_cross_runtime_reset_pump(void);

/* Estado completo, para el diagnóstico y el registro del lazo. */
bool zero_cross_runtime_get_snapshot(zero_cross_snapshot_t *snapshot);
