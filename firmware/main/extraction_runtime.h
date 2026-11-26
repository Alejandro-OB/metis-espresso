#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include <stdint.h>

#include "extraction_session.h"
#include "nvs_outbox.h"

typedef struct {
    bool extraction_active;
    bool completed_extraction_available;
    extraction_summary_t active;
    extraction_summary_t last_completed;
    /* Registros cerrados que no pudieron encolarse; se reintentan al cerrar el
     * siguiente y al fijar la hora de pared. */
    uint32_t pending_without_clock;
    /* Registros perdidos porque la espera de hora alcanzó su capacidad. */
    uint32_t dropped_without_clock;
    uint32_t enqueued;
    uint32_t enqueue_failures;
} extraction_runtime_status_t;

/* `outbox` puede ser NULL: el registrador funciona igual, pero los registros no
 * sobreviven a un reinicio. */
esp_err_t extraction_runtime_start(nvs_outbox_t *outbox);
bool extraction_runtime_get_status(extraction_runtime_status_t *status);

/*
 * Fija la correspondencia entre el reloj monótono y la hora de pared. Sin ella
 * los registros no se serializan, porque el contrato exige instantes reales y
 * un reloj sin sincronizar arrancaría en 1970. Al fijarla se fechan y entregan
 * todos los registros que quedaron en espera, cada uno con el instante en que
 * ocurrió.
 */
void extraction_runtime_set_wall_clock(uint64_t epoch_ms);
bool extraction_runtime_has_wall_clock(void);

/*
 * Hora de pared actual, o cero si todavía no se conoce. La publica el
 * registrador porque es quien mantiene la correspondencia con el reloj
 * monótono, y la necesita cualquier mensaje que deba llevar un instante real.
 */
uint64_t extraction_runtime_wall_clock_now(void);

/* Tiempo transcurrido de la preparación en curso; cero si no hay ninguna. */
uint32_t extraction_runtime_active_elapsed_ms(void);

