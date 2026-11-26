#include "nvs_outbox.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvs.h"

#define OUTBOX_RECORD_MAGIC UINT32_C(0x454B4F31)
#define OUTBOX_RECORD_VERSION UINT16_C(1)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    nvs_outbox_item_t item;
} persisted_record_t;

static bool outbox_ready(const nvs_outbox_t *outbox)
{
    return outbox != NULL && outbox->initialized && outbox->mutex != NULL;
}

static void slot_key(size_t slot, char key[5])
{
    (void)snprintf(key, 5U, "q%02u", (unsigned int)slot);
}

static esp_err_t read_slot(nvs_handle_t handle,
                           size_t slot,
                           persisted_record_t *record,
                           bool *occupied)
{
    char key[5];
    slot_key(slot, key);
    size_t size = sizeof(*record);
    esp_err_t result = nvs_get_blob(handle, key, record, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        *occupied = false;
        return ESP_OK;
    }
    if (result != ESP_OK) {
        return result;
    }
    if (size != sizeof(*record) || record->magic != OUTBOX_RECORD_MAGIC ||
        record->version != OUTBOX_RECORD_VERSION ||
        record->item.message_id[NVS_OUTBOX_MAX_MESSAGE_ID_BYTES] != '\0' ||
        record->item.json[NVS_OUTBOX_MAX_JSON_BYTES] != '\0') {
        return ESP_ERR_INVALID_CRC;
    }
    *occupied = true;
    return ESP_OK;
}

static bool identifier_matches(const char *candidate,
                               const char *const *message_ids,
                               size_t message_id_count)
{
    for (size_t index = 0; index < message_id_count; ++index) {
        if (message_ids[index] != NULL && strcmp(candidate, message_ids[index]) == 0) {
            return true;
        }
    }
    return false;
}

esp_err_t nvs_outbox_initialize(nvs_outbox_t *outbox, const char *namespace_name)
{
    if (outbox == NULL || namespace_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t length = strnlen(namespace_name, sizeof(outbox->namespace_name));
    if (length == 0U || length >= sizeof(outbox->namespace_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(outbox, 0, sizeof(*outbox));
    memcpy(outbox->namespace_name, namespace_name, length + 1U);
    outbox->mutex = xSemaphoreCreateMutex();
    if (outbox->mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    outbox->initialized = true;
    return ESP_OK;
}

void nvs_outbox_deinitialize(nvs_outbox_t *outbox)
{
    if (outbox == NULL) {
        return;
    }
    if (outbox->mutex != NULL) {
        vSemaphoreDelete(outbox->mutex);
    }
    memset(outbox, 0, sizeof(*outbox));
}

esp_err_t nvs_outbox_enqueue(nvs_outbox_t *outbox,
                             const char *message_id,
                             const char *json,
                             bool *inserted)
{
    if (!outbox_ready(outbox) || message_id == NULL || json == NULL || inserted == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t id_length = strnlen(message_id, NVS_OUTBOX_MAX_MESSAGE_ID_BYTES + 1U);
    const size_t json_length = strnlen(json, NVS_OUTBOX_MAX_JSON_BYTES + 1U);
    if (id_length < 8U || id_length > NVS_OUTBOX_MAX_MESSAGE_ID_BYTES ||
        json_length == 0U || json_length > NVS_OUTBOX_MAX_JSON_BYTES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xSemaphoreTake(outbox->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result;
    nvs_handle_t handle;
    *inserted = false;
    result = nvs_open(outbox->namespace_name, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        xSemaphoreGive(outbox->mutex);
        return result;
    }

    persisted_record_t *record = calloc(1U, sizeof(*record));
    if (record == NULL) {
        result = ESP_ERR_NO_MEM;
        goto finish;
    }
    size_t free_slot = NVS_OUTBOX_CAPACITY;
    uint64_t maximum_order = 0U;
    for (size_t slot = 0; slot < NVS_OUTBOX_CAPACITY; ++slot) {
        memset(record, 0, sizeof(*record));
        bool occupied = false;
        result = read_slot(handle, slot, record, &occupied);
        if (result != ESP_OK) {
            break;
        }
        if (!occupied) {
            if (free_slot == NVS_OUTBOX_CAPACITY) {
                free_slot = slot;
            }
            continue;
        }
        if (strcmp(record->item.message_id, message_id) == 0) {
            result = ESP_OK;
            goto finish_record;
        }
        if (record->item.order > maximum_order) {
            maximum_order = record->item.order;
        }
    }
    if (result != ESP_OK) {
        goto finish_record;
    }
    if (free_slot == NVS_OUTBOX_CAPACITY) {
        result = ESP_ERR_NO_MEM;
        goto finish_record;
    }

    memset(record, 0, sizeof(*record));
    record->magic = OUTBOX_RECORD_MAGIC;
    record->version = OUTBOX_RECORD_VERSION;
    record->item.order = maximum_order + 1U;
    memcpy(record->item.message_id, message_id, id_length + 1U);
    memcpy(record->item.json, json, json_length + 1U);
    char key[5];
    slot_key(free_slot, key);
    result = nvs_set_blob(handle, key, record, sizeof(*record));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (result == ESP_OK) {
        *inserted = true;
    }

finish_record:
    free(record);
finish:
    nvs_close(handle);
    xSemaphoreGive(outbox->mutex);
    return result;
}

esp_err_t nvs_outbox_count(nvs_outbox_t *outbox, size_t *count)
{
    if (!outbox_ready(outbox) || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(outbox->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(outbox->namespace_name, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0U;
        result = ESP_OK;
    } else if (result == ESP_OK) {
        persisted_record_t *record = calloc(1U, sizeof(*record));
        if (record == NULL) {
            nvs_close(handle);
            xSemaphoreGive(outbox->mutex);
            return ESP_ERR_NO_MEM;
        }
        *count = 0U;
        for (size_t slot = 0; slot < NVS_OUTBOX_CAPACITY; ++slot) {
            memset(record, 0, sizeof(*record));
            bool occupied = false;
            result = read_slot(handle, slot, record, &occupied);
            if (result != ESP_OK) {
                break;
            }
            *count += occupied ? 1U : 0U;
        }
        free(record);
        nvs_close(handle);
    }
    xSemaphoreGive(outbox->mutex);
    return result;
}

esp_err_t nvs_outbox_peek_batch(nvs_outbox_t *outbox,
                                nvs_outbox_item_t *items,
                                size_t capacity,
                                size_t *count)
{
    if (!outbox_ready(outbox) || items == NULL || capacity == 0U || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(outbox->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(outbox->namespace_name, NVS_READONLY, &handle);
    *count = 0U;
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
        goto finish_without_handle;
    }
    if (result != ESP_OK) {
        goto finish_without_handle;
    }

    persisted_record_t *selected = calloc(1U, sizeof(*selected));
    persisted_record_t *candidate = calloc(1U, sizeof(*candidate));
    if (selected == NULL || candidate == NULL) {
        free(selected);
        free(candidate);
        result = ESP_ERR_NO_MEM;
        goto finish_with_handle;
    }
    uint64_t previous_order = 0U;
    while (*count < capacity) {
        bool found = false;
        memset(selected, 0, sizeof(*selected));
        for (size_t slot = 0; slot < NVS_OUTBOX_CAPACITY; ++slot) {
            memset(candidate, 0, sizeof(*candidate));
            bool occupied = false;
            result = read_slot(handle, slot, candidate, &occupied);
            if (result != ESP_OK) {
                goto finish_batch;
            }
            if (occupied && candidate->item.order > previous_order &&
                (!found || candidate->item.order < selected->item.order)) {
                *selected = *candidate;
                found = true;
            }
        }
        if (!found) {
            break;
        }
        items[*count] = selected->item;
        ++(*count);
        previous_order = selected->item.order;
    }

finish_batch:
    free(selected);
    free(candidate);
finish_with_handle:
    nvs_close(handle);
finish_without_handle:
    xSemaphoreGive(outbox->mutex);
    return result;
}

esp_err_t nvs_outbox_acknowledge(nvs_outbox_t *outbox,
                                 const char *const *message_ids,
                                 size_t message_id_count,
                                 size_t *removed)
{
    if (!outbox_ready(outbox) || (message_id_count > 0U && message_ids == NULL) ||
        removed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(outbox->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(outbox->namespace_name, NVS_READWRITE, &handle);
    *removed = 0U;
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
        goto finish_without_handle;
    }
    if (result != ESP_OK) {
        goto finish_without_handle;
    }

    persisted_record_t *record = calloc(1U, sizeof(*record));
    if (record == NULL) {
        result = ESP_ERR_NO_MEM;
        goto finish_ack;
    }
    for (size_t slot = 0; slot < NVS_OUTBOX_CAPACITY; ++slot) {
        memset(record, 0, sizeof(*record));
        bool occupied = false;
        result = read_slot(handle, slot, record, &occupied);
        if (result != ESP_OK) {
            break;
        }
        if (occupied && identifier_matches(record->item.message_id, message_ids,
                                           message_id_count)) {
            char key[5];
            slot_key(slot, key);
            result = nvs_erase_key(handle, key);
            if (result != ESP_OK) {
                break;
            }
            ++(*removed);
        }
    }
    if (result == ESP_OK && *removed > 0U) {
        result = nvs_commit(handle);
    }
    free(record);
finish_ack:
    nvs_close(handle);

finish_without_handle:
    xSemaphoreGive(outbox->mutex);
    return result;
}

esp_err_t nvs_outbox_clear(nvs_outbox_t *outbox)
{
    if (!outbox_ready(outbox)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(outbox->mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open(outbox->namespace_name, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_erase_all(handle);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    xSemaphoreGive(outbox->mutex);
    return result;
}
