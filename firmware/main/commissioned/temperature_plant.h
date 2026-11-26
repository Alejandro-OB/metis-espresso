#pragma once

/*
 * Planta identificada. GENERADO: no se edita a mano (ADR-020 § 4.2).
 *
 * Se regenera con `python -m software.commissioning.plant` desde el acta.
 * Acta: docs/engineering/actas/acta-temperatura-20261028b.json
 * sha256 del acta: c06aa567e929be4ad7e17d9b22df2fa07ca6a1cfd469d7a689c630fc0e4abf35
 * Fecha: 2026-10-28; operador: José Alejandro Ortega Benavides;
 * firma: José Alejandro Ortega Benavides
 * Calibración vigente: ntc-2026-10-09
 * Estimación: Primer orden con retardo ajustado al escalón heater-step-2pct-v2 (2 % durante 3 h desde caldera fría, 5.1) con software/commissioning/identify.py step: K por mínimos cuadrados para cada (θ, τ), θ y τ por rejilla con afinado, demanda de la columna u; 5,2 constantes de tiempo cubiertas, RMSE 0,57 °C. Sin cambios desde la v2 (acta-temperatura-20261010b). Evidencia: docs/engineering/evidencias/identificacion-termica-2026-10-10.md
 * Unidades del lazo: °C; ganancia por punto de salida, velocidad por segundo.
 *
 * Que este archivo exista pone `commissioned` en verdadero para este lazo.
 */

#define COMMISSIONED_TEMPERATURE_IDENTIFIER "acta-temperatura-20261028b-v12"
#define COMMISSIONED_TEMPERATURE_GAIN 32.58F
#define COMMISSIONED_TEMPERATURE_TIME_CONSTANT_S 2076.0F
#define COMMISSIONED_TEMPERATURE_DEAD_TIME_S 44.0F
#define COMMISSIONED_TEMPERATURE_LAMBDA_S 45.0F
#define COMMISSIONED_TEMPERATURE_FILTER_TIME_CONSTANT_S 0.5F
#define COMMISSIONED_TEMPERATURE_SETPOINT_RATE 0.5F
#define COMMISSIONED_TEMPERATURE_OUTPUT_MAXIMUM 10.0F
#define COMMISSIONED_TEMPERATURE_HARD_LIMIT 120.0F
#define COMMISSIONED_TEMPERATURE_FAST_START_MARGIN_C 25.0F
#define COMMISSIONED_TEMPERATURE_FAST_START_BAND_C 42.0F
#define COMMISSIONED_TEMPERATURE_FAST_START_MAX_BOOST_S 70.0F
#define COMMISSIONED_TEMPERATURE_FAST_START_MAX_SETPOINT_C 106.0F
#define COMMISSIONED_TEMPERATURE_FAST_START_RELEASE_C 0.2F
#define COMMISSIONED_TEMPERATURE_FEEDFORWARD_GAIN 0.75F
#define COMMISSIONED_TEMPERATURE_WATER_GAIN 0.9093F
#define COMMISSIONED_TEMPERATURE_WATER_OFFSET_C -2.39F
#define COMMISSIONED_TEMPERATURE_WATER_BOILER_MINIMUM_C 96.9F
#define COMMISSIONED_TEMPERATURE_WATER_BOILER_MAXIMUM_C 104.88F
#define COMMISSIONED_TEMPERATURE_FIRST_USE_MAX_OFFSET_C 4.0F
#define COMMISSIONED_TEMPERATURE_FIRST_USE_MAX_FEEDFORWARD_GAIN 1.25F
