#pragma once

/*
 * Soporte físico del registro del lazo: cola en PSRAM y drenaje por consola.
 *
 * `control_log` decide qué se guarda y cómo se escribe cada línea; aquí vive lo
 * que obliga a tener la tarjeta delante: dónde reside la cola, quién la protege
 * frente a dos tareas a la vez y quién la vacía.
 *
 * ## Por qué la cola está en PSRAM y el formateo fuera del lazo
 *
 * El lazo de presión corre cada 20 ms y el de temperatura cada 200 ms. Componer
 * una línea con doce flotantes cuesta un orden de magnitud más que copiar la
 * estructura que los contiene, así que el lazo solo copia —dentro de una
 * sección crítica de unos pocos microsegundos— y el `snprintf` ocurre en una
 * tarea de prioridad baja, fuera del camino del control. La N16R8 tiene 8 MB de
 * PSRAM sin usar; con registros de poco más de cien bytes, la capacidad por
 * omisión son unos ochenta segundos de holgura.
 *
 * ## Por qué a la consola y no a la flash ni a la red
 *
 * ADR-007 § 3.6 exige que el registro **no dependa de la plataforma externa
 * durante la extracción**, lo que descarta la red. La cola NVS que ya existe
 * está dimensionada para resúmenes de preparación —32 entradas— y escribir en
 * flash cincuenta veces por segundo la desgastaría sin necesidad. El puerto
 * serie ya está conectado en cada prueba de banco, no exige hardware añadido, y
 * sobre todo **lo ya emitido sobrevive a un reinicio o a un cuelgue**, que es
 * justamente el caso que más interesa conservar. Un volcado al cerrar la sesión
 * se perdería entero.
 *
 * El caudal del registro es de unos 110 kbit/s, por lo que la consola necesita
 * 921600 baudios: a 115200 no cabe, y la primera consecuencia sería que el
 * drenaje se retrasa y la cola empieza a descartar.
 *
 * ## Qué pasa si el drenaje no da abasto
 *
 * Nada que afecte al control. La tarea corre a prioridad baja, de modo que si
 * la consola se bloquea —por ejemplo porque la salida secundaria USB no tiene
 * a nadie al otro lado— quien espera es ella, no el lazo. La cola absorbe el
 * retraso y, si llega a llenarse, descarta lo nuevo y lo cuenta; el drenaje
 * emite entonces una línea de evento diciendo cuántos registros se perdieron.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "control_log.h"

/* Unos ochenta segundos del lazo de presión a 50 Hz; cerca de medio megabyte. */
#define CONTROL_LOG_DEFAULT_CAPACITY 4096U

/*
 * Reserva la cola en PSRAM, emite las cabeceras y lanza el drenaje.
 *
 * Devuelve `ESP_ERR_NO_MEM` si la PSRAM no puede darle la capacidad pedida. No
 * recurre a la memoria interna en silencio: medio megabyte allí comprometería
 * la pila de red y el lazo, y un registro más corto de lo pedido debe ser una
 * decisión de quien llama, no un apaño invisible.
 */
esp_err_t control_log_runtime_start(uint32_t capacity);

/*
 * Encola una muestra del lazo. Apta para llamarse desde la tarea de control.
 *
 * El llamante rellena la física —las señales, los términos del PI, lo pedido y
 * lo aplicado— y **no toca `monotonic_us` ni `sequence`**: los pone esta
 * función, con el reloj monotónico del sistema y un contador propio de cada
 * lazo. Así la marca de tiempo es siempre la del instante en que el registro
 * entró en la cola, y la numeración no puede saltarse por descuido de un
 * llamante: una laguna en ella significa pérdida y solo eso.
 */
void control_log_runtime_sample(const control_log_sample_t *sample);

/*
 * Encola una conversión de la báscula de ensayo.
 *
 * Igual que con las muestras del lazo, el llamante no pone `monotonic_us` ni
 * `sequence`: los pone esta función con el mismo reloj monotónico, que es la
 * razón entera de que la masa viaje por este registro y no por un canal aparte.
 * El caudal se obtiene derivando la masa, y derivar contra un reloj distinto del
 * de la presión metería el error de alineamiento justo donde más estorba.
 *
 * La numeración es propia de la báscula: una laguna significa que se perdieron
 * conversiones, no que el lazo saltara una vuelta.
 */
void control_log_runtime_scale(const control_log_scale_t *reading);

/* Encola un suceso: apertura y cierre de sesión, habilitación o inhibición. */
void control_log_runtime_event(control_log_event_kind_t kind,
                               control_log_loop_t loop,
                               uint32_t detail);

/* Estado de la cola, para el diagnóstico y el latido. */
bool control_log_runtime_statistics(uint32_t *pending,
                                    uint32_t *accepted,
                                    uint32_t *lost_total);

uint32_t control_log_runtime_capacity(void);
bool control_log_runtime_active(void);
