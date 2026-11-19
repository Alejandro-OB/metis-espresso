#pragma once

/*
 * Composición de los tramos de la serie medida de una preparación terminada.
 *
 * Existe porque la telemetría cumplía dos papeles con un solo mensaje. Uno es
 * observar el proceso mientras ocurre, que no tiene sentido diferido: si nadie
 * miraba, entregarlo media hora después no sirve de nada, y por eso ADR-012 § 4
 * decide que no ocupe la cola. El otro es **ser la serie medida** contra la que
 * se compara el modelo (§ 4.1), que es evidencia y no puede perderse porque la
 * plataforma estuviera apagada.
 *
 * Se comprobó en la placa el 15/09/2026: una preparación hecha sin enlace se
 * archivó entera —instante, duración, configuración, estadísticos— y sin
 * ninguna de sus 270 muestras. Quedó registrada y fuera de la comparación
 * físico-virtual, que es lo que el sistema existe para hacer.
 *
 * La serie se compone al cerrar la preparación, a partir del registro interno
 * de la sesión, y se entrega desde la cola persistente igual que el resumen. El
 * flujo en vivo sigue saliendo por `telemetry_message` y sigue perdiéndose sin
 * enlace, que es lo correcto para lo que es.
 *
 * Delante de las muestras de la preparación van las del minuto previo a pulsar,
 * con `elapsedMs` negativo, si la sesión las tomó (`pre_start_history`): con
 * ellas la plataforma calcula la estabilidad térmica de la máquina, que la § 4.4
 * de la monografía mide antes del café. Enmienda del 2026-10-19 a ADR-012 § 4.1.
 * La numeración de tramos cubre las dos partes como una sola serie.
 *
 * Módulo puro: no reserva memoria, no conoce el reloj y no toca la cola. Recibe
 * la sesión ya cerrada y devuelve el JSON de un tramo.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "extraction_session.h"

/*
 * Separación entre muestras conservadas.
 *
 * No es la cadencia a la que el dispositivo registra —que es mucho mayor— sino
 * la rejilla sobre la que se compara: el plan de comparación físico-virtual
 * declara la serie medida a 500 ms nominales y alinea el modelo sobre ella. Se
 * conserva esa y no todo lo registrado porque lo que sobra no se compara con
 * nada y sí ocuparía sitio en la cola, donde compite con evidencia de otras
 * preparaciones.
 */
#define SERIES_MESSAGE_INTERVAL_MS 500U

/*
 * Muestras por tramo, y el hueco donde tiene que caber.
 *
 * `SERIES_MESSAGE_JSON_BYTES` es el tamaño de un hueco de la cola persistente:
 * un tramo que no quepa no puede encolarse, y la serie se perdería justo en el
 * caso para el que existe, que es el corte de enlace.
 *
 * El número de muestras se eligió midiendo, no estimando. Cada muestra ocupa
 * unos 121 bytes y la cabecera del sobre unos 205, de modo que doce se pasaban
 * —1656 bytes—. Diez cabían en una serie medida, pero no en una simulada: la
 * calidad «simulated» ocupa cuatro caracteres más que «valid» en cada canal, y
 * el tramo de una simulación de más de 100 s llegaba a 1578 bytes. El minuto
 * previo lo vuelve el caso común, porque sus instantes de −10 000 a −60 000 ms
 * tienen seis caracteres: con diez, cualquier simulación con minuto previo se
 * pasaba (1538 bytes con valores corrientes) y su serie entera se perdía.
 *
 * Con nueve, el peor tramo medido —simulado, instantes de seis caracteres a
 * los dos lados del cero, valores de ocho, secuencia de diez cifras— ocupa 1460
 * bytes, y el peor medido 1386. Lo comprueba `test_series_message.c`.
 */
#define SERIES_MESSAGE_SAMPLES_PER_CHUNK 9U

#define SERIES_MESSAGE_ID_LENGTH 64U
#define SERIES_MESSAGE_JSON_BYTES 1536U

typedef enum {
    SERIES_MESSAGE_OK = 0,
    SERIES_MESSAGE_INVALID_ARGUMENT,
    SERIES_MESSAGE_BUFFER_TOO_SMALL,
    /* No hay tramo con ese número: la serie tiene menos. */
    SERIES_MESSAGE_ABSENT,
    /* El contrato exige instantes reales, igual que el resumen. */
    SERIES_MESSAGE_TIME_BASE_UNAVAILABLE,
} series_message_status_t;

typedef struct {
    const char *device_id;
    uint32_t sequence;
    /* Instante de cierre de la preparación, no el de composición del tramo. */
    uint64_t epoch_ms;
    /* Falso cuando alguna muestra no fue medida; decide `source` del tramo. */
    bool measured;
} series_message_context_t;

/*
 * Número de tramos que componen la serie de esta sesión.
 *
 * Devuelve cero cuando no hay ninguna muestra conservable, que es distinto de
 * un error: una preparación puede terminar antes de la primera muestra de la
 * rejilla y entonces no hay serie que enviar. Si la sesión tomó el minuto
 * previo, sus muestras cuentan: una preparación cortada al instante sigue
 * diciendo cómo estaba la máquina antes de pulsar.
 */
uint32_t series_message_chunk_count(const extraction_session_t *session);

/*
 * Compone el tramo indicado.
 *
 * `chunk_index` va de cero a `series_message_chunk_count() - 1`. Cada muestra
 * conserva la calidad con la que se registró y su valor se emite como `null`
 * cuando el canal no era utilizable: una medida no apta se marca, no se
 * sustituye por la anterior ni se interpola.
 */
series_message_status_t series_message_build(const extraction_session_t *session,
                                             const series_message_context_t *context,
                                             uint32_t chunk_index,
                                             char *buffer,
                                             size_t buffer_size,
                                             char *message_id,
                                             size_t message_id_size);

const char *series_message_status_name(series_message_status_t status);
