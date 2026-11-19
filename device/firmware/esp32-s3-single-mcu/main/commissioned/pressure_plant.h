#pragma once

/*
 * Planta identificada. GENERADO: no se edita a mano (ADR-020 § 4.2).
 *
 * Se regenera con `python -m software.commissioning.plant` desde el acta.
 * Acta: device/docs/engineering/actas/acta-presion-20261012.json
 * sha256 del acta: 5531c38045288eebee23fb43b9faf0b269d0327386b90b90c8b9e3e4eb415918
 * Fecha: 2026-10-12; operador: José Alejandro Ortega Benavides;
 * firma: José Alejandro Ortega Benavides
 * Calibración vigente: presion-2026-10-08 v2
 * Estimación: Primer orden con retardo ajustado a la escalera de la bomba con café (6.5 y 6.6, 2026-10-11): retardo de 60–100 ms y constante rápida de 40–100 ms entre el 50 y el 75 %, sobre la columna filtrada a 50 ms; K entre las mesetas altas (cota superior). La subida lenta de la pastilla al mojarse no se modela: la corrige la integral y la preinfusión. Evidencia: docs/engineering/evidencias/escalera-con-cafe-2026-10-11.md y sintonia-presion-2026-10-12.md
 * Unidades del lazo: bar; ganancia por punto de salida, velocidad por segundo.
 *
 * Que este archivo exista pone `commissioned` en verdadero para este lazo.
 */

#define COMMISSIONED_PRESSURE_IDENTIFIER "acta-presion-20261012-v1"
#define COMMISSIONED_PRESSURE_GAIN 0.06F
#define COMMISSIONED_PRESSURE_TIME_CONSTANT_S 0.1F
#define COMMISSIONED_PRESSURE_DEAD_TIME_S 0.1F
#define COMMISSIONED_PRESSURE_LAMBDA_S 1.0F
#define COMMISSIONED_PRESSURE_FILTER_TIME_CONSTANT_S 0.02F
#define COMMISSIONED_PRESSURE_SETPOINT_RATE 3.0F
#define COMMISSIONED_PRESSURE_OUTPUT_MAXIMUM 75.0F
#define COMMISSIONED_PRESSURE_HARD_LIMIT 12.0F
#define COMMISSIONED_PRESSURE_PREINFUSION_MARGIN_BAR 0.5F
#define COMMISSIONED_PRESSURE_PREINFUSION_FILTER_TIME_CONSTANT_S 0.3F
#define COMMISSIONED_PRESSURE_PREINFUSION_INTEGRAL_PERCENT 15.0F
