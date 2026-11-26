#pragma once

/*
 * Parámetros de los dos lazos, con su versión y su marca de puesta en servicio.
 *
 * ADR-007 § 5.9 y ADR-009 § 5.9 exigen lo mismo antes de habilitar el control
 * ordinario: **congelar los valores, su versión, las condiciones de prueba y los
 * criterios de aceptación**. Este módulo es donde eso vive. Que exista un sitio
 * único obliga a que cambiar una ganancia sea un cambio visible y no una
 * constante editada en medio de una tarea.
 *
 * ## Las ganancias no son números elegidos
 *
 * Se calculan aplicando SIMC —la fórmula que ambos ADR prescriben— sobre el
 * modelo de banco de `control_plant`, en el mismo instante en que se piden. No
 * están escritas a mano en ninguna parte, de modo que **no pueden desfasarse
 * respecto al modelo**: cambiar la planta cambia la sintonía, que es la relación
 * correcta entre ambas cosas.
 *
 *     Kp = τ / [K·(λ + θ)]
 *     Ti = min(τ, 4·(λ + θ))
 *     Ki = Kp / Ti
 *
 * `λ` se toma deliberadamente conservador, tres veces el retardo: un valor mayor
 * produce una respuesta más lenta y más robusta, que es lo que se quiere cuando
 * el modelo todavía no está identificado.
 *
 * ## Lo que esta sintonía **no** autoriza
 *
 * Sin planta identificada, `commissioned` vale falso y así debe seguir. Estas
 * ganancias son correctas para el modelo de banco y no dicen nada sobre la
 * máquina. El guardián las trata en consecuencia y ninguna salida física puede
 * energizarse con ellas.
 *
 * ## La planta identificada (ADR-020 § 4.2)
 *
 * Cuando el firmware se compila con el archivo de planta de un lazo
 * (`commissioned_plant.h`), SIMC se aplica sobre esa planta y no sobre el
 * modelo de banco, con la `λ`, el filtro y la velocidad de referencia del acta,
 * y ese lazo —solo ese— sale con `commissioned` verdadero. El identificador de
 * la sintonía pasa a ser el del acta y viaja en cada línea del registro.
 *
 * Si los números del archivo no admitieran sintonía, el lazo queda sin poner en
 * servicio y con ganancias nulas en vez de arrancar con algo calculado a
 * medias. El generador ya lo comprueba; esto es la segunda barrera.
 *
 * La puesta en servicio real sigue exigiendo el procedimiento completo de
 * ADR-007 § 5 y ADR-009 § 5, que empieza con `Ki = 0` y sube `Kp` gradualmente
 * sobre registros medidos. Aquí se ejecuta el PI entero desde el principio
 * porque el modelo se conoce y lo que se quiere ejercitar es la estructura.
 */

#include <stdbool.h>

#include "pi_controller.h"

#define CONTROL_TUNING_IDENTIFIER "bench-v1"

/* Tamaño del identificador, con su terminador, tal como viaja en el registro. */
#define CONTROL_TUNING_ID_BYTES 32U

/* Lo que trae el acta de un lazo; ver `commissioned_plant.h`. */
typedef struct {
    const char *identifier;
    float gain;
    float time_constant_s;
    float dead_time_s;
    float lambda_s;
    float filter_time_constant_s;
    float setpoint_rate;
    /* Techo de la salida del PI, en %. Con 0 la planta no admite sintonía: el
     * cero no puede significar «sin techo» (acta del 2026-10-10). */
    float output_maximum;
} control_identified_plant_t;

typedef struct {
    const char *identifier;        /* versión de esta sintonía */
    bool commissioned;             /* falso mientras la planta no esté identificada */
    float filter_time_constant_s;  /* τf del filtro del lazo */
    float sample_period_s;         /* periodo de ejecución del PI */
    pi_parameters_t pi;
} control_tuning_t;

/*
 * Calcula `Kp` y `Ki` por SIMC para una planta de primer orden con retardo.
 *
 * Devuelve false si los datos no describen una planta utilizable —ganancia o
 * constante de tiempo no positivas, valores no finitos—, en lugar de producir
 * ganancias sin sentido que el lazo aplicaría sin enterarse.
 */
bool control_tuning_simc(float gain,
                         float time_constant_s,
                         float dead_time_s,
                         float lambda_s,
                         float *proportional_gain,
                         float *integral_gain);

/*
 * Sintonía sobre una planta identificada. Es la única vía por la que una
 * sintonía sale con `commissioned` verdadero, y solo si la planta la admite.
 * Pura: las pruebas la ejercitan con plantas de prueba declaradas como tales.
 */
control_tuning_t control_tuning_from_identified(const control_identified_plant_t *plant,
                                               float sample_period_s);

/* ADR-007 § 3.3: PI cada 20 ms, filtro de 50 ms, salida 0–100 %. */
control_tuning_t control_tuning_pressure(void);

/* ADR-009 § 3.6: PI cada 200 ms, filtro de 1 s, salida 0–100 %. */
control_tuning_t control_tuning_temperature(void);

/*
 * La sintonía térmica sobre el modelo de banco, haya o no acta. Es la que rige
 * sin planta identificada, y la que necesitan las pruebas que cierran el lazo
 * contra ese mismo modelo: con el acta firmada (2026-10-10) la sintonía vigente
 * es la de la caldera real, y aplicarla al modelo de banco no prueba nada.
 */
control_tuning_t control_tuning_temperature_bench(void);

/*
 * La sintonía de banco del lazo de presión, siempre, haya o no acta. La
 * necesitan las pruebas que cierran el lazo sobre el modelo de banco: desde el
 * acta del 2026-10-12, `control_tuning_pressure` devuelve la de la máquina, que
 * no es la de esa planta.
 */
control_tuning_t control_tuning_pressure_bench(void);
