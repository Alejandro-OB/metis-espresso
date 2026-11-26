#include "extraction_runtime.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "acquisition_runtime.h"
#include "commissioned_plant.h"
#include "condition_monitor.h"
#include "event_message.h"
#include "extraction_message.h"
#include "first_use_offset_runtime.h"
#include "heater_feedforward.h"
#include "open_extraction.h"
#include "pre_start_history.h"
#include "process_variables.h"
#include "series_message.h"
#include "supervisor_runtime.h"
#include "operational_runtime.h"
#include "profile_runtime.h"
#include "simulation_runtime.h"
#include "water_reference_runtime.h"

#define EXTRACTION_TASK_PERIOD_MS 100U
/*
 * Registros cerrados que pueden esperar hora. Se alinea con la capacidad de la
 * cola persistente: retener más de lo que la cola podrá aceptar solo aplazaría
 * la pérdida.
 */
/* Registros retenidos en memoria cuando no se pudieron encolar (sin hora o con
 * la cola llena). Cada uno ocupa un resumen entero en RAM, así que no crece con
 * la cola: era igual a ella mientras la cola tenía 32 huecos. */
#define EXTRACTION_PENDING_CAPACITY 32U
/* Cada cuánto se reintenta encolar lo retenido. Antes solo se reintentaba al
 * cerrar la preparación siguiente o al recibir hora, y el 2026-10-19 tres
 * resúmenes esperaron más de 40 min con la cola ya vacía. */
#define EXTRACTION_PENDING_RETRY_MS 10000U
/*
 * La tarea arma en su pila el JSON entero de un resumen (hasta 1537 bytes) y
 * llama a `snprintf` con coma flotante, que en newlib pide por sí solo del orden
 * de un kilobyte. Con 4096 bytes iba justa: el 2026-10-28, al añadir la recta
 * caldera → agua al resumen (192 bytes más en `extraction_message_build`), la
 * placa se reinició al cerrar el primer café (sim-00000461), con la comprobación
 * de desborde por canario como causa más probable (inferido: el registro solo
 * muestra el reinicio justo al cerrar). Se sube a 6144, el mismo margen que la
 * tarea de actualización, en vez de recortar el mensaje o mover el búfer a
 * memoria estática, que obligaría a demostrar que ninguna otra ruta lo usa a la
 * vez.
 */
#define EXTRACTION_TASK_STACK_BYTES 6144U
#define EXTRACTION_TASK_PRIORITY (tskIDLE_PRIORITY + 5U)

/*
 * El número de preparación se guarda porque identifica la evidencia. Vivía solo
 * en memoria y volvía a uno en cada arranque, de modo que la primera
 * preparación tras un reinicio reutilizaba el identificador de la primera
 * preparación anterior. La plataforma reconoce ese registro como un reenvío del
 * mismo —que es justo lo que debe hacer con un identificador repetido—, lo
 * descarta y responde que lo tiene; el dispositivo lo borra de la cola por
 * entregado y la preparación desaparece sin que nadie lo advierta. Peor aún,
 * sus muestras sí entran, con identificadores propios, y quedan colgando de la
 * preparación anterior: evidencia de una sesión atribuida a otra.
 *
 * Se comprobó en la placa el 15/09/2026: una preparación de 29,9 s con 299
 * muestras se perdió por completo y 16 de sus muestras se archivaron bajo una
 * preparación del día anterior. Ver ADR-012 § 4.1.
 */
#define EXTRACTION_SEQUENCE_NAMESPACE "cafe-extract"
#define EXTRACTION_SEQUENCE_KEY "session_seq"
/* Marca de la preparación abierta (`open_extraction.h`), junto a su número. */
#define EXTRACTION_OPEN_KEY "open_session"

static const char *TAG = "extraction_task";

static TaskHandle_t s_task_handle;
static extraction_session_t s_session;
static extraction_runtime_status_t s_status;
static uint32_t s_session_sequence;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static nvs_outbox_t *s_outbox;
static uint64_t s_wall_clock_offset_ms;   /* epoch − monótono; 0 = sin hora */
static bool s_wall_clock_available;
static extraction_summary_t s_pending[EXTRACTION_PENDING_CAPACITY];
static uint32_t s_pending_count;
static uint32_t s_pending_dropped;
static uint32_t s_message_sequence;
/* Una preparación se declara medida solo si todas sus muestras lo fueron. */
static bool s_session_measured;
static condition_monitor_t s_condition_monitor;
/* Preparación que el arranque encontró abierta, a la espera de hora. */
static open_extraction_marker_t s_interrupted;
static bool s_interrupted_pending;
/*
 * El cuerpo y la presión mientras la máquina está lista, para anteponer a la
 * serie el minuto previo a pulsar (enmienda del 2026-10-19 a ADR-012 § 4.1).
 * Solo lo toca esta tarea: se observa en «lista», se copia al abrir la
 * preparación y se vacía en cada cambio de estado aceptado.
 */
static pre_start_history_t s_pre_start;

static void flush_interrupted(void);

static uint64_t monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / UINT64_C(1000);
}

/*
 * Hora de pared del instante monótono indicado. Permite fechar un registro que
 * se cerró antes de conocer la hora: el desfase medido se aplica al momento en
 * que ocurrió la preparación, no al momento en que se serializa.
 */
static uint64_t wall_clock_for(uint64_t monotonic_at_ms)
{
    return s_wall_clock_available ? s_wall_clock_offset_ms + monotonic_at_ms : 0U;
}

static uint64_t wall_clock_ms(void)
{
    return wall_clock_for(monotonic_ms());
}

static void publish_pending_counters(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.pending_without_clock = s_pending_count;
    s_status.dropped_without_clock = s_pending_dropped;
    portEXIT_CRITICAL(&s_status_lock);
}

/*
 * Serializa un resumen cerrado y lo entrega a la cola persistente.
 *
 * Devuelve false cuando no hay hora de pared fiable, de modo que el registro
 * quede pendiente en lugar de publicarse con una fecha inventada.
 */
/* El acta de temperatura compilada, o NULL en un firmware sin ella. */
static const char *commissioned_temperature_plant(void)
{
#if HAVE_COMMISSIONED_TEMPERATURE
    return COMMISSIONED_TEMPERATURE_IDENTIFIER;
#else
    return NULL;
#endif
}

static bool enqueue_summary(const extraction_summary_t *summary)
{
    if (s_outbox == NULL) {
        return false;
    }
    /* La calibración solo cambia al reiniciar (`K,set` rige desde el siguiente
     * arranque), así que la vigente al cerrar es la que midió toda la sesión. */
    sensor_calibration_t calibration = {0};
    const bool calibration_known = acquisition_runtime_get_calibration(&calibration);
    const extraction_message_context_t context = {
        .device_id = CONFIG_CAFE_DEVICE_ID,
        .barista = NULL,
        .measured = s_session_measured,
        .sequence = s_message_sequence + 1U,
        .epoch_ms_at_finish = wall_clock_for(summary->finished_monotonic_ms),
        .calibration_id = calibration_known ? calibration.id : NULL,
        .calibration_version = calibration_known ? calibration.version : 0U,
        .firmware_version = esp_app_get_description()->version,
        /* El acta compilada rige desde el arranque y no cambia sin reiniciar,
         * así que es con la que se convirtió el agua de toda la sesión. */
        .temperature_plant = commissioned_temperature_plant(),
        .water_reference = water_reference_commissioned(),
    };
    char message_id[EXTRACTION_MESSAGE_ID_LENGTH + 1U];
    char json[NVS_OUTBOX_MAX_JSON_BYTES + 1U];
    extraction_message_status_t status =
        extraction_message_identifier(summary, message_id, sizeof(message_id));
    if (status == EXTRACTION_MESSAGE_OK) {
        status = extraction_message_build(summary, &context, json, sizeof(json), NULL);
    }
    if (status != EXTRACTION_MESSAGE_OK) {
        if (status != EXTRACTION_MESSAGE_TIME_BASE_UNAVAILABLE) {
            ESP_LOGE(TAG, "No se pudo serializar %s: %s", summary->id,
                     extraction_message_status_name(status));
            portENTER_CRITICAL(&s_status_lock);
            s_status.enqueue_failures++;
            portEXIT_CRITICAL(&s_status_lock);
        }
        return false;
    }

    bool inserted = false;
    const esp_err_t result = nvs_outbox_enqueue(s_outbox, message_id, json, &inserted);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo encolar %s: %s", message_id, esp_err_to_name(result));
        portENTER_CRITICAL(&s_status_lock);
        s_status.enqueue_failures++;
        portEXIT_CRITICAL(&s_status_lock);
        return false;
    }
    s_message_sequence++;
    portENTER_CRITICAL(&s_status_lock);
    s_status.enqueued++;
    portEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "Registro %s %s en la cola persistente", message_id,
             inserted ? "encolado" : "ya presente");
    return true;
}

/*
 * Conserva un registro cerrado que todavía no pudo entregarse. Al alcanzar la
 * capacidad se descarta el más reciente y no el más antiguo, igual que hace la
 * cola persistente: la evidencia que ya esperaba no se sacrifica por la que
 * acaba de llegar.
 */
/*
 * Publica un cambio de estado como evidencia. A diferencia del resumen de una
 * preparación, un evento sin hora no se retiene: describe un instante que ya
 * pasó y cuyo valor está en cuándo ocurrió, de modo que fecharlo más tarde con
 * la hora de otro momento sería peor que no publicarlo.
 */
static void enqueue_event(const operational_transition_t *transition)
{
    if (s_outbox == NULL) {
        return;
    }
    event_message_context_t context = {
        .device_id = CONFIG_CAFE_DEVICE_ID,
        .sequence = s_message_sequence + 1U,
        .epoch_ms = wall_clock_ms(),
        .transition = *transition,
    };
    if (s_session.active || transition->extraction_finished) {
        context.preparation_id = s_session.summary.id;
    }

    char json[NVS_OUTBOX_MAX_JSON_BYTES + 1U];
    char message_id[EVENT_MESSAGE_ID_LENGTH + 1U];
    const event_message_status_t status =
        event_message_build(&context, json, sizeof(json), message_id, sizeof(message_id));
    if (status != EVENT_MESSAGE_OK) {
        if (status != EVENT_MESSAGE_NOT_REPORTABLE &&
            status != EVENT_MESSAGE_TIME_BASE_UNAVAILABLE) {
            ESP_LOGW(TAG, "Evento no serializable: %s", event_message_status_name(status));
        }
        return;
    }

    bool inserted = false;
    if (nvs_outbox_enqueue(s_outbox, message_id, json, &inserted) != ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.enqueue_failures++;
        portEXIT_CRITICAL(&s_status_lock);
        return;
    }
    s_message_sequence++;
    portENTER_CRITICAL(&s_status_lock);
    s_status.enqueued++;
    portEXIT_CRITICAL(&s_status_lock);
}

/*
 * Entrega a la cola la serie medida de la preparación recién cerrada.
 *
 * La serie es evidencia y no observación: es aquello contra lo que se compara
 * el modelo (ADR-012 § 4.1). Viajaba únicamente en la telemetría, que no ocupa
 * la cola y se pierde sin enlace, de modo que una preparación hecha con la
 * plataforma apagada se archivaba entera y sin ninguna de sus muestras: quedaba
 * registrada y fuera de la comparación físico-virtual. Se observó en la placa
 * el 15/09/2026.
 *
 * Se encola **después** del resumen a propósito. La cola descarta lo más
 * reciente cuando se llena, así que este orden hace que lo que se pierda ante
 * una cola saturada sea la serie y nunca la ficha de la preparación.
 *
 * Sin hora de pared no se compone: el sobre exige un instante real. A
 * diferencia del resumen, la serie no puede retenerse a la espera —las muestras
 * viven en la sesión, que la preparación siguiente sobrescribe, y no caben en
 * ningún sitio donde esperar—. Es el límite declarado en ADR-013 § 7 aplicado a
 * la serie: un arranque en frío sin plataforma pierde la serie de las
 * preparaciones hechas antes de conocer la hora, aunque conserve sus resúmenes.
 */
static void enqueue_series(void)
{
    if (s_outbox == NULL) {
        return;
    }
    const uint32_t chunks = series_message_chunk_count(&s_session);
    if (chunks == 0U) {
        return;  /* una preparación demasiado corta no deja serie que enviar */
    }

    const series_message_context_t context = {
        .device_id = CONFIG_CAFE_DEVICE_ID,
        .sequence = 0U,
        .epoch_ms = wall_clock_for(s_session.summary.finished_monotonic_ms),
        .measured = s_session_measured,
    };
    if (context.epoch_ms == 0U) {
        ESP_LOGW(TAG,
                 "Sin hora al cerrar %s: se conserva el resumen y se pierde su serie",
                 s_session.summary.id);
        return;
    }

    uint32_t enqueued = 0U;
    for (uint32_t chunk = 0U; chunk < chunks; ++chunk) {
        series_message_context_t chunk_context = context;
        chunk_context.sequence = s_message_sequence + 1U;

        char json[NVS_OUTBOX_MAX_JSON_BYTES + 1U];
        char message_id[SERIES_MESSAGE_ID_LENGTH + 1U];
        const series_message_status_t status =
            series_message_build(&s_session, &chunk_context, chunk, json, sizeof(json),
                                 message_id, sizeof(message_id));
        if (status != SERIES_MESSAGE_OK) {
            ESP_LOGW(TAG, "Tramo %" PRIu32 " de la serie no serializable: %s", chunk,
                     series_message_status_name(status));
            break;
        }

        bool inserted = false;
        if (nvs_outbox_enqueue(s_outbox, message_id, json, &inserted) != ESP_OK) {
            /* La cola llena detiene el envío de la serie y no el de nada más:
             * los tramos que falten se declaran ausentes por `chunkCount`. */
            ESP_LOGW(TAG, "La cola no admitió el tramo %" PRIu32 " de %" PRIu32, chunk, chunks);
            portENTER_CRITICAL(&s_status_lock);
            s_status.enqueue_failures++;
            portEXIT_CRITICAL(&s_status_lock);
            break;
        }
        s_message_sequence++;
        portENTER_CRITICAL(&s_status_lock);
        s_status.enqueued++;
        portEXIT_CRITICAL(&s_status_lock);
        ++enqueued;
    }

    ESP_LOGI(TAG, "Serie de %s encolada: %" PRIu32 " de %" PRIu32 " tramos cada %u ms",
             s_session.summary.id, enqueued, chunks, (unsigned)SERIES_MESSAGE_INTERVAL_MS);
}

static void retain_summary(const extraction_summary_t *summary)
{
    if (s_pending_count >= EXTRACTION_PENDING_CAPACITY) {
        s_pending_dropped++;
        publish_pending_counters();
        ESP_LOGE(TAG,
                 "Registro %s descartado: %" PRIu32 " esperan hora y no cabe otro",
                 summary->id, s_pending_count);
        return;
    }
    s_pending[s_pending_count] = *summary;
    s_pending_count++;
    publish_pending_counters();
    /* Se retiene por no tener hora o por tener la cola llena: enqueue_summary
     * no distingue, y el mensaje anterior («pendiente de sello temporal»)
     * atribuía a la hora lo que el 2026-10-19 fue la cola. */
    ESP_LOGW(TAG, "Registro %s retenido en memoria (sin hora o con la cola llena); %" PRIu32
             " en espera", summary->id, s_pending_count);
}

/*
 * Entrega en orden de llegada los registros retenidos y conserva los que no
 * pudieron encolarse. Cada uno se fecha con su propio instante de cierre.
 */
static void flush_pending(void)
{
    if (s_pending_count == 0U) {
        return;
    }
    uint32_t delivered = 0U;
    while (delivered < s_pending_count && enqueue_summary(&s_pending[delivered])) {
        delivered++;
    }
    if (delivered == 0U) {
        return;
    }
    const uint32_t remaining = s_pending_count - delivered;
    for (uint32_t index = 0U; index < remaining; index++) {
        s_pending[index] = s_pending[delivered + index];
    }
    s_pending_count = remaining;
    publish_pending_counters();
    ESP_LOGI(TAG, "Entregados %" PRIu32 " registros en espera; quedan %" PRIu32,
             delivered, s_pending_count);
}

void extraction_runtime_set_wall_clock(uint64_t epoch_ms)
{
    if (epoch_ms < EXTRACTION_MESSAGE_MINIMUM_EPOCH_MS) {
        ESP_LOGW(TAG, "Hora de pared rechazada por no ser plausible");
        return;
    }
    const uint64_t now = monotonic_ms();
    s_wall_clock_offset_ms = epoch_ms > now ? epoch_ms - now : 0U;
    s_wall_clock_available = s_wall_clock_offset_ms > 0U;
    ESP_LOGI(TAG, "Hora de pared establecida; registros en espera=%" PRIu32,
             s_pending_count);
    flush_interrupted();
    flush_pending();
}

bool extraction_runtime_has_wall_clock(void)
{
    return s_wall_clock_available;
}

uint64_t extraction_runtime_wall_clock_now(void)
{
    return wall_clock_ms();
}

uint32_t extraction_runtime_active_elapsed_ms(void)
{
    /* El resumen solo fija su duración al cerrarse, de modo que durante la
     * preparación el tiempo transcurrido se calcula contra su instante de
     * inicio. */
    if (!s_session.active) {
        return 0U;
    }
    const uint64_t elapsed = monotonic_ms() - s_session.summary.started_monotonic_ms;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static void publish_active(void)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.extraction_active = s_session.active;
    s_status.active = s_session.summary;
    portEXIT_CRITICAL(&s_status_lock);
}

/* Sin contador guardado se empieza en cero, que es lo que declara que este
 * dispositivo no ha registrado ninguna preparación todavía. */
static uint32_t load_session_sequence(void)
{
    nvs_handle_t handle;
    if (nvs_open(EXTRACTION_SEQUENCE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return 0U;
    }
    uint32_t stored = 0U;
    if (nvs_get_u32(handle, EXTRACTION_SEQUENCE_KEY, &stored) != ESP_OK) {
        stored = 0U;
    }
    nvs_close(handle);
    return stored;
}

/*
 * El número y la marca de la preparación que se abre van en la misma
 * escritura: cada escritura en flash detiene la caché de los dos núcleos, y
 * abrir una preparación es justo cuando arranca el lazo de presión. Sin marca
 * válida se guarda solo el número.
 */
static bool store_session_opening(uint32_t value, const open_extraction_marker_t *marker)
{
    nvs_handle_t handle;
    if (nvs_open(EXTRACTION_SEQUENCE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t result = nvs_set_u32(handle, EXTRACTION_SEQUENCE_KEY, value);
    if (result == ESP_OK && marker != NULL) {
        result = nvs_set_blob(handle, EXTRACTION_OPEN_KEY, marker, sizeof(*marker));
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result == ESP_OK;
}

static void clear_open_marker(void)
{
    nvs_handle_t handle;
    if (nvs_open(EXTRACTION_SEQUENCE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    const esp_err_t result = nvs_erase_key(handle, EXTRACTION_OPEN_KEY);
    if (result == ESP_OK) {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

static bool load_open_marker(open_extraction_marker_t *marker)
{
    *marker = (open_extraction_marker_t){0};
    nvs_handle_t handle;
    if (nvs_open(EXTRACTION_SEQUENCE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(*marker);
    const esp_err_t result = nvs_get_blob(handle, EXTRACTION_OPEN_KEY, marker, &size);
    nvs_close(handle);
    /* Otro tamaño es otro formato: se descarta, no se reinterpreta. */
    if (result != ESP_OK || size != sizeof(*marker) || !open_extraction_is_valid(marker)) {
        *marker = (open_extraction_marker_t){0};
        return false;
    }
    return true;
}

/*
 * Envía la preparación que el arranque encontró abierta. Necesita hora: el
 * sobre la exige, y la del aviso es la del momento en que se recupera. La
 * marca solo se borra cuando el registro ya está en la cola, de modo que un
 * segundo corte antes de eso no la pierde.
 */
static void flush_interrupted(void)
{
    if (!s_interrupted_pending || s_outbox == NULL || !s_wall_clock_available) {
        return;
    }
    extraction_summary_t summary;
    if (!open_extraction_recover(&s_interrupted, &summary)) {
        s_interrupted_pending = false;
        return;
    }
    const extraction_message_context_t context = {
        .device_id = CONFIG_CAFE_DEVICE_ID,
        .barista = NULL,
        .measured = s_interrupted.source == OPEN_EXTRACTION_SOURCE_MEASURED,
        .sequence = s_message_sequence + 1U,
        .epoch_ms_at_finish = wall_clock_ms(),
        .epoch_ms_at_start = s_interrupted.started_epoch_ms,
        .calibration_id =
            s_interrupted.calibration_id[0] != '\0' ? s_interrupted.calibration_id : NULL,
        .calibration_version = s_interrupted.calibration_version,
        .firmware_version =
            s_interrupted.firmware_version[0] != '\0' ? s_interrupted.firmware_version : NULL,
        /* Sin recta: la sesión pudo abrirse con otro firmware, y su acta no se
         * guardó al abrirla. Va null, no la vigente. */
    };
    char message_id[EXTRACTION_MESSAGE_ID_LENGTH + 1U];
    char json[NVS_OUTBOX_MAX_JSON_BYTES + 1U];
    extraction_message_status_t status =
        extraction_message_identifier(&summary, message_id, sizeof(message_id));
    if (status == EXTRACTION_MESSAGE_OK) {
        status = extraction_message_build(&summary, &context, json, sizeof(json), NULL);
    }
    if (status != EXTRACTION_MESSAGE_OK) {
        ESP_LOGE(TAG, "No se pudo serializar la preparación cortada %s: %s", summary.id,
                 extraction_message_status_name(status));
        s_interrupted_pending = false;
        return;
    }
    bool inserted = false;
    if (nvs_outbox_enqueue(s_outbox, message_id, json, &inserted) != ESP_OK) {
        return;  /* se reintenta con la hora siguiente; la marca sigue en NVS */
    }
    s_message_sequence++;
    s_interrupted_pending = false;
    clear_open_marker();
    ESP_LOGW(TAG, "Preparación %s cortada por la energía: registrada como incompleta",
             summary.id);
}

/*
 * El desplazamiento del primer uso que rige al abrir es el de toda la
 * preparación: el supervisor no lo retira hasta que esta termina (ADR-009,
 * enmienda del 2026-10-19). Sin estado del supervisor no se anota y el resumen
 * no lo declara.
 *
 * Aparte y sin expandir en línea para que la copia del estado del supervisor no
 * ocupe el marco de `begin_extraction` en una pila de 4 KB.
 */
__attribute__((noinline)) static void note_first_use_offset(void)
{
    supervisor_status_t supervisor = {0};
    if (supervisor_runtime_get_status(&supervisor)) {
        (void)extraction_session_note_boiler_offset(&s_session, supervisor.first_use_offset_c);
        (void)extraction_session_note_feedforward_gain(
            &s_session, heater_feedforward_gain_for(heater_feedforward_configured_gain(NULL),
                                                    supervisor.first_use_feedforward_gain));
    }
}

static void begin_extraction(void)
{
    extraction_profile_t profile = {0};
    if (!profile_runtime_get_effective(&profile)) {
        ESP_LOGE(TAG, "No existe configuración efectiva para registrar");
        return;
    }

    char session_id[EXTRACTION_SESSION_ID_LENGTH];
    /* El número se guarda antes de usarlo: si la corriente se corta entre abrir
     * la preparación y cerrarla, el siguiente arranque continúa por el
     * siguiente y no repite este. Un fallo al guardar no cancela la
     * preparación —perderla sería peor que arriesgar el número—, pero se
     * advierte, porque a partir de ahí la unicidad ya no está garantizada. */
    const uint32_t sequence = s_session_sequence + 1U;
    snprintf(session_id, sizeof(session_id), "sim-%08" PRIu32, sequence);
    /* Lo que había al abrir: al recuperarla, el arranque ya es otro. */
    process_snapshot_t process = {0};
    const bool process_known = process_variables_get(&process);
    sensor_calibration_t calibration = {0};
    const bool calibration_known = acquisition_runtime_get_calibration(&calibration);
    const open_extraction_opening_t opening = {
        .session_id = session_id,
        .profile = &profile,
        .started_epoch_ms = wall_clock_ms(),
        .source = !process_known                             ? OPEN_EXTRACTION_SOURCE_UNKNOWN
                  : process.source == PROCESS_SOURCE_MEASURED ? OPEN_EXTRACTION_SOURCE_MEASURED
                  : process.source == PROCESS_SOURCE_SIMULATED
                      ? OPEN_EXTRACTION_SOURCE_SIMULATED
                      : OPEN_EXTRACTION_SOURCE_UNKNOWN,
        .calibration_id = calibration_known ? calibration.id : NULL,
        .calibration_version = calibration_known ? calibration.version : 0U,
        .firmware_version = esp_app_get_description()->version,
    };
    open_extraction_marker_t marker;
    const bool marked = open_extraction_mark(&marker, &opening);
    if (!store_session_opening(sequence, marked ? &marker : NULL)) {
        ESP_LOGW(TAG,
                 "No se pudo guardar el número de preparación; un reinicio "
                 "podría repetir %" PRIu32,
                 sequence);
    }
    s_session_sequence = sequence;
    s_session_measured = false;
    if (!extraction_session_start(&s_session, session_id, &profile,
                                  monotonic_ms())) {
        ESP_LOGE(TAG, "No se pudo abrir la sesión de extracción");
        return;
    }
    note_first_use_offset();
    /* El minuto previo solo se antepone si su origen es el de la preparación;
     * sin origen conocido al abrir no se antepone nada. Si se toma y era
     * medido, la preparación cuenta como medida aunque se corte antes de su
     * primera muestra: de lo contrario la serie declararía «simulación» sobre
     * muestras que salieron de los sensores. */
    const bool opening_measured = process_known && process.source == PROCESS_SOURCE_MEASURED;
    const bool opening_known = process_known && (process.source == PROCESS_SOURCE_MEASURED ||
                                                 process.source == PROCESS_SOURCE_SIMULATED);
    const uint32_t previous =
        opening_known ? extraction_session_take_pre_start(&s_session, &s_pre_start, opening_measured)
                      : 0U;
    if (previous > 0U && opening_measured) {
        s_session_measured = true;
    }
    ESP_LOGI(TAG, "Minuto previo de %s: %" PRIu32 " muestras", session_id, previous);
    publish_active();
    ESP_LOGI(TAG, "Extracción %s iniciada con perfil=%s v%" PRIu32,
             session_id, profile.id, profile.version);
}

/*
 * Ofrece al anillo del minuto previo el estado vigente del proceso. Sin origen
 * declarado no se observa: el hueco que deja rompe la racha, que es lo que
 * corresponde a un rato en que no se sabe qué se medía.
 */
static void observe_pre_start(void)
{
    process_snapshot_t process = {0};
    if (!process_variables_get(&process) ||
        (process.source != PROCESS_SOURCE_MEASURED &&
         process.source != PROCESS_SOURCE_SIMULATED)) {
        return;
    }
    const pre_start_observation_t observation = {
        .temperature_c = process.temperature_c,
        .pressure_bar = process.pressure_bar,
        .temperature_valid = signal_quality_is_usable(process.temperature_quality),
        .pressure_valid = signal_quality_is_usable(process.pressure_quality),
        .measured = process.source == PROCESS_SOURCE_MEASURED,
    };
    (void)pre_start_history_observe(&s_pre_start, (uint32_t)monotonic_ms(), &observation);
}

static void finish_extraction(extraction_termination_t termination)
{
    if (!extraction_session_finish(&s_session, termination, monotonic_ms())) {
        ESP_LOGE(TAG, "No se pudo cerrar la sesión de extracción");
        return;
    }

    portENTER_CRITICAL(&s_status_lock);
    s_status.extraction_active = false;
    s_status.active = s_session.summary;
    s_status.last_completed = s_session.summary;
    s_status.completed_extraction_available = true;
    portEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG,
             "Extracción %s finalizada causa=%s duración=%" PRIu32
             "ms muestras=%" PRIu32,
             s_session.summary.id,
             extraction_termination_name(termination),
             s_session.summary.duration_ms,
             s_session.summary.sample_count);

    /* El registro debe sobrevivir a un reinicio: se entrega a la cola en NVS.
     * Tras él va la serie medida, remuestreada a la rejilla de comparación: las
     * muestras registradas no caben enteras en la cola, pero la rejilla sí, y
     * sin ella la preparación queda fuera de la comparación físico-virtual. */
    flush_pending();
    if (enqueue_summary(&s_session.summary)) {
        enqueue_series();
    } else {
        retain_summary(&s_session.summary);
    }
    /* Cerrada, ya no es una preparación cortada. Si esperaba hora, un corte
     * ahora la pierde igual que antes de existir la marca (ADR-013 § 7). */
    clear_open_marker();
}

/*
 * Registra una muestra del proceso, venga de los sensores o de la planta
 * simulada. El origen no se pierde: se conserva en la sesión y viaja hasta el
 * registro entregado, para que nadie confunda una simulación con una medición.
 */
static void record_process_sample(void)
{
    if (!s_session.active) {
        return;
    }

    process_snapshot_t process = {0};
    if (!process_variables_get(&process)) {
        return;
    }
    /* La actuación virtual solo existe mientras la planta sea simulada. */
    simulation_snapshot_t plant = {0};
    const bool simulated = process.source == PROCESS_SOURCE_SIMULATED &&
                           simulation_runtime_get_snapshot(&plant);

    const uint64_t now = monotonic_ms();
    const uint64_t elapsed = now - s_session.summary.started_monotonic_ms;
    const extraction_sample_t sample = {
        .elapsed_ms = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed,
        .observed_temperature_c = process.temperature_c,
        .observed_pressure_bar = process.pressure_bar,
        .target_temperature_c =
            s_session.summary.effective_profile.temperature_setpoint_c,
        .target_pressure_bar =
            s_session.summary.effective_profile.pressure_setpoint_bar,
        .simulated_pump_duty = simulated ? plant.simulated_pump_duty : 0.0F,
        .temperature_valid = signal_quality_is_usable(process.temperature_quality),
        .pressure_valid = signal_quality_is_usable(process.pressure_quality),
        .simulated_heater_on = simulated && plant.simulated_heater_on,
    };
    if (process.source == PROCESS_SOURCE_MEASURED) {
        s_session_measured = true;
    }
    (void)extraction_session_record(&s_session, &sample);
    publish_active();
}

/*
 * Publica los avisos de condición que no nacen de un cambio de estado. El sistema
 * debe informar de una condición insegura, y el guardián puede cortar las
 * salidas sin que la máquina de estados se mueva: un sensor que se suelta en
 * reposo no produce transición alguna.
 *
 * El veredicto llega completo en una sola lectura del supervisor, porque leer
 * bomba y calentador por separado podría describir una combinación que nunca
 * existió.
 */
static void publish_condition_event(void)
{
    supervisor_status_t supervisor = {0};
    if (!supervisor_runtime_get_status(&supervisor)) {
        return;
    }
    const condition_event_t condition =
        condition_monitor_observe(&s_condition_monitor, &supervisor.actuation);
    if (condition.kind == CONDITION_EVENT_NONE || s_outbox == NULL) {
        return;
    }

    condition_event_context_t context = {
        .device_id = CONFIG_CAFE_DEVICE_ID,
        .sequence = s_message_sequence + 1U,
        .epoch_ms = wall_clock_ms(),
        .state = supervisor.state,
        .condition = condition,
    };
    if (s_session.active) {
        context.preparation_id = s_session.summary.id;
    }

    char json[NVS_OUTBOX_MAX_JSON_BYTES + 1U];
    char message_id[EVENT_MESSAGE_ID_LENGTH + 1U];
    const event_message_status_t status = event_message_build_condition(
        &context, json, sizeof(json), message_id, sizeof(message_id));
    if (status != EVENT_MESSAGE_OK) {
        return;
    }

    bool inserted = false;
    if (nvs_outbox_enqueue(s_outbox, message_id, json, &inserted) != ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.enqueue_failures++;
        portEXIT_CRITICAL(&s_status_lock);
        return;
    }
    s_message_sequence++;
    portENTER_CRITICAL(&s_status_lock);
    s_status.enqueued++;
    portEXIT_CRITICAL(&s_status_lock);
    ESP_LOGW(TAG, "Aviso de condición publicado: %s", message_id);
}

static void extraction_task(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t pending_retry_elapsed_ms = 0U;
    ESP_LOGI(TAG, "Registrador de extracciones iniciado; núcleo=%d",
             (int)xPortGetCoreID());

    while (true) {
        operational_transition_t transition = {0};
        while (operational_runtime_receive_transition(&transition, 0U)) {
            if (transition.extraction_started) {
                begin_extraction();
            }
            if (transition.extraction_finished) {
                finish_extraction(transition.extraction_termination);
            }
            /* El evento se publica después de abrir o cerrar la preparación,
             * para que pueda referirse a ella. Es evidencia: va a la cola. */
            enqueue_event(&transition);
            /* Todo cambio de estado aceptado empieza una racha nueva: al salir
             * de «lista» la preparación ya copió su minuto (arriba), y al
             * volver a ella lo anterior no es máquina lista. Se vacía por la
             * transición y no al ver otro estado en el bucle, porque el estado
             * cambia antes de que llegue la transición y vaciar entonces
             * dejaría a la preparación sin su minuto. Si una transición se
             * pierde, el hueco de observación rompe la racha igualmente. */
            if (transition.accepted && transition.from != transition.to) {
                pre_start_history_reset(&s_pre_start);
            }
        }

        publish_condition_event();

        if (s_pending_count > 0U && s_wall_clock_available) {
            pending_retry_elapsed_ms += EXTRACTION_TASK_PERIOD_MS;
            if (pending_retry_elapsed_ms >= EXTRACTION_PENDING_RETRY_MS) {
                pending_retry_elapsed_ms = 0U;
                flush_pending();
            }
        } else {
            pending_retry_elapsed_ms = 0U;
        }

        const operational_state_t state = operational_runtime_state();
        if (state == OP_STATE_EXTRACTION) {
            record_process_sample();
        } else if (state == OP_STATE_READY) {
            observe_pre_start();
        }
        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(EXTRACTION_TASK_PERIOD_MS));
    }
}

esp_err_t extraction_runtime_start(nvs_outbox_t *outbox)
{
    if (s_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_outbox = outbox;
    condition_monitor_init(&s_condition_monitor);
    s_session_sequence = load_session_sequence();
    ESP_LOGI(TAG, "Preparaciones registradas por este dispositivo=%" PRIu32,
             s_session_sequence);
    if (load_open_marker(&s_interrupted)) {
#if CONFIG_CAFE_REPORT_INTERRUPTED_EXTRACTIONS
        s_interrupted_pending = true;
        ESP_LOGW(TAG, "La preparación %s quedó abierta: se enviará como cortada por la energía",
                 s_interrupted.session_id);
#else
        ESP_LOGW(TAG,
                 "La preparación %s quedó abierta y no se informa: "
                 "CAFE_REPORT_INTERRUPTED_EXTRACTIONS desactivado",
                 s_interrupted.session_id);
#endif
    }

    const BaseType_t created = xTaskCreatePinnedToCore(
        extraction_task, "extraction_rec", EXTRACTION_TASK_STACK_BYTES,
        NULL, EXTRACTION_TASK_PRIORITY, &s_task_handle, 0);
    if (created != pdPASS) {
        s_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool extraction_runtime_get_status(extraction_runtime_status_t *status)
{
    if (status == NULL || s_task_handle == NULL) {
        return false;
    }
    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return true;
}
