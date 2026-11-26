#pragma once

/*
 * La planta identificada de cada lazo, si este firmware se compiló con ella
 * (ADR-020 § 4.2, F3).
 *
 * Los parámetros que salen del acta de puesta en servicio no se escriben a mano
 * en ninguna parte: el generador `software/commissioning/plant.py` los escribe,
 * desde el acta firmada, en `commissioned/pressure_plant.h` o
 * `commissioned/temperature_plant.h`, y ese archivo se versiona con su acta.
 * Esta cabecera solo detecta si existe.
 *
 * **Que exista es lo que pone `commissioned` en verdadero**, y solo para su lazo.
 * No hay opción de configuración, orden por el puerto ni mensaje de la red que
 * lo haga: la llave de la actuación cambia únicamente con un commit que trae un
 * acta y un firmware recompilado. Sin archivo —que es como está el repositorio
 * hoy, porque ninguna planta se ha medido— el lazo sigue sin poner en servicio y
 * el guardián inhibe.
 *
 * Se detecta con `__has_include` y no con una opción de Kconfig a propósito: una
 * opción podría activarse sin acta, y un archivo generado no.
 *
 * Cada archivo define, con el prefijo `COMMISSIONED_PRESSURE_` o
 * `COMMISSIONED_TEMPERATURE_`:
 *
 *   IDENTIFIER              acta y versión; viaja en cada línea del registro
 *   GAIN                    K, en unidad del lazo por punto de salida
 *   TIME_CONSTANT_S         τ
 *   DEAD_TIME_S             θ
 *   LAMBDA_S                λ de SIMC, elegido en la sintonía
 *   FILTER_TIME_CONSTANT_S  τf del filtro del lazo
 *   SETPOINT_RATE           velocidad del limitador de referencia, por segundo
 *   HARD_LIMIT              límite duro: bar para la presión, °C para el calentador
 *
 * El acta de temperatura puede traer además el arranque rápido (ADR-009,
 * enmienda del 2026-10-11). Sin estos cinco, no actúa:
 *
 *   FAST_START_MARGIN_C         δ: el corte es la referencia menos δ
 *   FAST_START_BAND_C           banda de activación por debajo del corte
 *   FAST_START_MAX_BOOST_S      tiempo máximo al 100 %
 *   FAST_START_MAX_SETPOINT_C   referencia máxima con arranque rápido
 *   FAST_START_RELEASE_C        caída desde el máximo que termina la espera
 *
 * Y la ganancia de la prealimentación (puerta 2, enmienda del 2026-10-12); sin
 * ella, la puerta queda cerrada:
 *
 *   FEEDFORWARD_GAIN            % de calentador por punto de densidad de bomba
 *
 * Y la recta caldera → agua (ADR-009, enmienda del 2026-10-18); sin ella, la
 * temperatura del perfil es la de la caldera (ver `water_reference.h`):
 *
 *   WATER_GAIN                  °C de agua por °C de caldera
 *   WATER_OFFSET_C              ordenada de la recta
 *   WATER_BOILER_MINIMUM_C      intervalo de caldera medido; fuera, se rechaza
 *   WATER_BOILER_MAXIMUM_C
 *
 * Y el máximo de la compensación del primer uso (ADR-009, enmienda del
 * 2026-10-19); sin él, la placa rechaza cualquier desplazamiento que pida el
 * perfil (ver `first_use_offset.h` y `first_use_offset_runtime.h`):
 *
 *   FIRST_USE_MAX_OFFSET_C      °C que puede subir la referencia de caldera
 *
 * El acta de presión puede traer la preinfusión a presión de extracción
 * (ADR-007, enmienda del 2026-10-12); sin ella, el PI regula desde el
 * principio:
 *
 *   PREINFUSION_MARGIN_BAR                 el PI entra a la referencia menos esto
 *   PREINFUSION_FILTER_TIME_CONSTANT_S     filtro de la decisión del paso
 *   PREINFUSION_INTEGRAL_PERCENT           integral con que arranca el PI
 */

#if defined(__has_include)
#if __has_include("commissioned/pressure_plant.h")
#include "commissioned/pressure_plant.h"
#endif
#if __has_include("commissioned/temperature_plant.h")
#include "commissioned/temperature_plant.h"
#endif
#endif

#if defined(COMMISSIONED_PRESSURE_IDENTIFIER)
#define HAVE_COMMISSIONED_PRESSURE 1
#else
#define HAVE_COMMISSIONED_PRESSURE 0
#endif

#if defined(COMMISSIONED_TEMPERATURE_IDENTIFIER)
#define HAVE_COMMISSIONED_TEMPERATURE 1
#else
#define HAVE_COMMISSIONED_TEMPERATURE 0
#endif
