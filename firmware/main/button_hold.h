#pragma once

/*
 * Detección de un pulsador del mando retenido (ADR-006 § 7.9).
 *
 * Cada función del mando lleva dos pulsadores en paralelo, y uno que falle en
 * cortocircuito deja la acción apretada para siempre. ADR-006 exige detectarlo
 * **en el arranque y durante la operación**. El 2026-10-07, en la puesta en
 * servicio, la placa leyó el encendido como pulsado y la máquina (simulada) se
 * encendió sin que nadie lo pidiera; el firmware no tenía con qué notarlo.
 *
 * ## Dos reglas
 *
 * - **Al arrancar.** Si la primera lectura aceptada ya es «pulsado», el botón se
 *   declara retenido sin esperar a ningún plazo: nadie aprieta un botón en el
 *   instante del encendido. `button_debounce` ya impide que esa lectura produzca
 *   una orden; lo que faltaba es que alguien se entere.
 * - **En operación.** Pulsado más allá de un límite, se declara retenido.
 *
 * En ambos casos deja de estarlo al soltarse: un contacto que se libera vuelve a
 * ser un botón utilizable, y exigir un reinicio por un dedo apoyado no protege
 * nada que el propio antirrebote no proteja ya.
 *
 * ## Por qué el purgado no tiene límite de tiempo
 *
 * Mantenerlo apretado es su uso normal: el agua sale mientras se sostiene, y
 * la máquina de estados ya corta la purga al agotar su tiempo máximo. Ponerle
 * un límite aquí convertiría en avería una purga larga legítima. La regla del
 * arranque sí le aplica.
 *
 * ## Lo que no hace
 *
 * No anula órdenes ni toca la máquina de estados. Un pulsador retenido ya no
 * produce más flancos que el de soltar, de modo que «ignorarlo hasta que se
 * suelte» no exige nada aquí: lo que añade este módulo es **saberlo** para
 * avisar al barista. Tampoco lee el reloj: recibe el tiempo pulsado que lleva
 * la cuenta de `button_debounce`.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    /* 0 significa «sin límite de tiempo»: solo aplica la regla del arranque. */
    uint32_t limit_ms;

    bool started;          /* ya se vio la primera lectura aceptada */
    bool held;             /* retenido ahora */
    uint32_t occurrences;  /* veces que pasó de libre a retenido */
} button_hold_t;

/* Configura el límite; 0 para un botón que se usa manteniéndolo. */
void button_hold_configure(button_hold_t *hold, uint32_t limit_ms);

/*
 * Entrega el nivel aceptado del pulsador y el tiempo que lleva pulsado, y
 * devuelve verdadero **solo en la muestra en que pasa a retenido**, para que
 * quien lo use avise una vez y no en cada ciclo.
 */
bool button_hold_update(button_hold_t *hold, bool pressed, uint32_t pressed_ms);

bool button_hold_is_held(const button_hold_t *hold);
uint32_t button_hold_occurrences(const button_hold_t *hold);
