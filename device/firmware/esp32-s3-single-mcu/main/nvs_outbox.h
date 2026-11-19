#ifndef EKO_NVS_OUTBOX_H
#define EKO_NVS_OUTBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * Huecos de la cola. Eran 32, y bastaban para varias preparaciones mientras la
 * serie ocupaba 6 tramos. Desde que lleva el minuto previo a pulsar (ADR-012,
 * enmienda del 2026-10-19) un café de 20 s ocupa unos 20 huecos, y el
 * 2026-10-19 una caída de la plataforma de 20 min llenó la cola con un solo
 * café: los tres siguientes perdieron su serie. Con 96 caben unos cuatro cafés
 * sin enlace. Cada hueco es un blob de ~1,6 KB: 96 ocupan ~160 KB de los 256 KB
 * de la partición nvs, que también guarda la configuración y el perfil. La
 * cola se recorre entera en cada operación, así que más huecos también es más
 * lectura de flash; se eligió el número más pequeño que cubre una caída como
 * la del 19.
 */
#define NVS_OUTBOX_CAPACITY 96U
#define NVS_OUTBOX_MAX_MESSAGE_ID_BYTES 80U
#define NVS_OUTBOX_MAX_JSON_BYTES 1536U

typedef struct {
    uint64_t order;
    char message_id[NVS_OUTBOX_MAX_MESSAGE_ID_BYTES + 1U];
    char json[NVS_OUTBOX_MAX_JSON_BYTES + 1U];
} nvs_outbox_item_t;

typedef struct {
    char namespace_name[16];
    SemaphoreHandle_t mutex;
    bool initialized;
} nvs_outbox_t;

esp_err_t nvs_outbox_initialize(nvs_outbox_t *outbox, const char *namespace_name);
void nvs_outbox_deinitialize(nvs_outbox_t *outbox);

esp_err_t nvs_outbox_enqueue(nvs_outbox_t *outbox,
                             const char *message_id,
                             const char *json,
                             bool *inserted);
esp_err_t nvs_outbox_count(nvs_outbox_t *outbox, size_t *count);
esp_err_t nvs_outbox_peek_batch(nvs_outbox_t *outbox,
                                nvs_outbox_item_t *items,
                                size_t capacity,
                                size_t *count);
esp_err_t nvs_outbox_acknowledge(nvs_outbox_t *outbox,
                                 const char *const *message_ids,
                                 size_t message_id_count,
                                 size_t *removed);
esp_err_t nvs_outbox_clear(nvs_outbox_t *outbox);

#endif
