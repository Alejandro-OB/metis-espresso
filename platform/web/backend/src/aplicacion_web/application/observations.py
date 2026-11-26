"""Observaciones puntuales de una preparación, en la forma que publica el grafo.

El razonador juzga valores puntuales con unidad (ADR-004 § 11.3); la plataforma
guarda series y medidas a mano. Este módulo hace el paso de unas a otras y nada
más: reduce cada serie al tramo en que se juzga, que es tratamiento de la
medida, y deja la decisión de si algo está dentro o fuera del perfil al
razonador, que es donde vive esa regla.

**El tramo es la ventana estabilizada** (`espresso_stability`, decisión del
2026-10-10). De la ventana se publican el mínimo y el máximo: una muestra fuera
de los límites basta para la desviación, y el extremo es esa muestra. Sin
ventana, la temperatura no se publica; la presión que nunca llegó a su rango se
publica entera, porque justamente ese es su fallo.

**La temperatura de la serie es la del cuerpo de la caldera** y se publica
como tal (EKO 3.4.0). Se juzga contra la referencia de caldera que se deriva del
agua pedida (`espresso_profiles.water_range.judged_specifications`), no contra
el objetivo del agua: hasta el 2026-10-22 se comparaba el metal con el agua, y
con 92 °C de agua la caldera va a 101,9 °C. La temperatura del agua solo entra
si quien llama la da ya estimada, y se publica como estimación, nunca como
medida.

**Qué no hace.** No inventa: un dato que no consta no produce observación, y un
cálculo al que le falta un operando tampoco. No juzga, no corrige muestras y no
lee la base: recibe la preparación y su serie.
"""

from __future__ import annotations

from typing import Any

from espresso_profiles import profile_reference
from espresso_profiles.water_range import measures_boiler
from espresso_semantics.extraction import has_series
from espresso_stability.indicators import Requested, stability

BOILER_NOTE = (
    "Temperatura del cuerpo de la caldera; se compara con la referencia de caldera "
    "que se deriva del agua pedida con la recta del acta con que se hizo el café."
)


def conformity_window(
    extraction: dict[str, Any], samples: list[dict[str, Any]]
) -> dict[str, Any]:
    """El mismo tramo que los indicadores de estabilidad.

    Si el registro ya trae su ventana (la decidió el dispositivo con todas
    sus muestras, o la plataforma al cerrar) se usa esa; recalcularla sobre
    la serie archivada, más espaciada, podría abrirla en otro instante.
    """
    window = (extraction.get("statistics") or {}).get("window")
    if window:
        return dict(window)
    profile = extraction.get("profile") or {}
    requested = {}
    for magnitude in ("temperature", "pressure"):
        target, tolerance = profile_reference(profile, magnitude)
        requested[magnitude] = Requested(target=target, tolerance=tolerance)
    return stability(samples, requested["temperature"], requested["pressure"])["window"]


def boiler_at_press(samples: list[dict[str, Any]]) -> float | None:
    """El cuerpo de la caldera al pulsar: lo que decide el agua del café.

    La recta del acta da el agua sobre el café a partir del metal al pulsar
    (v6: agua = 0,8063 · caldera + 9,84; v11: 0,8819 · caldera − 0,49).
    Después la caldera cae siempre mientras sale el agua, unos 10 a 13 °C en
    un café: juzgarla durante el café, como
    hasta el 26-oct-2026, la daba fuera en todos los cafés aunque el agua
    saliera bien. Es la última lectura antes de pulsar (la placa manda el minuto
    previo con elapsedMs negativo) o, si no la hay, la primera del café.
    """
    with_value = [s for s in samples if s.get("temperatureC") is not None]
    before = [s for s in with_value if int(s.get("elapsedMs", 0)) < 0]
    if before:
        return float(max(before, key=lambda s: int(s["elapsedMs"]))["temperatureC"])
    after = [s for s in with_value if int(s.get("elapsedMs", 0)) >= 0]
    if after:
        return float(min(after, key=lambda s: int(s["elapsedMs"]))["temperatureC"])
    return None


def preparation_observations(
    extraction: dict[str, Any],
    samples: list[dict[str, Any]],
    window: dict[str, Any] | None,
    water_estimate: dict[str, Any] | None = None,
) -> list[dict[str, Any]]:
    result_time = extraction.get("finishedAt") or extraction.get("startedAt")
    if result_time is None:
        return []
    # La caldera al pulsar sale del minuto previo, antes de descartarlo.
    press_c = boiler_at_press(samples) if measures_boiler(extraction) else None
    # El minuto previo a pulsar (elapsedMs negativo) no es la preparación: con
    # él, la estabilidad térmica mediría el reposo y no el café.
    samples = [s for s in samples if int(s.get("elapsedMs", 0)) >= 0]
    manual = extraction.get("manualMeasurements") or {}
    environment = extraction.get("environment") or {}
    manual_time = manual.get("recordedAt") or result_time
    recorder = {"recordedBy": manual.get("recordedBy"), "recordedAt": manual.get("recordedAt")}
    observations: list[dict[str, Any]] = []

    def add(key: str, prop: str, unit: str, value: Any, **extra: Any) -> None:
        if value is None:
            return
        observations.append(
            {"key": key, "property": prop, "unit": unit, "value": float(value),
             "kind": "measured", "resultTime": result_time, **extra}
        )

    add("coffee-dose", "coffee-dose", "g", manual.get("coffeeDoseG"),
        resultTime=manual_time, **recorder)
    add("beverage-mass", "beverage-mass", "g", manual.get("beverageMassG"),
        feature="beverage", resultTime=manual_time, **recorder)
    add("tds", "tds", "%", manual.get("tdsPercent"),
        feature="beverage", resultTime=manual_time, **recorder)
    # Del cuarto, no de la extracción: cuelgan de su condición ambiental (PC17).
    add("ambient-temperature", "ambient-temperature", "°C", environment.get("temperatureC"),
        feature="environment")
    add("relative-humidity", "relative-humidity", "%", environment.get("humidityPercent"),
        feature="environment")
    if extraction.get("durationMs") is not None:
        add("extraction-time", "extraction-time", "s", float(extraction["durationMs"]) / 1000)

    present = {item["key"] for item in observations}
    if {"coffee-dose", "beverage-mass"} <= present:
        dose, mass = float(manual["coffeeDoseG"]), float(manual["beverageMassG"])
        add("brew-ratio", "brew-ratio", ":1", mass / dose, kind="calculated",
            resultTime=manual_time, computedFrom=["coffee-dose", "beverage-mass"],
            method="urn:method:brew-ratio:beverage-over-dose")
    # El rendimiento lo calcula quien registra las medidas; aquí solo se declara
    # de qué sale. Sin sus tres operandos publicados no se puede declarar.
    if manual.get("extractionYieldPercent") is not None and {
        "coffee-dose", "beverage-mass", "tds"
    } <= present:
        add("extraction-yield", "extraction-yield", "%", manual["extractionYieldPercent"],
            kind="calculated", resultTime=manual_time,
            computedFrom=["coffee-dose", "beverage-mass", "tds"],
            method="urn:method:extraction-yield:tds-beverage-dose")

    # Las series solo existen en el grafo si la preparación tiene muestras.
    if not has_series(extraction):
        return observations
    temperatures = [float(s["temperatureC"]) for s in samples
                    if s.get("temperatureC") is not None]
    if temperatures:
        add("thermal-stability", "thermal-stability", "°C",
            max(temperatures) - min(temperatures), kind="calculated",
            fromSeries="temperature", method="urn:method:thermal-stability:range")

    # El agua sobre el café no la mide nadie: entra solo si viene estimada, con
    # su método, y se publica como tal. El razonador no la juzga contra el
    # perfil como si fuera una medida; la usan las reglas que la necesitan.
    if water_estimate and water_estimate.get("valueC") is not None:
        add("water-temperature-estimate", "water-temperature", "°C",
            water_estimate["valueC"], kind="estimated", fromSeries="temperature",
            method=str(water_estimate["method"]),
            note=water_estimate.get("note"))

    # En un café medido la temperatura que se juzga es la caldera al pulsar,
    # haya o no ventana de presión (boiler_at_press).
    add("boiler-at-press", "boiler-temperature", "°C", press_c, kind="calculated",
        fromSeries="temperature", method="urn:method:series-at-press", note=BOILER_NOTE)

    status = (window or {}).get("status")
    if status == "stabilized":
        start = int((window or {}).get("startMs") or 0)
        channels = (("pressure", "pressure", "pressureBar", "bar"),) if measures_boiler(
            extraction
        ) else (("temperature", "water-temperature", "temperatureC", "°C"),
                ("pressure", "pressure", "pressureBar", "bar"))
        method = "urn:method:series-window-extreme"
    elif status == "never-stabilized":
        start = 0
        channels = (("pressure", "pressure", "pressureBar", "bar"),)
        method = "urn:method:series-extreme"
    else:
        return observations
    for series, prop, field, unit in channels:
        values = [float(s[field]) for s in samples
                  if s.get(field) is not None and int(s["elapsedMs"]) >= start]
        if not values:
            continue
        note = BOILER_NOTE if series == "temperature" else None
        for extreme, value in (("minimum", min(values)), ("maximum", max(values))):
            add(f"{series}-{extreme}", prop, unit, value, kind="calculated",
                fromSeries=series, method=f"{method}:{extreme}", note=note)
    return observations
