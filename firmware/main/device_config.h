#pragma once

/*
 * Configuración de enlace del dispositivo: a dónde envía y con qué credencial.
 *
 * El valor compilado es la puesta en servicio; el guardado en memoria no volátil
 * es la configuración efectiva y lo desplaza. Así un cambio de red, de dirección
 * o de credencial deja de exigir recompilar y regrabar el firmware que gobierna
 * la máquina (ADR-012 § 4.4).
 *
 * Cada valor declara de dónde procede. En cuanto lo guardado puede desplazar a
 * lo compilado, mirar la configuración de compilación deja de responder a la
 * pregunta de a dónde apunta este dispositivo.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "device_config_limits.h"

typedef enum {
    /* El cero no puede significar «guardado»: una estructura sin rellenar debe
     * declarar la procedencia más conservadora, no la más específica. */
    DEVICE_CONFIG_ORIGIN_COMPILED = 0,
    DEVICE_CONFIG_ORIGIN_STORED,
} device_config_origin_t;

typedef struct {
    char sync_url[DEVICE_CONFIG_URL_BYTES];
    char sync_token[DEVICE_CONFIG_TOKEN_BYTES];
    device_config_origin_t url_origin;
    device_config_origin_t token_origin;
    /*
     * De dónde se baja un firmware nuevo (ADR-021): el manifiesto que publica
     * el computador. Vacío significa «ninguna», y entonces el gesto del mando
     * no busca nada. Es aparte de la plataforma a propósito: el firmware va del
     * computador a la placa y no pasa por la plataforma externa.
     */
    char update_url[DEVICE_CONFIG_URL_BYTES];
    device_config_origin_t update_url_origin;
} device_config_t;

/*
 * Entrega la configuración vigente. Nunca falla: si no hay nada guardado, o la
 * lectura no es utilizable, devuelve los valores de puesta en servicio y lo
 * declara en el origen.
 */
void device_config_load(device_config_t *config);

/* Guarda un valor, que pasa a desplazar al compilado en los arranques siguientes. */
esp_err_t device_config_set_sync_url(const char *url);
esp_err_t device_config_set_sync_token(const char *token);
esp_err_t device_config_set_update_url(const char *url);

/* Descarta lo guardado y vuelve a la puesta en servicio. */
esp_err_t device_config_reset(void);

const char *device_config_origin_name(device_config_origin_t origin);
