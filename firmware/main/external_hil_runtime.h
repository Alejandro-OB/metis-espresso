#pragma once

#include "esp_err.h"

/* Inicia el canal HIL por la consola serie. Las salidas físicas no se usan. */
esp_err_t external_hil_runtime_start(void);
