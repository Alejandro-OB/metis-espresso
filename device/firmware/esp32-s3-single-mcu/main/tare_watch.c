#include "tare_watch.h"

#include <math.h>
#include <stddef.h>

bool tare_watch_update(tare_watch_t *watch, const tare_watch_input_t *input)
{
    if (watch == NULL || input == NULL) {
        return false;
    }
    if (input->tare_taken) {
        watch->armed = true;
        watch->drifted = false;
    }
    if (input->state == OP_STATE_EXTRACTION || input->state == OP_STATE_MANUAL_PURGE) {
        watch->armed = false;
        watch->drifted = false;
        return false;
    }
    if (watch->armed && !watch->drifted &&
        (!input->mass_available || !isfinite(input->grams) ||
         fabsf(input->grams) > TARE_WATCH_LIMIT_G)) {
        watch->drifted = true;
    }
    return watch->drifted;
}
