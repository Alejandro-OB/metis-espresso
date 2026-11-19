#pragma once

/*
 * Gestos del mando para salir de una alarma, para reiniciar la placa y para
 * pedir un firmware nuevo por Wi-Fi.
 *
 * Hasta el 2026-10-17 la alarma solo se dejaba reiniciando la placa desde el
 * computador: ningún componente publicaba `OP_EVENT_SAFETY_CLEARED`. El usuario
 * pidió hacerlo desde el mando, y también un reinicio, «que a veces es
 * necesario». Son dos gestos distintos para que no se confundan:
 *
 * - **Encendido solo, sostenido `PANEL_GESTURE_HOLD_MS`, en `ALARM`:** pide
 *   despejar la alarma. No la despeja este módulo: quien observa el proceso
 *   decide si la causa ya desapareció (`process_supervisor_clear_allowed`) y
 *   solo entonces la máquina vuelve a reposo, nunca a lo que hacía.
 * - **Encendido y tara juntos, sostenidos `PANEL_GESTURE_HOLD_MS`:** pide
 *   reiniciar la placa, en cualquier estado salvo con la bomba en uso.
 * - **Tara y purgado juntos, sostenidos `PANEL_GESTURE_HOLD_MS`, con la máquina
 *   apagada:** pide buscar un firmware nuevo en el computador (ADR-021). No lo
 *   instala este módulo: `firmware_update` decide si las condiciones lo admiten.
 *
 * ## Por qué encendido y tara, y no encendido y café
 *
 * Café solo, en `READY`, prepara un café: el primer dedo que llegara arrancaría
 * la bomba antes de que el gesto se completara. La tara sola nunca acciona
 * nada; encendido solo, a lo sumo enciende o apaga, y el reinicio deja la
 * máquina en reposo de todas formas. Lo eligió el usuario el 2026-10-17.
 *
 * ## Por qué tara y purgado para el firmware
 *
 * Con la cafetera sellada el USB no se alcanza (el usuario, 2026-10-25), y el
 * mando es lo único que queda a mano. Hacía falta una pareja que no usara ningún
 * otro gesto: encendido y tara reinician, café y tara confirman el modo
 * restringido (`identification_mode`). Con la máquina apagada ninguno de los
 * dos botones hace nada por sí solo: la tara no cambia el estado y el purgado
 * solo actúa en `READY`. Por eso el gesto exige la máquina apagada y no solo
 * «sin bomba»: en `READY` el primer dedo en el purgado abriría el agua.
 *
 * ## Por qué 3 s y no encendido sostenido más tiempo
 *
 * A los `PANEL_HELD_LIMIT_MS` (10 s) un pulsador se declara retenido
 * (`button_hold`). Un gesto más largo se cruzaría con esa regla. Los 3 s los
 * aceptó el usuario; no salen de una medida.
 *
 * ## Lo que no hace
 *
 * - **No actúa con un pulsador retenido.** Un contacto en cortocircuito no puede
 *   despejar alarmas ni reiniciar la placa en bucle: tras el reinicio la regla
 *   del arranque de `button_hold` lo declara retenido de inmediato.
 * - **No reinicia con la bomba en uso.** Con agua saliendo, el reinicio cortaría
 *   la extracción sin cerrarla.
 * - **No repite.** Cada gesto actúa una vez por pulsación; para repetirlo hay
 *   que soltar.
 * - **No pide firmware con el encendido apretado.** Tres dedos no son ninguno
 *   de los gestos, y el encendido manda: puede ir camino del reinicio.
 *
 * Puro: recibe lo que el mando lee y el estado, y devuelve qué pide.
 */

#include <stdbool.h>
#include <stdint.h>

#include "operational_fsm.h"

#define PANEL_GESTURE_HOLD_MS 3000U

typedef enum {
    /* El cero es «nada»: una estructura sin escribir no pide nada. */
    PANEL_GESTURE_NONE = 0,
    PANEL_GESTURE_CLEAR_ALARM,
    PANEL_GESTURE_RESTART,
    PANEL_GESTURE_FIRMWARE_UPDATE,
} panel_gesture_action_t;

typedef struct {
    operational_state_t state;
    bool power_pressed;
    uint32_t power_pressed_ms;
    bool power_held;     /* retenido según button_hold */
    bool tare_pressed;
    uint32_t tare_pressed_ms;
    bool tare_held;
    bool purge_pressed;
    uint32_t purge_pressed_ms;
    bool purge_held;     /* el purgado solo se declara retenido en el arranque */
} panel_gesture_input_t;

typedef struct {
    /* Ya actuó en esta pulsación; se rearma al soltar. A cero, rearmado. */
    bool clear_done;
    bool restart_done;
    bool update_done;
} panel_gesture_t;

panel_gesture_action_t panel_gesture_update(panel_gesture_t *gesture,
                                            const panel_gesture_input_t *input);

const char *panel_gesture_name(panel_gesture_action_t action);

/*
 * Si el botón de encendido puede encender la máquina. Hace falta red (el
 * detector de cruce por cero ve la onda) y que el supervisor ya tenga medidas
 * aptas del proceso. Sin red el calentador no recibiría corriente y el
 * transductor de presión, que se alimenta de la red, marca avería. Con la red
 * recién conectada hay un instante en que el cruce por cero ya es apto (unos
 * 42 ms) y la presión todavía no: el 2026-10-17 un encendido en ese hueco llevó
 * la placa a alarma, porque el supervisor, sin medidas, dispara. Apagar sí se
 * puede siempre: solo se niega salir del reposo.
 */
bool panel_power_on_allowed(operational_state_t state, bool mains_present, bool measuring);
