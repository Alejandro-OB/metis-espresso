"""Desviación de una propiedad frente a lo que pidió el perfil (RF-020).

RF-020 pide identificar las desviaciones y conservar sus valores, tolerancias,
instantes y procedencia. Este módulo decide la desviación de **una** propiedad;
quién la guarda y cuándo es la plataforma.

**Las series se juzgan en la ventana estabilizada** (decisión del 2026-10-10),
la misma de los indicadores de estabilidad: desde que la presión entra en su
rango. Antes bastaba una muestra de la subida inicial para declarar «no
conforme» la preparación, y una misma preparación salía estable con una regla y
no conforme con otra por los mismos segundos de arranque.

**Una tolerancia no declarada no se juzga.** Un objetivo sin margen no dice
cuánto apartarse es demasiado; tomarlo como cero declaraba fuera casi cualquier
lectura con un margen que nadie decidió. La propiedad queda «sin tolerancia
definida» y su valor observado se conserva.

**Qué no hace.** No marca ni corrige muestras: las no aptas llegan como `None` y
no cuentan. No decide la conformidad de la preparación entera ni explica la
causa de una desviación: dice dónde y cuánto se apartó, no por qué.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from enum import Enum
from typing import Any

CONFORMITY_RULE = "profile-conformity-v1"


class Status(str, Enum):
    # El cero es el estado conservador: nadie ha juzgado la propiedad.
    UNASSESSED = "unassessed"
    WITHIN = "within"
    OUTSIDE = "outside"
    #: No hay ninguna observación con la que juzgar.
    UNAVAILABLE = "unavailable"
    #: El perfil da un objetivo sin margen.
    NO_TOLERANCE = "no-tolerance"
    #: Hay observaciones, pero no el tramo en que deben juzgarse.
    NOT_EVALUABLE = "not-evaluable"


@dataclass(frozen=True)
class Bounds:
    """Lo que el perfil admite. `minimum`/`maximum` en `None` si no hay margen."""

    objective: dict[str, Any]
    minimum: float | None
    maximum: float | None

    @property
    def judgeable(self) -> bool:
        return self.minimum is not None and self.maximum is not None


def bounds(specification: Mapping[str, Any]) -> Bounds:
    if specification.get("strategy") == "range":
        low, high = float(specification["minimum"]), float(specification["maximum"])
        return Bounds({"minimum": low, "maximum": high}, low, high)
    target = float(specification["target"])
    tolerance = specification.get("tolerance")
    if tolerance is None:
        return Bounds(
            {"target": target, "tolerance": None, "minimum": None, "maximum": None},
            None,
            None,
        )
    tolerance = float(tolerance)
    low, high = target - tolerance, target + tolerance
    return Bounds({"target": target, "tolerance": tolerance, "minimum": low, "maximum": high},
                  low, high)


def value_deviation(value: float | None, limits: Bounds) -> dict[str, Any]:
    """Desviación de un valor único (dosis, TDS, tiempo…): no tiene instante."""
    observed = None if value is None else {"value": round(value, 3),
                                           "minimum": round(value, 3),
                                           "maximum": round(value, 3)}
    return {"observed": observed, "deviation": _judge([value] if value is not None else [],
                                                      limits, None)}


def series_deviation(
    samples: Iterable[Mapping[str, Any]],
    field: str,
    limits: Bounds,
    window_start_ms: int | None,
) -> dict[str, Any]:
    """Desviación de un canal de la serie a partir de `window_start_ms`."""
    ordered = sorted(
        (sample for sample in samples
         if sample.get(field) is not None and window_start_ms is not None
         and int(sample["elapsedMs"]) >= window_start_ms),
        key=lambda sample: int(sample["elapsedMs"]),
    )
    values = [float(sample[field]) for sample in ordered]
    observed = None
    if values:
        observed = {"value": round(sum(values) / len(values), 3),
                    "minimum": round(min(values), 3),
                    "maximum": round(max(values), 3)}
    return {"observed": observed, "deviation": _judge(values, limits, ordered, field)}


def not_evaluable(reason: str) -> dict[str, Any]:
    return {"status": Status.NOT_EVALUABLE.value, "reason": reason, "direction": None,
            "amount": None, "firstOutsideMs": None, "outsideDurationMs": None}


def _judge(
    values: list[float],
    limits: Bounds,
    ordered: list[Mapping[str, Any]] | None,
    field: str | None = None,
) -> dict[str, Any]:
    result: dict[str, Any] = {"status": Status.UNASSESSED.value, "reason": None,
                              "direction": None, "amount": None,
                              "firstOutsideMs": None, "outsideDurationMs": None}
    if not values:
        result["status"] = Status.UNAVAILABLE.value
        return result
    if not limits.judgeable:
        result["status"] = Status.NO_TOLERANCE.value
        return result
    low, high = limits.minimum, limits.maximum
    assert low is not None and high is not None
    below = max(0.0, low - min(values))
    above = max(0.0, max(values) - high)
    if not (below or above):
        result.update(status=Status.WITHIN.value, amount=0.0)
        return result
    result.update(
        status=Status.OUTSIDE.value,
        direction="both" if below and above else ("below" if below else "above"),
        amount=round(max(below, above), 3),
    )
    if ordered is not None and field is not None:
        result["firstOutsideMs"], result["outsideDurationMs"] = _outside_time(
            ordered, field, low, high
        )
    return result


def _outside_time(
    ordered: list[Mapping[str, Any]], field: str, low: float, high: float
) -> tuple[int | None, int]:
    """Primer instante fuera y tiempo total fuera.

    Cada muestra fuera cuenta hasta la siguiente; la última cuenta lo mismo que
    el intervalo anterior, para que una sola muestra final fuera no sume cero.
    """
    times = [int(sample["elapsedMs"]) for sample in ordered]
    first = None
    total = 0
    for index, sample in enumerate(ordered):
        value = float(sample[field])
        if low <= value <= high:
            continue
        if first is None:
            first = times[index]
        if index + 1 < len(times):
            total += times[index + 1] - times[index]
        elif index > 0:
            total += times[index] - times[index - 1]
    return first, total
