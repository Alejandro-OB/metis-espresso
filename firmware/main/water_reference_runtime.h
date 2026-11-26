#pragma once

/*
 * La relación caldera → agua compilada desde el acta de temperatura.
 *
 * Solo lee las macros que `software/commissioning/plant.py` escribe en
 * `commissioned/temperature_plant.h` cuando el acta trae `water_reference`; la
 * conversión está en `water_reference.c`, que es puro y se prueba en el
 * anfitrión. Sin la sección devuelve la relación a cero, que deja la
 * temperatura del perfil como temperatura de caldera.
 */

#include "water_reference.h"

water_reference_t water_reference_commissioned(void);
