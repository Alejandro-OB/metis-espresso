#pragma once

/*
 * Registro obligatorio del lazo (ADR-007 § 3.6 y ADR-009 § 3.9).
 *
 * Cada prueba del lazo debe conservar, con tiempo monotónico, lo suficiente
 * para reproducir después lo que hizo el control: las tres señales de la cadena
 * de medida como magnitudes distintas, las dos referencias también distintas,
 * los términos intermedios del PI antes y después de limitar, lo que se pidió
 * al actuador frente a lo que realmente se aplicó, y el estado, la calidad y el
 * veredicto bajo los que se decidió. Sin ese registro, la puesta en marcha que
 * ambos ADR describen —comparar constantes de filtro fuera de línea, estimar
 * ganancia y retardo, sintonizar por SIMC— no tiene sobre qué trabajar.
 *
 * Este módulo es puro. No reserva memoria, no conoce el reloj, no escribe en
 * ningún sitio y no sabe qué es una tarea: define **qué** se guarda, **cómo se
 * ordena en la cola** y **cómo se escribe cada línea**. El soporte físico
 * —la PSRAM donde vive la cola y la tarea que la vacía por la consola— es
 * `control_log_runtime`. Esa separación es la misma de `zero_cross_monitor` y
 * existe por la misma razón: el formato y la política de desbordamiento son lo
 * que puede equivocarse, y se prueban en el anfitrión.
 *
 * ## La política ante desbordamiento
 *
 * Si la cola se llena, **se descarta el registro nuevo y se cuenta**. La
 * alternativa —sobrescribir el más antiguo— conserva lo reciente, pero deja al
 * lector una laguna en medio del flujo que solo delata la numeración, y obliga
 * a que productor y consumidor se disputen la misma posición. Descartar lo
 * nuevo mantiene estable lo ya encolado y convierte la saturación en
 * contrapresión declarada: el contador de pérdidas se emite como una línea de
 * evento propia, de modo que un registro incompleto nunca lo parece completo.
 *
 * ## El formato
 *
 * Texto separado por comas, una línea por registro, con la primera columna
 * indicando de qué clase de línea se trata: `P` una muestra del lazo de
 * presión, `T` una del lazo de temperatura, `E` un evento. Las cabeceras se
 * emiten una vez, precedidas de `#`, con el mismo indicador.
 *
 * Que cada línea lleve su clase delante no es adorno: la salida comparte el
 * puerto serie con los mensajes de `ESP_LOG`, y así una captura se separa con
 * una sola expresión regular sin tener que silenciar el resto del diagnóstico.
 *
 * Los enumerados se escriben **por su nombre y no por su código**. Un registro
 * que meses después dijera `verdict=7` no serviría para nada, y el proyecto ya
 * tiene funciones de nombre para todos ellos.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "actuation_guard.h"
#include "control_tuning.h"
#include "operational_fsm.h"
#include "process_variables.h"
#include "signal_quality.h"
#include "stimulus_sequence.h"

/* Holgura suficiente para la línea más larga con todos los nombres extensos. */
#define CONTROL_LOG_LINE_BYTES 384U

typedef enum {
    CONTROL_LOG_LOOP_PRESSURE = 0,
    CONTROL_LOG_LOOP_TEMPERATURE,
} control_log_loop_t;

/*
 * La cadena de medida completa, con sus cuatro eslabones separados.
 *
 * ADR-007 § 3.3 y ADR-009 § 3.6 exigen conservar la muestra cruda, la magnitud
 * convertida y la filtrada como señales distintas, precisamente para que el
 * filtro no pueda ocultar una entrada inválida o saturada. Se añade el código
 * del convertidor porque una saturación del ADS1115 se reconoce ahí y en
 * ningún otro sitio.
 */
typedef struct {
    int16_t counts;    /* código del convertidor */
    float volts;       /* tensión en la entrada analógica */
    float converted;   /* magnitud en su unidad, sin el filtro del lazo */
    float filtered;    /* la que ve el controlador */
} control_log_signal_t;

/* Lo que devolvió el PI, término a término. Se corresponde con `pi_result_t`. */
typedef struct {
    float authorized_setpoint;  /* la referencia validada */
    float internal_setpoint;    /* la que aplicó el limitador de cambio */
    float error;
    float proportional;
    float integral;
    float feedforward;
    float unlimited;            /* la suma antes de limitar */
    float output;               /* la salida aplicada */
    bool saturated_high;
    bool saturated_low;
    bool integral_held;         /* el anti-windup impidió acumular */
} control_log_control_t;

/* Lo propio del lazo de presión: el planificador PSM y el servicio de la bomba. */
typedef struct {
    uint8_t density;          /* % solicitado, ya cuantizado */
    uint32_t opportunities;   /* cruces ofrecidos al planificador */
    uint32_t emitted;         /* actuaciones realmente emitidas */
    uint32_t residue;         /* acumulador del PSM */
    uint32_t enabled_ms;      /* habilitación continua de la bomba */
    uint32_t rest_ms;         /* reposo acumulado */
} control_log_pressure_t;

/* Lo propio del lazo de temperatura: la modulación por ciclo integral. */
typedef struct {
    uint32_t window_ms;       /* ventana de modulación vigente */
    uint32_t requested_ms;    /* conducción que pide la demanda */
    uint32_t applied_ms;      /* conducción realmente aplicada */
    int32_t residue_ms;       /* arrastrado a la ventana siguiente */
    float rise_rate_c_s;      /* velocidad de ascenso observada */
    /* Fase del arranque rápido (ADR-009, enmienda del 2026-10-11): qué gobernaba
     * el calentador en este ciclo. Literal estático; nulo se escribe «-». */
    const char *fast_start;
} control_log_temperature_t;

/*
 * Longitud del identificador de calibración que viaja en cada línea.
 *
 * Se copia en vez de guardar el puntero que entrega `scale_calibration_t`: la
 * cola se drena en otra tarea y mucho después, y un puntero obligaría a que la
 * cadena viviera tanto como el registro más viejo. Dieciséis bytes bastan para
 * una etiqueta de ensayo y no dependen de nadie.
 */
#define CONTROL_LOG_CALIBRATION_ID_BYTES 16U

/*
 * Una conversión de la báscula de ensayo.
 *
 * Existe para la campaña de caudal de ADR-006: el caudal es la derivada de la
 * masa, y derivar exige la **curva**, no la lectura actual. Antes de esto la
 * báscula solo mantenía su último valor en una estructura consultable, con lo que
 * se podía preguntar cuánta masa había pero no reconstruir cómo llegó ahí.
 *
 * Va en el mismo registro que los dos lazos, y no en un canal aparte, por una
 * razón concreta: comparte con ellos el **reloj monotónico**. Correlacionar
 * después dos capturas con bases de tiempo distintas introduciría un error de
 * alineamiento justo en el dato que sostiene la identificación hidráulica, que
 * es donde menos se puede permitir.
 *
 * La báscula no es una función de la cafetera sino instrumentación, de modo que
 * su ausencia no altera nada: sin conversiones simplemente no hay líneas `S`.
 */
typedef struct {
    uint64_t monotonic_us;
    uint32_t sequence;       /* conversiones de la báscula; una laguna delata pérdidas */
    int32_t counts;          /* siempre presente, calibrada o no */
    float grams;             /* solo significa algo si `mass_available` */
    bool mass_available;
    bool settled;            /* la ventana de estabilidad está llena y dentro de tolerancia */
    uint32_t spread_counts;  /* recorrido de esa ventana; nunca negativo */
    signal_quality_t quality;
    char calibration_id[CONTROL_LOG_CALIBRATION_ID_BYTES];
} control_log_scale_t;

typedef struct {
    uint64_t monotonic_us;
    uint32_t sequence;          /* ejecución del lazo; una laguna delata pérdidas */
    control_log_loop_t loop;
    operational_state_t state;
    process_source_t source;    /* medido o simulado, nunca implícito */
    signal_quality_t quality;
    actuation_verdict_t verdict;
    bool commissioned;          /* el lazo tiene parámetros puestos en servicio */
    /*
     * Sintonía con la que se reguló: `bench-v1` o el acta de la planta
     * identificada (ADR-020 § 4.2). Se copia por la misma razón que el
     * identificador de la báscula: la cola se drena mucho después y en otra
     * tarea. Así cada serie dice con qué planta se reguló sin consultar nada.
     */
    char tuning[CONTROL_TUNING_ID_BYTES];
    bool output_enabled;        /* la salida estuvo realmente habilitada */
    control_log_signal_t signal;
    control_log_control_t control;
    union {
        control_log_pressure_t pressure;
        control_log_temperature_t temperature;
    } specific;
} control_log_sample_t;

/*
 * Sucesos que no son una muestra periódica pero sin los cuales el registro no
 * se interpreta: dónde empieza y acaba cada prueba, por qué terminó, cuándo el
 * guardián cortó la salida, y si se perdió algo.
 */
typedef enum {
    CONTROL_LOG_EVENT_NONE = 0,
    CONTROL_LOG_EVENT_SESSION_OPENED,
    CONTROL_LOG_EVENT_SESSION_CLOSED,   /* detalle: `extraction_termination_t` */
    CONTROL_LOG_EVENT_LOOP_ENABLED,
    CONTROL_LOG_EVENT_LOOP_INHIBITED,   /* detalle: `actuation_verdict_t` */
    CONTROL_LOG_EVENT_RECORDS_LOST,     /* detalle: cuántos registros se perdieron */
    /*
     * El modo restringido. El inicio y el final llevan la secuencia, para que el
     * registro diga qué estímulo se aplicó sin depender de lo que se anotó a
     * mano; la interrupción lleva el veredicto que la causó.
     */
    CONTROL_LOG_EVENT_STIMULUS_STARTED,  /* detalle: `stimulus_sequence_id_t` */
    CONTROL_LOG_EVENT_STIMULUS_FINISHED, /* detalle: `stimulus_sequence_id_t` */
    CONTROL_LOG_EVENT_STIMULUS_ABORTED,  /* detalle: `actuation_verdict_t` */
} control_log_event_kind_t;

typedef struct {
    uint64_t monotonic_us;
    control_log_event_kind_t kind;
    control_log_loop_t loop;
    uint32_t detail;
} control_log_event_t;

typedef enum {
    CONTROL_LOG_RECORD_SAMPLE = 0,
    CONTROL_LOG_RECORD_EVENT,
    CONTROL_LOG_RECORD_SCALE,
} control_log_record_kind_t;

typedef struct {
    control_log_record_kind_t kind;
    union {
        control_log_sample_t sample;
        control_log_event_t event;
        control_log_scale_t scale;
    } as;
} control_log_record_t;

/*
 * Cola circular sobre almacenamiento del llamante.
 *
 * No reserva memoria a propósito: en la tarjeta vive en PSRAM y en el anfitrión
 * en la pila de la prueba, sin que el módulo tenga que saber la diferencia.
 */
typedef struct {
    control_log_record_t *storage;
    uint32_t capacity;
    uint32_t head;      /* próxima escritura */
    uint32_t tail;      /* próxima lectura */
    uint32_t pending;
    uint32_t accepted;  /* total admitido desde el arranque */
    uint32_t lost;      /* descartados por desbordamiento, aún no avisados */
    uint32_t lost_total;
} control_log_ring_t;

bool control_log_ring_init(control_log_ring_t *ring,
                           control_log_record_t *storage,
                           uint32_t capacity);

/* Devuelve false si la cola estaba llena; en ese caso incrementa las pérdidas. */
bool control_log_ring_push(control_log_ring_t *ring, const control_log_record_t *record);

/* Extrae el registro más antiguo. Devuelve false si no había ninguno. */
bool control_log_ring_pop(control_log_ring_t *ring, control_log_record_t *record);

uint32_t control_log_ring_pending(const control_log_ring_t *ring);
uint32_t control_log_ring_accepted(const control_log_ring_t *ring);
uint32_t control_log_ring_lost_total(const control_log_ring_t *ring);

/*
 * Lee las pérdidas pendientes de avisar y las pone a cero.
 *
 * Existe para que el aviso se emita una sola vez por racha y no en cada vuelta
 * del drenaje: una cola que se llenó durante dos segundos produciría centenares
 * de líneas idénticas, que es ruido, no información.
 */
uint32_t control_log_ring_take_lost(control_log_ring_t *ring);

/*
 * Cabeceras del flujo. Cadenas estáticas, ya terminadas en salto de línea.
 *
 * Se emiten una vez al arrancar el registro. Llevan `#` delante para que un
 * lector distinga la descripción de los datos sin conocer el orden en que
 * aparecen.
 */
const char *control_log_header_pressure(void);
const char *control_log_header_temperature(void);
const char *control_log_header_event(void);
const char *control_log_header_scale(void);

/*
 * Escribe un registro como una línea, con su salto final incluido.
 *
 * Devuelve los bytes escritos sin contar el terminador, o -1 si los argumentos
 * no sirven o el destino se queda corto. Nunca escribe una línea truncada: una
 * línea a medias en un CSV corrompe la columna, no solo el valor.
 */
int control_log_format(const control_log_record_t *record, char *buffer, size_t size);

const char *control_log_loop_name(control_log_loop_t loop);
const char *control_log_event_name(control_log_event_kind_t kind);
