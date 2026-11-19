#pragma once

/*
 * Modulación por ciclo integral del calentador (ADR-009 § 3.5).
 *
 * La salida del PI se convierte en **tiempo de conducción dentro de una ventana
 * fija**. El SSR cuantiza por sí mismo la orden a semiciclos completos, de modo
 * que este módulo no cuenta ciclos de red: mantiene la orden activa la fracción
 * de ventana que corresponde y deja que el relé redondee.
 *
 * Es el análogo térmico de `psm_scheduler`, a otra granularidad y por una razón
 * distinta. La bomba se gobierna por actuaciones discretas porque el dimmer solo
 * ofrece oportunidades sincronizadas con la red; el calentador se gobierna por
 * tiempo porque el SSR conduce mientras se le pida. Lo que comparten es el
 * acumulador de residuo, y lo comparten por el mismo motivo.
 *
 * ## Por qué hay residuo, y qué resuelve
 *
 * La ventana vale 1 s y el firmware solo puede conmutar en los instantes en que
 * se ejecuta, cada 20 ms. La resolución efectiva sería por tanto del 2 %, y una
 * demanda del 23 % se aplicaría siempre como 20 % o siempre como 40 %: un sesgo
 * permanente, indistinguible de un error de calibración.
 *
 * El residuo lo elimina. Al cerrar cada ventana se guarda lo que se quiso
 * conducir menos lo que se pudo, con su signo, y se suma a la ventana
 * siguiente. Una demanda del 23 % produce entonces ventanas de 240 ms y 220 ms
 * alternadas cuya media es exactamente 230 ms. Es aritmética entera a propósito:
 * un residuo fraccionario arrastrado durante una hora de calentamiento
 * terminaría desplazando la conducción real respecto a la pedida.
 *
 * ## Lo que no hace
 *
 * No conoce el guardián ni la inhibición. Si la salida debe cortarse, quien
 * llama deja de pedirle pasos y lo reinicia: una inhibición no es un error de
 * redondeo y no debe arrastrarse como residuo a la ventana siguiente.
 */

#include <stdbool.h>
#include <stdint.h>

#define HEATER_DEMAND_FULL_SCALE 100U

typedef struct {
    uint32_t window_ms;   /* ventana de modulación; ADR-009 decide 1 s */
    uint32_t tick_ms;     /* periodo con el que se llama a `step` */
    uint32_t elapsed_ms;  /* transcurrido dentro de la ventana en curso */
    uint32_t target_ms;   /* conducción objetivo de la ventana, con el residuo dentro */
    uint32_t applied_ms;  /* conducción ya emitida en la ventana */
    int32_t residue_ms;   /* lo que faltó o sobró, arrastrado a la siguiente */
    uint32_t windows;     /* ventanas cerradas desde el último reinicio */
} heater_modulator_t;

/*
 * Fija la ventana y el paso.
 *
 * Rechaza un paso que no divida la ventana: la última porción quedaría más
 * corta que las demás y la conducción efectiva no coincidiría con la pedida ni
 * siquiera en media.
 */
bool heater_modulator_configure(heater_modulator_t *modulator,
                                uint32_t window_ms,
                                uint32_t tick_ms);

/* Olvida ventana y residuo, y conserva la configuración. */
void heater_modulator_reset(heater_modulator_t *modulator);

/*
 * Lleva una demanda del controlador a la escala del modulador.
 *
 * Acota a [0, 100] y redondea al entero más próximo. Una demanda no finita se
 * traduce como 0: ante un número imposible, la única respuesta defendible es no
 * conducir.
 */
uint8_t heater_modulator_quantize(float demand_percent);

/*
 * Avanza un paso y responde si el calentador debe conducir durante él.
 *
 * `demand` viene ya cuantizada y solo se lee al abrir cada ventana: cambiarla a
 * mitad de ventana no altera la conducción en curso, que es lo que hace que la
 * modulación sea una fracción de ventana y no una orden instantánea. Una demanda
 * de cero corta de inmediato y **descarta el residuo**, por la misma razón que
 * una densidad de cero no dispara la bomba: cero significa apagado, no una
 * deuda pendiente de saldar.
 */
bool heater_modulator_step(heater_modulator_t *modulator, uint8_t demand);

uint32_t heater_modulator_target_ms(const heater_modulator_t *modulator);
uint32_t heater_modulator_applied_ms(const heater_modulator_t *modulator);
uint32_t heater_modulator_elapsed_ms(const heater_modulator_t *modulator);
uint32_t heater_modulator_windows(const heater_modulator_t *modulator);
int32_t heater_modulator_residue_ms(const heater_modulator_t *modulator);
