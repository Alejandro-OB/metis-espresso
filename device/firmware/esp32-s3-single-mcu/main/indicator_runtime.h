#pragma once

/*
 * Tarea que enciende los LED del mando (RF-011).
 *
 * Es la envoltura de hardware de `indicator_panel`: pregunta al supervisor cuál
 * es el estado y qué veredicto tiene cada salida, se lo pasa al módulo puro y
 * escribe los cuatro pines con lo que este decide. Nada más.
 *
 * ## Por qué lee el supervisor y no la cola de transiciones
 *
 * El supervisor publica el estado y los dos veredictos **tomados en el mismo
 * instante**. Leerlos por separado podría dar un estado de un ciclo y un
 * veredicto de otro, y el panel enseñaría una combinación que nunca existió.
 *
 * Además, la cola de transiciones de `operational_runtime` entrega cada cambio
 * **una sola vez**: un segundo consumidor se repartiría las transiciones con el
 * que ya existe y cada uno vería la mitad. Consultar el estado vigente no
 * consume nada y no interfiere con nadie.
 *
 * ## Por qué es una tarea aparte
 *
 * Podría vivir dentro del supervisor, que ya tiene los datos y corre a 50 ms,
 * pero el supervisor es la ruta de corte de las salidas de potencia y conviene
 * que no haga nada más. Cuatro escrituras de GPIO cada 50 ms no justifican
 * ensuciar el único componente cuya responsabilidad es detener la máquina.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "indicator_panel.h"

typedef struct {
    bool running;
    uint32_t cycles;
    indicator_panel_t panel;      /* patrones vigentes */
    operational_state_t state;    /* con el que se calcularon */
    bool fault_warned;            /* el aviso de avería está activo */
} indicator_status_t;

/*
 * Arranca la tarea. Los pines ya están configurados como salida durante el
 * diagnóstico de arranque; aquí solo se les escribe.
 *
 * Debe iniciarse **después** de la prueba visual de LED del arranque, o las dos
 * se disputarían los mismos pines y la secuencia de comprobación no se vería.
 */
esp_err_t indicator_runtime_start(void);

bool indicator_runtime_get_status(indicator_status_t *status);
