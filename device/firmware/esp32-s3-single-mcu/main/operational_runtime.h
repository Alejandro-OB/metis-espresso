#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include "operational_fsm.h"

esp_err_t operational_runtime_start(void);

bool operational_runtime_post_event(operational_event_t event,
                                    TickType_t wait_ticks);
bool operational_runtime_post_events(uint32_t pending_events,
                                     TickType_t wait_ticks);
bool operational_runtime_receive_transition(operational_transition_t *transition,
                                            TickType_t wait_ticks);

operational_state_t operational_runtime_state(void);

/* Instante monotónico (µs) del último botón del mando que la máquina de estados
 * rechazó; 0 si ninguno. Lo usa el panel para el destello rojo. */
uint64_t operational_runtime_last_rejected_press_us(void);

/*
 * Autorización que corresponde al estado vigente. Es lo que el estado permite,
 * no lo que se hará: el guardián de actuación puede inhibir aunque el estado
 * autorice, y nunca al revés.
 */
operational_authorization_t operational_runtime_authorization(void);
