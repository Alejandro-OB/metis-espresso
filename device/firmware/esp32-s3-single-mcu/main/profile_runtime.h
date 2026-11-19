#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "extraction_profile.h"

esp_err_t profile_runtime_start(void);
bool profile_runtime_get_effective(extraction_profile_t *profile);
esp_err_t profile_runtime_activate(const extraction_profile_t *profile,
                                   extraction_profile_validation_t *validation);

/* Indica si la configuración efectiva está respaldada en almacenamiento. */
bool profile_runtime_is_persisted(void);
