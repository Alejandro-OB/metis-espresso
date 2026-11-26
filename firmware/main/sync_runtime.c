#include "sync_runtime.h"

#include <stdlib.h>

#include <inttypes.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "control_runtime.h"
#include "device_config.h"
#include "extraction_message.h"
#include "extraction_runtime.h"
#include "operational_runtime.h"
#include "process_variables.h"
#include "profile_message.h"
#include "profile_runtime.h"
#include "sync_transport.h"
#include "telemetry_message.h"

#define SYNC_TASK_STACK_BYTES 8192U
#define SYNC_TASK_PRIORITY (tskIDLE_PRIORITY + 3U)
#define SYNC_TASK_CORE 0
#define SYNC_BODY_BYTES (SYNC_MAX_BATCH * (NVS_OUTBOX_MAX_JSON_BYTES + 8U) + 128U)
#define SYNC_RESPONSE_BYTES 2048U
/* `{"deviceId":"…","messages":[]}` con el identificador más largo admisible. */
#define SYNC_TIME_REQUEST_BYTES 128U
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "sync_runtime";

static TaskHandle_t s_task;
static nvs_outbox_t *s_outbox;
static EventGroupHandle_t s_events;
static sync_runtime_status_t s_status;
static uint32_t s_telemetry_sequence;

/*
 * Plazos de cada entrega. La evidencia de la cola puede esperar a una
 * plataforma lenta: se confirma registro a registro y no se pierde. La
 * telemetría no: una muestra que tarda más que el periodo ya no describe el
 * proceso, y esperarla 10 s dejaba al café sin observación. El 2026-10-18 la
 * confirmación del registro de inicio llegó ~12 s después de pulsar, y la
 * plataforma no recibió ninguna muestra hasta ~20 s después (inferido que una
 * cosa causó la otra). Con 2 s, una respuesta lenta cuesta una muestra perdida
 * y no el café entero.
 */
#define SYNC_DELIVERY_TIMEOUT_MS 10000
#define SYNC_TELEMETRY_TIMEOUT_MS 2000
/*
 * Cuánto puede apartarse la hora de la placa de la que trae cada respuesta antes
 * de corregirla. Provisional, elegido así: la respuesta se espera como mucho
 * SYNC_DELIVERY_TIMEOUT_MS (10 s) y en la Raspberry tarda 0,05–0,13 s (registro
 * de nginx, 26-oct-2026), así que 2 min no se confunden con el viaje; es el
 * mismo umbral con que el panel de la plataforma avisa de la hora corrida.
 */
#define SYNC_CLOCK_TOLERANCE_MS UINT64_C(120000)
/*
 * Con la cola vacía, cada cuántos intentos de entrega se consulta igualmente la
 * plataforma. La configuración, y con ella la subida del primer uso, solo llega
 * en la respuesta a una sincronización; sin registros que entregar la placa no
 * sincronizaba, y la subida calculada para un reposo llegaba con el registro
 * del café siguiente, es decir, tarde. Se observó el 2026-11-11: 20 min de
 * reposo sin ninguna consulta y la subida en +0,00 °C hasta forzar un cambio de
 * estado. Cuatro intentos de CONFIG_CAFE_SYNC_INTERVAL_MS (15 s por omisión)
 * dan una consulta por minuto: la regla del primer uso avanza en pasos de 5 min,
 * así que un minuto de retraso no cambia lo que se pide, y la Raspberry
 * responde en 0,05–0,13 s.
 */
#define SYNC_IDLE_POLL_EVERY 4U
static uint32_t s_idle_attempts;
static device_config_t s_config;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.wifi_connected = false;
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi desconectado; se reintenta");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        portENTER_CRITICAL(&s_status_lock);
        s_status.wifi_connected = true;
        portEXIT_CRITICAL(&s_status_lock);
        snprintf(s_status.address, sizeof(s_status.address), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Wi-Fi conectado; dirección %s", s_status.address);
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t start_wifi(void)
{
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }
    const wifi_init_config_t configuration = WIFI_INIT_CONFIG_DEFAULT();
    if ((result = esp_wifi_init(&configuration)) != ESP_OK) {
        return result;
    }
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));

    wifi_config_t station = {0};
    strncpy((char *)station.sta.ssid, CONFIG_CAFE_WIFI_SSID, sizeof(station.sta.ssid) - 1U);
    strncpy((char *)station.sta.password, CONFIG_CAFE_WIFI_PASSWORD,
            sizeof(station.sta.password) - 1U);
    if ((result = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (result = esp_wifi_set_config(WIFI_IF_STA, &station)) != ESP_OK) {
        return result;
    }
    return esp_wifi_start();
}

/*
 * Entrega un cuerpo al servicio de sincronización. La respuesta se devuelve al
 * llamante porque la entrega de la cola necesita leer qué mensajes confirmó el
 * servicio; la telemetría, en cambio, la ignora.
 */
static esp_err_t post_body(const char *body, char *response, size_t response_size,
                           int *status_code, int timeout_ms)
{
    esp_http_client_config_t configuration = {
        .url = s_config.sync_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = timeout_ms,
    };
    esp_http_client_handle_t client = esp_http_client_init(&configuration);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    char authorization[DEVICE_CONFIG_TOKEN_BYTES + 8U];
    snprintf(authorization, sizeof(authorization), "Bearer %s", s_config.sync_token);
    esp_http_client_set_header(client, "Authorization", authorization);
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    const esp_err_t result = esp_http_client_open(client, (int)strlen(body));
    if (result == ESP_OK) {
        esp_http_client_write(client, body, (int)strlen(body));
        esp_http_client_fetch_headers(client);
        *status_code = esp_http_client_get_status_code(client);
        if (response != NULL && response_size > 0U) {
            const int read = esp_http_client_read(client, response, (int)response_size - 1);
            if (read > 0) {
                response[read] = '\0';
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

/*
 * Aplica la configuración que el barista aceptó, si la respuesta trae alguna.
 *
 * La plataforma propone y la máquina dispone: el firmware valida contra sus
 * propios límites y rechaza con causa tipificada. Aceptarla no inicia nada —una
 * preparación solo empieza por el mando físico—: queda como
 * configuración efectiva para la siguiente.
 */
static void apply_proposed_profile(const char *response)
{
    /* Una configuración no entra en mitad de una preparación. El resumen guarda
     * la configuración con la que se abrió la sesión, de modo que cambiarla
     * ahora dejaría un registro que declara unas referencias mientras el lazo
     * siguió otras: evidencia que se contradice a sí misma. No hace falta
     * retenerla, porque la plataforma la vuelve a ofrecer en cada intercambio;
     * entrará en cuanto la máquina esté libre, que es lo que decide
     * ADR-017 § 4.4. No se exige que la máquina esté apagada: protegería un poco
     * más y convertiría cada ajuste en un ritual de apagado y encendido. */
    extraction_runtime_status_t extraction = {0};
    if (extraction_runtime_get_status(&extraction) && extraction.extraction_active) {
        return;
    }

    extraction_profile_t profile = {0};
    const profile_message_status_t read = profile_message_parse_first(response, &profile);
    if (read == PROFILE_MESSAGE_ABSENT) {
        return;  /* lo habitual: no hay nada propuesto */
    }
    if (read != PROFILE_MESSAGE_OK) {
        ESP_LOGW(TAG, "Propuesta descartada al leerla: %s",
                 profile_message_status_name(read));
        return;
    }

    /* La plataforma ofrece la configuración vigente en cada intercambio, de modo
     * que sin esta comprobación se reactivaría cada quince segundos: ruido en el
     * registro y, sobre todo, una escritura en memoria no volátil por
     * intercambio, que es desgaste real para no cambiar nada.
     *
     * El desplazamiento de caldera del primer uso cuenta como parte de la
     * identidad (ADR-009, enmienda del 2026-10-19): el gemelo lo pide o lo
     * retira sin cambiar de versión, y cada cambio es una petición nueva que
     * vale para una sola preparación. Repetir la misma petición no la renueva:
     * la que ya se consumió sigue consumida mientras la plataforma no la
     * retire y la vuelva a pedir. El refuerzo de la prealimentación
     * (enmienda del 2026-10-27) cuenta igual. */
    extraction_profile_t current = {0};
    if (profile_runtime_get_effective(&current) &&
        current.version == profile.version &&
        strcmp(current.id, profile.id) == 0 &&
        current.boiler_offset_c == profile.boiler_offset_c &&
        current.first_use_feedforward_gain == profile.first_use_feedforward_gain) {
        return;
    }

    extraction_profile_validation_t validation = PROFILE_VALID;
    const esp_err_t result = profile_runtime_activate(&profile, &validation);
    if (result != ESP_OK || validation != PROFILE_VALID) {
        ESP_LOGW(TAG, "Propuesta %s rechazada por el dispositivo: %s", profile.id,
                 extraction_profile_validation_name(validation));
        return;
    }
    ESP_LOGI(TAG, "Configuración %s v%" PRIu32 " aceptada y vigente; desplazamiento de "
             "caldera pedido %.2f C, refuerzo pedido %.3f", profile.id, profile.version,
             (double)profile.boiler_offset_c, (double)profile.first_use_feedforward_gain);
}

/* Envía un lote y confirma en la cola solo lo que el servicio declare recibido. */
/*
 * Obtiene la hora de pared preguntándosela a la plataforma. Sin ella los
 * registros no pueden serializarse, porque el contrato exige instantes reales:
 * es el paso que desbloquea el guardado.
 *
 * La pide a la plataforma y no a un servidor de hora de Internet porque así lo
 * decide ADR-013 § 4.1: la instalación debe conservar una base temporal
 * coherente aunque no tenga salida a Internet, y concentrar la hora en la
 * plataforma deja un único sitio donde mirar cuando la fecha de un registro no
 * cuadra. La petición no añade ningún servicio ni protocolo: es la misma
 * sincronización de siempre, sin mensajes que entregar (§ 4.4).
 *
 * Antes se consultaba un servidor externo por SNTP. Además de contradecir la
 * decisión, no se recuperaba de un fallo: el servicio quedaba a medio iniciar y
 * todos los reintentos posteriores fallaban, de modo que un primer intento sin
 * respuesta dejaba al dispositivo sin poder fechar nada hasta reiniciarlo. Se
 * observó en la placa el 15/09/2026.
 */
static void synchronise_clock(void)
{
    char body[SYNC_TIME_REQUEST_BYTES];
    if (sync_time_request_build(CONFIG_CAFE_DEVICE_ID, body, sizeof(body), NULL) !=
        SYNC_BODY_OK) {
        return;
    }
    char *response = calloc(1U, SYNC_RESPONSE_BYTES);
    if (response == NULL) {
        return;
    }

    int status_code = -1;
    const esp_err_t result = post_body(body, response, SYNC_RESPONSE_BYTES, &status_code,
                                       SYNC_DELIVERY_TIMEOUT_MS);
    uint64_t epoch_ms = 0U;
    if (result != ESP_OK || status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "Sin respuesta de hora; los registros seguirán pendientes");
    } else if (!sync_response_server_time_ms(response, &epoch_ms)) {
        /* La plataforma puede declarar que su propio reloj no es fiable. En ese
         * caso no hay hora, que es distinto de que la haya y no se entienda:
         * ninguna de las dos autoriza a inventarla (ADR-013 § 4.3). */
        ESP_LOGW(TAG, "La plataforma no entregó una hora utilizable");
    } else {
        extraction_runtime_set_wall_clock(epoch_ms);
        portENTER_CRITICAL(&s_status_lock);
        s_status.clock_synchronised = true;
        portEXIT_CRITICAL(&s_status_lock);
        ESP_LOGI(TAG, "Hora tomada de la plataforma; epoch=%" PRIu64 " ms", epoch_ms);
    }
    free(response);
}

/*
 * Sincronización sin mensajes, la misma petición que pide la hora al arrancar,
 * solo para traer la configuración vigente cuando no hay nada que entregar.
 * No borra ni confirma nada de la cola. Un fallo solo se anota: la consulta
 * siguiente lo reintenta y la placa sigue con la configuración que tenía.
 */
static void poll_configuration(void)
{
    char body[SYNC_TIME_REQUEST_BYTES];
    if (sync_time_request_build(CONFIG_CAFE_DEVICE_ID, body, sizeof(body), NULL) !=
        SYNC_BODY_OK) {
        return;
    }
    char *response = calloc(1U, SYNC_RESPONSE_BYTES);
    if (response == NULL) {
        return;
    }
    int status_code = -1;
    const esp_err_t result = post_body(body, response, SYNC_RESPONSE_BYTES, &status_code,
                                       SYNC_DELIVERY_TIMEOUT_MS);
    if (result != ESP_OK || status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "Consulta sin registros rechazada: estado=%d error=%s", status_code,
                 esp_err_to_name(result));
    } else {
        apply_proposed_profile(response);
    }
    free(response);
}

static void deliver_pending(void)
{
    size_t pending = 0U;
    if (nvs_outbox_count(s_outbox, &pending) != ESP_OK || pending == 0U) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.pending = (uint32_t)pending;
        portEXIT_CRITICAL(&s_status_lock);
        if (++s_idle_attempts >= SYNC_IDLE_POLL_EVERY) {
            s_idle_attempts = 0U;
            poll_configuration();
        }
        return;
    }
    s_idle_attempts = 0U;

    nvs_outbox_item_t *items = calloc(SYNC_MAX_BATCH, sizeof(*items));
    char *body = malloc(SYNC_BODY_BYTES);
    char *response = calloc(1U, SYNC_RESPONSE_BYTES);
    if (items == NULL || body == NULL || response == NULL) {
        ESP_LOGE(TAG, "Sin memoria para preparar el lote");
        goto cleanup;
    }

    size_t count = 0U;
    if (nvs_outbox_peek_batch(s_outbox, items, SYNC_MAX_BATCH, &count) != ESP_OK ||
        count == 0U) {
        goto cleanup;
    }
    const char *messages[SYNC_MAX_BATCH];
    for (size_t index = 0U; index < count; ++index) {
        messages[index] = items[index].json;
    }
    if (sync_body_build(CONFIG_CAFE_DEVICE_ID, messages, count, body, SYNC_BODY_BYTES,
                        NULL) != SYNC_BODY_OK) {
        ESP_LOGE(TAG, "No se pudo construir el lote de %zu mensajes", count);
        goto cleanup;
    }

    int status_code = -1;
    const esp_err_t result = post_body(body, response, SYNC_RESPONSE_BYTES, &status_code,
                                       SYNC_DELIVERY_TIMEOUT_MS);

    if (result != ESP_OK || status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "Entrega rechazada: estado=%d error=%s", status_code,
                 esp_err_to_name(result));
        portENTER_CRITICAL(&s_status_lock);
        s_status.delivery_failures++;
        portEXIT_CRITICAL(&s_status_lock);
        goto cleanup;
    }

    apply_proposed_profile(response);

    /* Solo se borra lo confirmado: un reenvío duplicado lo descarta el servicio,
     * pero un borrado sin confirmar perdería el registro para siempre. */
    const char *confirmed[SYNC_MAX_BATCH];
    size_t confirmed_count = 0U;
    for (size_t index = 0U; index < count; ++index) {
        if (sync_response_acknowledges(response, items[index].message_id)) {
            confirmed[confirmed_count++] = items[index].message_id;
        }
    }
    if (confirmed_count > 0U) {
        size_t removed = 0U;
        if (nvs_outbox_acknowledge(s_outbox, confirmed, confirmed_count, &removed) == ESP_OK) {
            portENTER_CRITICAL(&s_status_lock);
            s_status.delivered += (uint32_t)removed;
            portEXIT_CRITICAL(&s_status_lock);
            ESP_LOGI(TAG, "Entregados y confirmados %zu de %zu registros", removed, count);
        }
    } else {
        ESP_LOGW(TAG, "El servicio no confirmó ninguno de los %zu enviados", count);
    }
    if (nvs_outbox_count(s_outbox, &pending) == ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.pending = (uint32_t)pending;
        portEXIT_CRITICAL(&s_status_lock);
    }

cleanup:
    free(items);
    free(body);
    free(response);
}

/*
 * Cadencia de la telemetría. Durante la preparación se observa el proceso; en
 * reposo basta con mantener el estado al día, sin emisión sostenida.
 */
static uint32_t telemetry_interval_ms(void)
{
    return operational_runtime_state() == OP_STATE_EXTRACTION
               ? CONFIG_CAFE_TELEMETRY_ACTIVE_INTERVAL_MS
               : CONFIG_CAFE_TELEMETRY_IDLE_INTERVAL_MS;
}

/*
 * Compone y entrega una muestra de telemetría.
 *
 * La telemetría es observación del proceso en curso: no pasa por la cola
 * persistente y se pierde si no hay enlace, para que la evidencia de las
 * preparaciones conserve íntegra su capacidad de cola (ADR-012 § 4).
 *
 * También es la serie medida con la que después se compara el modelo virtual,
 * de modo que su procedencia debe ser la que declare la adquisición y nunca un
 * valor por omisión.
 */
/*
 * La hora se pedía solo al arrancar; si la plataforma la dio mal en ese momento
 * (2026-10-26: su reloj aún con la noche anterior), quedaba mal hasta reiniciar.
 * Cada respuesta trae la hora del servicio, cuando su reloj está sincronizado,
 * y se toma si la de la placa se aparta más de SYNC_CLOCK_TOLERANCE_MS. Nunca
 * durante un café: el resumen y la serie se fechan con la hora del arranque del
 * café, y cambiarla a mitad los partiría en dos relojes; se corrige después.
 */
static void correct_clock_from(const char *response, bool extracting)
{
    uint64_t service_ms = 0U;
    if (extracting || !sync_response_server_time_ms(response, &service_ms)) {
        return;
    }
    const uint64_t device_ms = extraction_runtime_wall_clock_now();
    if (!sync_clock_needs_correction(device_ms, service_ms, SYNC_CLOCK_TOLERANCE_MS)) {
        return;
    }
    ESP_LOGW(TAG, "Hora corregida desde la plataforma: placa=%" PRIu64 " servicio=%" PRIu64
             " ms", device_ms, service_ms);
    extraction_runtime_set_wall_clock(service_ms);
}

static void publish_telemetry(void)
{
    telemetry_message_context_t context = {
        .device_id = CONFIG_CAFE_DEVICE_ID,
        .sequence = s_telemetry_sequence + 1U,
        .epoch_ms = extraction_runtime_wall_clock_now(),
        .state = operational_runtime_state(),
    };

    if (!process_variables_get(&context.process)) {
        return;  /* sin fuente de proceso no hay nada que declarar */
    }

    extraction_profile_t profile = {0};
    if (!profile_runtime_get_effective(&profile)) {
        return;  /* sin configuración efectiva no hay referencias que publicar */
    }
    context.target_temperature_c = profile.temperature_setpoint_c;
    context.target_pressure_bar = profile.pressure_setpoint_bar;

    extraction_runtime_status_t extraction = {0};
    if (extraction_runtime_get_status(&extraction) && extraction.extraction_active) {
        context.preparation_id = extraction.active.id;
        context.elapsed_ms = extraction_runtime_active_elapsed_ms();
    }

    /* Si el lazo no publica su estado no se afirma que la actuación llegó a la
     * máquina: se declara virtual, que es la lectura conservadora. */
    control_runtime_status_t control = {0};
    if (control_runtime_get_status(&control)) {
        context.heater_on = control.temperature_demand > 0U;
        context.heater_applied_known = true;
        context.heater_applied_ms = control.heater_applied_ms;
        context.pump_duty_percent = control.pressure_density;
        context.virtual_actuation = control.bench_loop;
    } else {
        context.virtual_actuation = true;
    }

    char message[TELEMETRY_MESSAGE_JSON_BYTES];
    char message_id[TELEMETRY_MESSAGE_ID_LENGTH + 1U];
    const telemetry_message_status_t built = telemetry_message_build(
        &context, message, sizeof(message), message_id, sizeof(message_id));
    if (built != TELEMETRY_MESSAGE_OK) {
        if (built != TELEMETRY_MESSAGE_TIME_BASE_UNAVAILABLE &&
            built != TELEMETRY_MESSAGE_SOURCE_UNAVAILABLE) {
            ESP_LOGW(TAG, "Muestra no serializable: %s",
                     telemetry_message_status_name(built));
        }
        return;
    }

    char body[TELEMETRY_MESSAGE_JSON_BYTES + 128U];
    const char *messages[1] = {message};
    if (sync_body_build(CONFIG_CAFE_DEVICE_ID, messages, 1U, body, sizeof(body), NULL) !=
        SYNC_BODY_OK) {
        return;
    }

    s_telemetry_sequence++;
    /* La respuesta trae las configuraciones que el barista aceptó, y antes se
     * descartaba aquí. Como solo se leían al entregar la cola, y la entrega no
     * llega a preguntar cuando no hay nada pendiente, una configuración
     * activada no bajaba nunca mientras la máquina estaba en reposo: había que
     * preparar un café para que llegara la configuración con la que preparar
     * ese café. Es el mismo intercambio de siempre; leer lo que ya viene no
     * añade ninguna petición. Sin memoria para la respuesta la muestra se envía
     * igual: perderla sería peor que no enterarse de una propuesta. */
    char *response = calloc(1U, SYNC_RESPONSE_BYTES);
    int status_code = -1;
    const esp_err_t result =
        post_body(body, response, response == NULL ? 0U : SYNC_RESPONSE_BYTES, &status_code,
                  SYNC_TELEMETRY_TIMEOUT_MS);
    const bool delivered = result == ESP_OK && status_code >= 200 && status_code < 300;
    portENTER_CRITICAL(&s_status_lock);
    if (delivered) {
        s_status.telemetry_sent++;
    } else {
        s_status.telemetry_dropped++;
    }
    portEXIT_CRITICAL(&s_status_lock);
    if (delivered && response != NULL) {
        apply_proposed_profile(response);
        correct_clock_from(response, extraction.extraction_active);
    }
    free(response);
}

static void sync_task(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG, "Enlace con la plataforma iniciado; núcleo=%d", (int)xPortGetCoreID());
    xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    synchronise_clock();
    uint32_t elapsed_since_delivery_ms = 0U;
    bool was_extracting = false;
    uint32_t sent_at_start = 0U;
    uint32_t dropped_at_start = 0U;
    while (true) {
        const bool extracting = operational_runtime_state() == OP_STATE_EXTRACTION;
        if (extracting != was_extracting) {
            /* Cuántas muestras salieron durante el café, para comprobar en el
             * registro que la observación en vivo llega; sin esto, una
             * telemetría que se corta solo se nota desde la plataforma. */
            sync_runtime_status_t now = {0};
            (void)sync_runtime_get_status(&now);
            if (extracting) {
                sent_at_start = now.telemetry_sent;
                dropped_at_start = now.telemetry_dropped;
            } else {
                ESP_LOGI(TAG, "Telemetría durante el café: %" PRIu32 " entregadas, %" PRIu32
                         " perdidas",
                         now.telemetry_sent - sent_at_start,
                         now.telemetry_dropped - dropped_at_start);
            }
            was_extracting = extracting;
        }
        if ((xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT) != 0U) {
            if (!extraction_runtime_has_wall_clock()) {
                synchronise_clock();
            }
            /* La telemetría marca el ritmo del bucle y la entrega de la cola
             * conserva el suyo: una preparación necesita observarse cada
             * segundo, mientras que la evidencia puede esperar. */
            publish_telemetry();
            elapsed_since_delivery_ms += telemetry_interval_ms();
            /* Durante el café la cola no se entrega. Comparte la tarea con la
             * telemetría, y una entrega lenta la detenía entera: el 2026-10-18
             * la confirmación del registro de inicio llegó ~12 s después de
             * pulsar y el café se quedó sin observación. Lo encolado no se pierde; sale en cuanto
             * termina la preparación. */
            if (!extracting && elapsed_since_delivery_ms >= CONFIG_CAFE_SYNC_INTERVAL_MS) {
                deliver_pending();
                elapsed_since_delivery_ms = 0U;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(telemetry_interval_ms()));
    }
}

void sync_runtime_reload_config(void)
{
    device_config_load(&s_config);
    /* La credencial no se imprime: el puerto de servicio queda registrado en
     * cualquier captura de consola, y basta con saber de dónde procede. */
    ESP_LOGI(TAG, "Plataforma: %s (dirección %s, credencial %s)", s_config.sync_url,
             device_config_origin_name(s_config.url_origin),
             device_config_origin_name(s_config.token_origin));
}

esp_err_t sync_runtime_start(nvs_outbox_t *outbox)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (outbox == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_outbox = outbox;
    /* En cuanto lo guardado puede desplazar a lo compilado, la configuración de
     * compilación deja de responder a dónde apunta este dispositivo: el arranque
     * debe declarar qué está en vigor. La credencial no se registra. */
    device_config_load(&s_config);
    ESP_LOGI(TAG, "Plataforma: %s (dirección %s, credencial %s)", s_config.sync_url,
             device_config_origin_name(s_config.url_origin),
             device_config_origin_name(s_config.token_origin));
    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t result = start_wifi();
    if (result != ESP_OK) {
        return result;
    }
    return xTaskCreatePinnedToCore(sync_task, "sync_link", SYNC_TASK_STACK_BYTES, NULL,
                                   SYNC_TASK_PRIORITY, &s_task, SYNC_TASK_CORE) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

bool sync_runtime_get_status(sync_runtime_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return true;
}
