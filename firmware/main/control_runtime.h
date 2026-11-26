#pragma once

/*
 * Ejecución de los dos lazos de control (paso 7; cierra H-08).
 *
 * Es donde la ley escrita en los módulos puros pasa a ejecutarse de verdad.
 * Hasta aquí existían el filtro, el PI, el planificador y el modulador, cada uno
 * probado, y no los llamaba nadie.
 *
 * ## Una sola tarea para los dos lazos
 *
 * Corre cada 20 ms, que es el periodo del lazo de presión (ADR-007 § 3.3), y
 * ejecuta el térmico **uno de cada diez ciclos**, que son los 200 ms que decide
 * ADR-009 § 3.6. Los dos periodos son exactamente conmensurables, de modo que la
 * decimación no produce deriva ni batido.
 *
 * Es el mismo patrón que ya usa `acquisition_runtime`, y se elige por la misma
 * razón práctica: dos lazos dentro de la misma tarea no se disputan nada. No hay
 * estado compartido que proteger, no hay dos suscripciones al vigilante de
 * tareas, y el orden en que se lee el veredicto del guardián es uno y el mismo.
 * El coste es que el ciclo de presión que sigue al térmico hereda su jitter; son
 * un filtro, un PI y un modulador, es decir microsegundos.
 *
 * ## Lo que decide, y lo que no puede decidir
 *
 * No decide si actuar. Eso lo decide el guardián, en su propia tarea y por su
 * propia ruta, y este módulo se limita a preguntárselo y obedecer. Cuando el
 * veredicto inhibe, el lazo **se reinicia**: la referencia interna vuelve a la
 * medida y la integral se olvida, para que al volver no arrastre lo acumulado
 * mientras la salida estaba cortada.
 *
 * Tampoco elige referencias ni inicia procesos: las toma del supervisor, que las
 * toma del perfil efectivo ya validado (RD-002).
 *
 * ## El lazo de banco
 *
 * Con `CONFIG_CAFE_CONTROL_BENCH_LOOP` el lazo se cierra contra `control_plant`
 * en lugar de contra la medida del proceso, y la actuación calculada vuelve a
 * ese modelo. Es lo único que permite ver el lazo regular sin cafetera, porque
 * la planta de escenarios de `simulated_plant` no responde a la salida del
 * controlador —no es su cometido— y un PI enfrentado a una planta que lo ignora
 * no regula nada.
 *
 * Las salidas físicas siguen inhibidas en ese modo, y no por una precaución
 * añadida aquí: `control_tuning` declara `commissioned` falso y el guardián
 * inhibe por esa causa. La ruta de corte es la misma que en operación.
 *
 * ## El modo restringido
 *
 * Con el veredicto `allowed-identification` el lazo **no se cierra**: el PI se
 * mantiene reiniciado en la medida, igual que inhibido, y la salida es la
 * demanda que el supervisor publica, que es la que el guardián acaba de admitir.
 * Las ganancias no intervienen, de modo que valgan lo que valgan no actúan. En
 * esas líneas la referencia autorizada y los términos del PI quedan a cero, la
 * referencia interna muestra la medida en la que el PI está cebado, y la columna
 * de veredicto dice por qué.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "actuation_guard.h"
#include "control_log.h"
#include "control_tuning.h"
#include "hydraulic_test.h"
#include "identification_tuning.h"

typedef struct {
    bool running;
    uint32_t cycles;              /* ciclos del lazo de presión */
    uint32_t thermal_cycles;
    uint32_t deadline_misses;     /* ciclos que no cerraron dentro de su periodo */
    uint32_t pressure_resets;     /* veces que la inhibición reinició el lazo */
    uint32_t temperature_resets;
    bool bench_loop;              /* cerrado contra el modelo, no contra la medida */
    const char *pressure_tuning;      /* acta de la planta, o `bench-v1` */
    const char *temperature_tuning;
    bool pressure_commissioned;
    bool temperature_commissioned;

    float pressure_measurement;
    float pressure_output;
    uint8_t pressure_density;
    actuation_verdict_t pressure_verdict;

    float temperature_measurement;
    float temperature_output;
    uint8_t temperature_demand;
    actuation_verdict_t temperature_verdict;

    /* Milisegundos de conducción del calentador desde el arranque, acumulados.
     * La telemetría sale cada 15 s fuera del café: una demanda instantánea no
     * dice cuánto calentó entre dos muestras, y el contador sí. Vuelve a cero
     * con cada reinicio de la placa. */
    uint64_t heater_applied_ms;
} control_runtime_status_t;

esp_err_t control_runtime_start(void);

bool control_runtime_get_status(control_runtime_status_t *status);

/* Lo que quedó tras una orden de ajuste en caliente, para responderla. */
typedef struct {
    char identifier[CONTROL_TUNING_ID_BYTES];
    float lambda_s;
    bool integral_enabled;
    float proportional_gain;
    float integral_gain;
    float ceiling_percent;
    actuation_verdict_t verdict;   /* el del calentador en el último ciclo */
} control_runtime_tuning_report_t;

/*
 * Atiende una orden `A,` del puerto de servicio (ADR-020 § 4.2). Solo existe
 * en el binario de identificación con la secuencia de sintonía, y solo cambia
 * algo con el modo restringido corriendo, es decir, tras la confirmación
 * física. En cualquier otro binario responde «sin configurar».
 */
identification_tuning_result_t control_runtime_tuning_command(
    const identification_tuning_command_t *command, control_runtime_tuning_report_t *report);

/* Lo que quedó tras una orden del ensayo hidráulico, para responderla. */
typedef struct {
    uint8_t selected;   /* la de la próxima extracción; 0, ninguna */
    uint8_t applied;    /* la de la extracción en curso; 0, ninguna */
    uint8_t orders[HYDRAULIC_TEST_ORDERS];
} control_runtime_hydraulic_report_t;

/*
 * Atiende una orden `H,` del puerto de servicio (enmienda a ADR-007 § 5 del
 * 2026-10-16). Solo existe en el binario del ensayo hidráulico; en cualquier
 * otro responde «sin configurar».
 */
hydraulic_test_result_t control_runtime_hydraulic_command(
    const hydraulic_test_command_t *command, control_runtime_hydraulic_report_t *report);

/* Sintonía vigente, para el diagnóstico de arranque. */
control_tuning_t control_runtime_pressure_tuning(void);
control_tuning_t control_runtime_temperature_tuning(void);
