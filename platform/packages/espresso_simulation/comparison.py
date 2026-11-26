"""Comparación determinista entre la serie medida de una preparación y su réplica simulada.

Módulo puro: no depende de la aplicación web, del almacenamiento ni de FMPy.
Recibe las dos series ya leídas y devuelve el documento de comparación.

Convención de signo: la diferencia es ``simulado - medido``, igual que el
``errorTemperatura`` del modelo Modelica, de modo que un valor positivo
significa que el modelo predice por encima de lo observado.
"""

from __future__ import annotations

from bisect import bisect_left
from dataclasses import dataclass
from math import sqrt
from typing import Any

SCHEMA_VERSION = "1.0"
ALIGNMENT_METHOD = "linear-interpolation-on-measured-grid"

CHANNEL_UNITS = {"temperatureC": "Cel", "pressureBar": "bar"}

#: Diferencia máxima admisible entre lo medido y lo simulado, evaluada sobre el
#: error cuadrático medio: el mayor que tuvo el modelo en su propia comprobación,
#: sin margen. Un veredicto «fuera» dice entonces que el modelo se equivoca más
#: de lo que se equivocó cuando se lo validó.
#:
#: Temperatura, 1,85 °C: RMSE máximo de la FMU 2.1 en la réplica de la
#: plataforma sobre las preparaciones reservadas de su ajuste (sim-00000342,
#: `agua-retenida-2.1.md`, `agua_retenida/replica-plataforma.txt`).
#: Presión, 0,74 bar: RMSE máximo del modelo hidráulico 10.4 sobre la curva
#: completa de los 5 cafés de validación (`modelo-presion-10.4.md`, 357cf90).
#:
#: Hasta el 19-nov-2026 las dos llevaban un 20 % más (2,22 °C y 0,89 bar), un
#: margen «provisional» del 14-sep que nunca tuvo razón escrita; se quitó para
#: que el criterio de la monografía tenga origen (decisión del usuario). Con los
#: errores de la catación del 4-nov, la presión pasa de 11 a 10 cafés dentro.
#:
#: La presión solo vota con un modelo que el registro declare estable en
#: presión (`channel_limitation`); con la FMU v1 se muestra y no cuenta.
DEFAULT_MODEL_TOLERANCES: dict[str, float] = {"temperatureC": 1.85, "pressureBar": 0.74}

#: Fracción mínima de muestras medidas que deben poder compararse para emitir un
#: juicio sobre el modelo.
DEFAULT_MINIMUM_COVERAGE = 0.8

#: Limitación que se declara a un canal cuando nadie dijo qué tan maduro es el
#: modelo que lo simula: la presión del modelo de partida es hidráulica
#: estática, y sin declaración en contra no se le da voto.
CHANNEL_LIMITATIONS = {"pressureBar": "static-hydraulic-model"}

#: Madurez del canal, según el registro de modelos, que le permite votar.
STABLE = "stable"
EXPERIMENTAL_CHANNEL = "experimental-model-channel"
TOLERANCE_PENDING = "tolerance-pending"

WITHIN = "within-model-tolerance"
OUTSIDE = "outside-model-tolerance"
INSUFFICIENT = "insufficient-coverage"
NO_TOLERANCE = "no-tolerance-declared"
NOT_EVALUABLE = "not-evaluable"


@dataclass(frozen=True)
class AlignedPoint:
    elapsed_ms: int
    measured: float
    simulated: float

    @property
    def difference(self) -> float:
        return self.simulated - self.measured


def _round(value: float) -> float:
    return round(value, 6)


def measured_points(
    samples: list[dict[str, Any]], channel: str
) -> tuple[list[tuple[int, float]], int]:
    """Devuelve los puntos medidos utilizables y cuántos se descartaron por calidad.

    Una muestra solo es utilizable si su calidad es ``valid`` y el canal tiene
    valor numérico. Las ausencias nunca se imputan.
    """
    usable: list[tuple[int, float]] = []
    discarded = 0
    for sample in samples:
        value = sample.get(channel)
        if sample.get("quality") != "valid" or not isinstance(value, int | float):
            discarded += 1
            continue
        usable.append((int(sample["elapsedMs"]), float(value)))
    usable.sort(key=lambda item: item[0])
    return usable, discarded


def simulated_series(
    samples: list[dict[str, Any]],
    channel: str,
    initial_conditions: dict[str, float] | None = None,
) -> list[tuple[float, float]]:
    """Normaliza la serie simulada a pares (segundos, valor) ordenados.

    La FMU registra su primera muestra un paso de comunicación después del
    arranque, de modo que el instante ``t = 0`` no aparece en la serie aunque su
    estado sea conocido: es la condición inicial con la que se inicializó el
    modelo. Cuando se proporciona, ese estado se incorpora como punto inicial;
    si no, el primer instante medido quedará fuera del intervalo simulado y se
    contabilizará como tal, sin extrapolar.
    """
    series = [
        (float(sample["simulationTimeSeconds"]), float(sample["channels"][channel]))
        for sample in samples
        if channel in (sample.get("channels") or {})
    ]
    series.sort(key=lambda item: item[0])
    initial = (initial_conditions or {}).get(channel)
    if initial is not None and (not series or series[0][0] > 0.0):
        series.insert(0, (0.0, float(initial)))
    return series


def interpolate(series: list[tuple[float, float]], seconds: float) -> float | None:
    """Valor simulado en un instante, por interpolación lineal entre muestras.

    Devuelve ``None`` si el instante cae fuera del intervalo simulado: no se
    extrapola.
    """
    if not series:
        return None
    times = [point[0] for point in series]
    if seconds < times[0] or seconds > times[-1]:
        return None
    index = bisect_left(times, seconds)
    if times[index] == seconds:
        return series[index][1]
    previous_time, previous_value = series[index - 1]
    next_time, next_value = series[index]
    span = next_time - previous_time
    if span <= 0:
        return previous_value
    weight = (seconds - previous_time) / span
    return previous_value + weight * (next_value - previous_value)


def align(
    measured: list[dict[str, Any]],
    simulated: list[dict[str, Any]],
    channel: str,
    initial_conditions: dict[str, float] | None = None,
) -> tuple[list[AlignedPoint], dict[str, int]]:
    """Alinea ambas series sobre la rejilla de las muestras medidas válidas."""
    usable, invalid_quality = measured_points(measured, channel)
    series = simulated_series(simulated, channel, initial_conditions)
    points: list[AlignedPoint] = []
    outside = 0
    for elapsed_ms, value in usable:
        simulated_value = interpolate(series, elapsed_ms / 1000)
        if simulated_value is None:
            outside += 1
            continue
        points.append(AlignedPoint(elapsed_ms, value, simulated_value))
    return points, {
        "invalidQuality": invalid_quality,
        "outsideSimulatedRange": outside,
    }


def channel_metrics(
    points: list[AlignedPoint],
    measured_total: int,
    tolerance: float | None,
    minimum_coverage: float,
) -> dict[str, Any]:
    coverage = len(points) / measured_total if measured_total else 0.0
    metrics: dict[str, Any] = {
        "comparedSampleCount": len(points),
        "coverageRatio": _round(coverage),
    }
    if not points:
        metrics.update(
            {
                "meanSignedDifference": None,
                "meanAbsoluteDifference": None,
                "rootMeanSquareDifference": None,
                "maxAbsoluteDifference": None,
                "elapsedMsAtMaxDifference": None,
                "modelTolerance": tolerance,
                "verdict": INSUFFICIENT,
            }
        )
        return metrics
    differences = [point.difference for point in points]
    worst = max(points, key=lambda point: abs(point.difference))
    rmse = sqrt(sum(value * value for value in differences) / len(differences))
    metrics.update(
        {
            "meanSignedDifference": _round(sum(differences) / len(differences)),
            "meanAbsoluteDifference": _round(sum(abs(v) for v in differences) / len(differences)),
            "rootMeanSquareDifference": _round(rmse),
            "maxAbsoluteDifference": _round(abs(worst.difference)),
            "elapsedMsAtMaxDifference": worst.elapsed_ms,
            "modelTolerance": tolerance,
        }
    )
    if coverage < minimum_coverage:
        metrics["verdict"] = INSUFFICIENT
    elif tolerance is None:
        metrics["verdict"] = NO_TOLERANCE
    else:
        metrics["verdict"] = WITHIN if rmse <= tolerance else OUTSIDE
    return metrics


def overall_verdict(channels: list[dict[str, Any]]) -> str:
    contributing = [item for item in channels if item["contributesToVerdict"]]
    if not contributing:
        return NOT_EVALUABLE
    verdicts = {item["verdict"] for item in contributing}
    if INSUFFICIENT in verdicts:
        return INSUFFICIENT
    if OUTSIDE in verdicts:
        return OUTSIDE
    if verdicts == {WITHIN}:
        return WITHIN
    return NOT_EVALUABLE


def channel_limitation(
    channel: str, maturity: str | None, tolerance: float | None
) -> str | None:
    """Por qué un canal no vota, o ``None`` si vota.

    Vota el canal que el registro declara estable y que tiene tolerancia. Uno
    estable sin tolerancia no vota: entrar al veredicto sin ella dejaría sin
    veredicto a toda la comparación, también a la temperatura, que sí la tiene.
    Así, la presión entra sola en cuanto el registro publique el modelo
    hidráulico ajustado como estable y se declare su tolerancia.
    """
    if maturity is None:
        return CHANNEL_LIMITATIONS.get(channel)
    if maturity != STABLE:
        return EXPERIMENTAL_CHANNEL
    if tolerance is None:
        return TOLERANCE_PENDING
    return None


def compare(
    *,
    extraction_id: str,
    simulation_run_id: str,
    measured: list[dict[str, Any]],
    simulated: list[dict[str, Any]],
    channels: tuple[str, ...] = ("temperatureC", "pressureBar"),
    model_tolerances: dict[str, float] | None = None,
    minimum_coverage: float = DEFAULT_MINIMUM_COVERAGE,
    initial_conditions: dict[str, float] | None = None,
    channel_maturity: dict[str, str] | None = None,
) -> dict[str, Any]:
    """Compara una preparación con su réplica simulada y devuelve el documento.

    ``initial_conditions`` son los valores por canal con los que se inicializó el
    modelo, que completan la serie simulada en ``t = 0``. ``channel_maturity`` es
    la madurez que el registro de modelos declara para cada canal.
    """
    if not 0.0 <= minimum_coverage <= 1.0:
        raise ValueError("minimum_coverage debe estar entre 0 y 1")
    # Desde el firmware d8a8b66 la serie trae delante el minuto previo a
    # pulsar, con elapsedMs negativo. La simulación empieza al pulsar: contarlo
    # dejaría la cobertura en un tercio y ninguna comparación tendría veredicto.
    measured = [sample for sample in measured if int(sample["elapsedMs"]) >= 0]
    if not measured:
        raise ValueError("la preparación no tiene muestras medidas")
    if not simulated:
        raise ValueError("la simulación no tiene muestras")
    tolerances = {**DEFAULT_MODEL_TOLERANCES, **(model_tolerances or {})}
    measured_total = len(measured)
    results: list[dict[str, Any]] = []
    excluded = {"invalidQuality": 0, "outsideSimulatedRange": 0}
    compared_maximum = 0
    for channel in channels:
        points, channel_excluded = align(measured, simulated, channel, initial_conditions)
        for key, value in channel_excluded.items():
            excluded[key] = max(excluded[key], value)
        compared_maximum = max(compared_maximum, len(points))
        limitation = channel_limitation(
            channel, (channel_maturity or {}).get(channel), tolerances.get(channel)
        )
        entry = {
            "channel": channel,
            "unit": CHANNEL_UNITS.get(channel, ""),
            **channel_metrics(points, measured_total, tolerances.get(channel), minimum_coverage),
            "contributesToVerdict": limitation is None,
        }
        if limitation is not None:
            entry["modelLimitation"] = limitation
        results.append(entry)
    return {
        "schemaVersion": SCHEMA_VERSION,
        "comparisonId": f"comparison-{extraction_id}-{simulation_run_id}",
        "extractionId": extraction_id,
        "simulationRunId": simulation_run_id,
        "alignment": {
            "method": ALIGNMENT_METHOD,
            "measuredSampleCount": measured_total,
            "comparedSampleCount": compared_maximum,
            "coverageRatio": _round(compared_maximum / measured_total),
            "minimumCoverageRatio": minimum_coverage,
            "excluded": excluded,
            "initialConditions": dict(initial_conditions or {}),
        },
        "channels": results,
        "verdict": overall_verdict(results),
    }
