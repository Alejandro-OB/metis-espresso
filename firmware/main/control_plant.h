#pragma once

/*
 * Modelo de respuesta del proceso, para cerrar el lazo en banco.
 *
 * ## Por qué existe, teniendo ya `simulated_plant`
 *
 * Son dos objetos distintos y conviene no confundirlos. `simulated_plant`
 * describe **qué ocurre en cada estado operativo**: sube la temperatura cuando
 * la máquina calienta, produce presión mientras hay extracción, inyecta fallos.
 * Es un generador de escenarios, y existe para que el supervisor y la máquina de
 * estados tengan algo que vigilar. Su evolución no depende de ninguna orden del
 * controlador, y no debería: si dependiera, dejaría de servir para probar la
 * supervisión con el control apagado.
 *
 * Este módulo responde a otra pregunta: **qué hace el proceso cuando aplico
 * `u`**. Sin eso no se puede cerrar un lazo. Un PI enfrentado a una planta que
 * ignora su salida no está regulando nada; está hablando solo, y la prueba
 * parecería funcionar exactamente igual estuviera bien o mal sintonizado.
 *
 * ## La forma del modelo, y por qué esta
 *
 * Primer orden con retardo, que es la misma forma con la que ADR-007 § 5.5 y
 * ADR-009 § 5.5 mandan estimar la planta real para sintonizar por SIMC:
 *
 *     G(s) = K · e^(−θs) / (τ·s + 1)
 *
 * Usar aquí la misma estructura tiene una consecuencia deliberada: el
 * procedimiento de sintonía que esos ADR prescriben se puede **ejercitar** ya,
 * sobre un modelo cuyos parámetros se conocen, y comprobar que produce lo que
 * debería. Cuando existan las medidas reales, lo único que cambia son tres
 * números.
 *
 * La discretización es de Euler hacia atrás, `α = Ts/(τ + Ts)`, por la misma
 * razón que en `control_filter`: mantiene `α ≤ 1` para cualquier periodo, de
 * modo que un paso mayor que la constante de tiempo produce una respuesta lenta
 * y no una oscilación numérica que se confundiría con un problema de sintonía.
 *
 * ## Advertencia sobre los parámetros
 *
 * Los conjuntos que devuelven `control_plant_pressure_bench` y
 * `control_plant_temperature_bench` **no están identificados**. Son cifras de
 * banco, elegidas para que el orden de magnitud sea el de la máquina —la de
 * temperatura reproduce los 1,4 K/s con la caldera llena que calcula ADR-009
 * § 3.7—, y sirven para ejercitar la estructura del lazo, no para predecir su
 * comportamiento. Ninguna sintonía obtenida contra ellas autoriza nada: la
 * puesta en servicio sigue exigiendo los registros reales que describen ADR-007
 * § 5 y ADR-009 § 5.
 */

#include <stdbool.h>
#include <stdint.h>

/*
 * Retardo máximo expresable, en periodos de muestreo.
 *
 * Con el lazo térmico a 200 ms cubre 12,8 s de retardo, y con el de presión a
 * 20 ms, 1,28 s. Ambos holgados frente a lo que se espera. Un retardo que no
 * quepa se rechaza en la configuración en lugar de recortarse: una planta con
 * la mitad del retardo que se le pidió es otra planta, y la sintonía que
 * saliera de ella no valdría para la pedida.
 */
#define CONTROL_PLANT_MAX_DELAY_SAMPLES 256U

typedef struct {
    float gain;             /* K: unidades de la magnitud por cada 1 % de actuación */
    float time_constant_s;  /* τ */
    float dead_time_s;      /* θ */
    float sample_period_s;  /* Ts, el periodo con el que se avanzará */
    float rest_value;       /* a dónde tiende con u = 0: ambiente, o presión nula */
} control_plant_parameters_t;

typedef struct {
    control_plant_parameters_t parameters;
    float alpha;
    float value;
    float delay_line[CONTROL_PLANT_MAX_DELAY_SAMPLES];
    uint32_t delay_samples;
    uint32_t delay_index;
} control_plant_t;

/* Presión: unos 9 bar con la bomba al 100 %, con la dinámica rápida que cabe
 * esperar de una bomba vibratoria contra una cama de café. Cifras de banco. */
control_plant_parameters_t control_plant_pressure_bench(void);

/* Temperatura: 1,4 K/s de ascenso inicial al 100 %, que es lo que da 1050 W
 * sobre los ~750 J/K del conjunto lleno según ADR-009 § 3.7. Cifras de banco. */
control_plant_parameters_t control_plant_temperature_bench(void);

bool control_plant_configure(control_plant_t *plant,
                             const control_plant_parameters_t *parameters);

/* Lleva la magnitud al valor dado y vacía la historia de actuación. */
void control_plant_reset(control_plant_t *plant, float initial_value);

/*
 * Avanza un periodo con la actuación indicada, en por ciento, y devuelve la
 * magnitud resultante.
 *
 * Una actuación no finita se trata como cero, por el mismo criterio que aplican
 * los cuantizadores: ante un número imposible, no actuar.
 */
float control_plant_step(control_plant_t *plant, float actuation_percent);

float control_plant_value(const control_plant_t *plant);

/* Valor al que tendería la magnitud con esta actuación mantenida. */
float control_plant_steady_state(const control_plant_t *plant, float actuation_percent);
