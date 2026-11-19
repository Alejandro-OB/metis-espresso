"""Estabilidad de la máquina, como la define la monografía (§ 4.4).

La estabilidad térmica y la de presión son de la máquina, no del perfil. La
tolerancia que el barista declara es otra cosa, el margen que acepta, y la
juzga `indicators` y la conformidad. Las dos bandas de aquí salen de lo medido:

- **Térmica: ±2,1 °C** sobre el cuerpo de la caldera frente a su referencia,
  durante **toda la espera en «lista»** antes de pulsar (decisión del usuario del
  2026-10-20; `machine-stability-v2`). Es la mayor desviación de las esperas de
  los ensayos del 16 y el 18 de octubre (−1,04 a +2,04 °C en 21 esperas,
  `evidencias/estabilidad-espera-20261022/banda.py`), redondeada hacia arriba a
  la décima, el mismo criterio que dio los ±0,8 °C de la v1 en el minuto previo.
  En la espera el cuerpo sube 1–2 °C sobre la referencia entre los 2 y los 12 min
  y vuelve antes de pulsar; el 76 % del tiempo queda dentro de ±0,8 °C. La caída
  durante el café (7 a 13 °C por el agua que entra) no cuenta.
- **Presión: ±0,18 bar**, la exactitud del transductor, desde el final de la
  subida hasta el corte, en promedios de 1 s porque la pulsación de la bomba
  mueve unos 0,2 bar entre muestras. En quince cafés a 8,5 bar los promedios
  quedaron entre −0,13 y +0,10 bar.

**La espera llega de dos fuentes.** La serie de la preparación solo trae el
minuto previo a pulsar; el resto sale de la telemetría, una muestra cada 15 s,
mientras la plataforma la recibe. El tiempo de cada lectura pesa lo que dura
hasta la siguiente, para que el minuto previo, con una lectura cada 0,5 s, no
valga más que el resto de la espera. Un hueco de más de 60 s no se rellena: el
resultado declara qué parte de la espera quedó cubierta.

**El final de la subida es el primer promedio de 1 s dentro de la banda.** La
monografía observa que a los 15 s la subida ya había terminado en todos los
cafés de E3, pero un corte fijo en 15 s no vale para otra molienda ni para un
perfil con preinfusión; es la misma razón por la que `indicators` descartó un
número fijo de segundos.

**El agua entre preparaciones, estimada.** La banda térmica de la monografía es
la del agua sobre el café, ±0,5 °C entre preparaciones, y se verifica en los
ensayos con la sonda en el portafiltro: en el uso normal no hay sensor de agua.
Lo que la plataforma puede dar en cada café es el agua **estimada** con la recta
de su acta a partir del cuerpo al pulsar, y su diferencia con la preparación
anterior. Va marcada como estimada porque esa recta no ve el primer uso frío
tras un reposo: con el cuerpo en su referencia da lo mismo café tras café.

**Qué no hace.** No inventa la espera: sin lecturas antes de pulsar, la
estabilidad térmica queda «sin muestras de la espera», no se calcula con las
del café. No convierte el agua del perfil a caldera ni busca dónde empezó la
espera: recibe la referencia de caldera y las lecturas ya hechas.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping, Sequence
from typing import Any

from espresso_profiles.water_range import CURRENT_METAL, WaterLine

MACHINE_STABILITY_RULE = "machine-stability-v2"
THERMAL_BAND_C = 2.1
PRESSURE_BAND_BAR = 0.18
AVERAGE_MS = 1_000
#: Estabilidad térmica entre preparaciones: el ±1 °C que el barista considera
#: aceptable (cap. 4 de la monografía).
WATER_BAND_C = 1.0
#: Una lectura cubre como máximo esto hasta la siguiente. La telemetría fuera
#: del café llega cada 15 s; con 60 s caben tres perdidas.
WAIT_GAP_S = 60.0


def machine_stability(
    samples: Iterable[Mapping[str, Any]],
    boiler_reference_c: float | None,
    pressure_reference_bar: float | None,
    wait_readings: Sequence[tuple[float, float]] = (),
    wait_start_s: float | None = None,
) -> dict[str, Any]:
    """Estabilidad de la máquina en una preparación.

    `wait_readings` son pares (segundos respecto al pulsado, negativos;
    temperatura del cuerpo) de la espera en «lista», y `wait_start_s` cuándo
    empezó esa espera, en la misma escala. El minuto previo de la serie
    (`elapsedMs` negativo) se suma a esas lecturas.
    """
    ordered = sorted(samples, key=lambda sample: int(sample["elapsedMs"]))
    pre_start = [(int(s["elapsedMs"]) / 1000.0, float(s["temperatureC"])) for s in ordered
                 if int(s["elapsedMs"]) < 0 and s.get("temperatureC") is not None]
    return {
        "rule": MACHINE_STABILITY_RULE,
        "thermal": _thermal([*wait_readings, *pre_start], wait_start_s, boiler_reference_c),
        "pressure": _pressure(ordered, pressure_reference_bar),
    }


def body_at_press(
    samples: Iterable[Mapping[str, Any]], wait_readings: Sequence[tuple[float, float]] = ()
) -> float | None:
    """La última lectura del cuerpo antes de pulsar, de la serie o de la espera."""
    readings = [(int(s["elapsedMs"]) / 1000.0, float(s["temperatureC"])) for s in samples
                if int(s["elapsedMs"]) < 0 and s.get("temperatureC") is not None]
    readings += [(t, value) for t, value in wait_readings if t < 0]
    return max(readings)[1] if readings else None


def estimated_water_c(
    body_c: float | None, line: WaterLine | None = CURRENT_METAL
) -> float | None:
    """El agua sobre el café que da la recta del metal, o None fuera de su rango o sin ella."""
    if body_c is None or line is None:
        return None
    return line.water_c(body_c)


def water_between_preparations(
    body_c: float | None,
    previous_body_c: float | None,
    line: WaterLine | None = CURRENT_METAL,
    previous_line: WaterLine | None = CURRENT_METAL,
) -> dict[str, Any]:
    """Agua estimada de esta preparación frente a la anterior (banda ±0,5 °C).

    Cada una con la recta del metal de su acta (`metal_line_of`). Si las dos son de
    actas distintas no se restan: la diferencia mezclaría el cambio de acta con
    el del agua, y el cambio de la v6 a la v11 es de unos 2,6 °C.
    """
    water = estimated_water_c(body_c, line)
    previous = estimated_water_c(previous_body_c, previous_line)
    result: dict[str, Any] = {"bandC": WATER_BAND_C, "estimatedC": water,
                              "previousEstimatedC": previous, "differenceC": None,
                              "within": None,
                              "method": line.method if line is not None else None,
                              "status": "estimated"}
    if line is None:
        result["status"] = "no-line"
        return result
    if water is None:
        result["status"] = "outside-line-range" if body_c is not None else "no-body-at-press"
        return result
    if previous_body_c is not None and (
        previous_line is None or previous_line.method != line.method
    ):
        result["status"] = "line-changed"
        return result
    if previous is None:
        result["status"] = "no-previous"
        return result
    difference = round(water - previous, 2)
    result.update(differenceC=difference, within=abs(difference) <= WATER_BAND_C)
    return result


def _thermal(
    readings: list[tuple[float, float]], start_s: float | None, reference: float | None
) -> dict[str, Any]:
    result: dict[str, Any] = {"bandC": THERMAL_BAND_C, "referenceC": reference,
                              "status": "no-reference", "fraction": None, "sampleCount": 0,
                              "waitS": None, "coverage": None, "maxDeviationC": None}
    if reference is None:
        return result
    begin = min((t for t, _ in readings), default=None) if start_s is None else start_s
    points = sorted({t: value for t, value in readings if begin is not None and begin <= t < 0}.items())
    result["sampleCount"] = len(points)
    if not points or begin is None:
        result["status"] = "no-wait-samples"
        return result
    # Cada lectura vale el tiempo hasta la siguiente (la última, hasta el pulsado),
    # con el tope del hueco: lo que no se vio no se pondera.
    ends = [t for t, _ in points[1:]] + [0.0]
    weights = [min(end - t, WAIT_GAP_S) for (t, _), end in zip(points, ends, strict=True)]
    covered = sum(weights) + min(points[0][0] - begin, WAIT_GAP_S)
    total = -begin
    inside = sum(w for (_, value), w in zip(points, weights, strict=True)
                 if abs(value - reference) <= THERMAL_BAND_C)
    deviations = [value - reference for _, value in points]
    result.update(
        status="measured",
        fraction=round(inside / sum(weights), 3) if sum(weights) > 0 else None,
        waitS=round(total, 1),
        coverage=round(min(1.0, covered / total), 3) if total > 0 else None,
        maxDeviationC=round(max(deviations, key=abs), 2),
    )
    return result


def _pressure(samples: list[Mapping[str, Any]], reference: float | None) -> dict[str, Any]:
    result: dict[str, Any] = {"bandBar": PRESSURE_BAND_BAR, "referenceBar": reference,
                              "status": "no-reference", "fraction": None, "startMs": None,
                              "averageCount": 0}
    if reference is None:
        return result
    buckets: dict[int, list[float]] = {}
    for sample in samples:
        elapsed = int(sample["elapsedMs"])
        if elapsed < 0 or sample.get("pressureBar") is None:
            continue
        buckets.setdefault(elapsed // AVERAGE_MS, []).append(float(sample["pressureBar"]))
    averages = [(index, sum(values) / len(values)) for index, values in sorted(buckets.items())]
    start = next((index for index, value in averages
                  if abs(value - reference) <= PRESSURE_BAND_BAR), None)
    if start is None:
        result["status"] = "never-reached-band"
        return result
    tail = [value for index, value in averages if index >= start]
    inside = sum(abs(value - reference) <= PRESSURE_BAND_BAR for value in tail)
    result.update(status="measured", fraction=round(inside / len(tail), 3),
                  startMs=start * AVERAGE_MS, averageCount=len(tail))
    return result
