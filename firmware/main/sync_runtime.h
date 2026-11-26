#pragma once

/*
 * Enlace con la plataforma externa: conexión Wi-Fi, hora de pared y entrega de
 * los registros pendientes de la cola persistente.
 *
 * Nada de lo que llega por este canal puede iniciar una preparación ni actuar
 * sobre las salidas de potencia.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "nvs_outbox.h"

typedef struct {
    bool wifi_connected;
    bool clock_synchronised;
    uint32_t delivered;         /* mensajes confirmados por el servicio */
    uint32_t delivery_failures;
    uint32_t pending;           /* registros que siguen en la cola */
    uint32_t telemetry_sent;    /* muestras observadas que llegaron */
    uint32_t telemetry_dropped; /* muestras perdidas por falta de enlace */
    char address[64];           /* dirección IP obtenida, si la hay */
} sync_runtime_status_t;

esp_err_t sync_runtime_start(nvs_outbox_t *outbox);
bool sync_runtime_get_status(sync_runtime_status_t *status);

/*
 * Vuelve a leer a dónde apunta el dispositivo y con qué credencial.
 *
 * Se invoca tras guardar una configuración nueva por el puerto de servicio. Sin
 * esto el cambio solo regiría tras reiniciar, y reiniciar para cambiar de
 * plataforma vacía la retención en memoria de los registros que aún esperan
 * hora: precisamente la evidencia que el cambio pretende poder entregar.
 */
void sync_runtime_reload_config(void);
