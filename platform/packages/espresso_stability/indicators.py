"""Indicadores de estabilidad térmica y de presión de una preparación.

Los indicadores de estabilidad se evalúan con tolerancias definidas. Tres
decisiones del 2026-10-10 fijan cómo:

**La ventana empieza cuando la presión entra por primera vez en su rango** y
llega hasta el final de la preparación. Antes de eso la presión todavía sube, y
la desviación de esos segundos es esperada: contarla castigaba a una máquina
que no había fallado. Se descartó ignorar un número fijo de segundos porque esa
cifra no sale de ninguna medida y no sirve igual para un perfil con
preinfusión que para uno sin ella. Si la presión nunca entra, la preparación
«no llegó a estabilizarse» y no recibe nota: un promedio sobre una presión que
nunca llegó no describe estabilidad, describe el fallo.

**La tolerancia es la del perfil.** El porcentaje dentro de tolerancia responde
si la máquina se mantuvo donde el barista pidió. Si el perfil no la declara, el
porcentaje no se calcula: rellenarla con una cifra plausible juzgaría la máquina
con un margen que nadie decidió. El error medio sí se calcula, porque solo
necesita el valor pedido, y es el que sirve para comparar perfiles distintos.

**La temperatura que se juzga es la de la caldera** frente a la que se le pidió
a la caldera, y así se declara en `temperatureSite`. La del agua exige la
transformación de ADR-019, que no existe hasta la etapa 8; cuando exista será
un indicador más, no un reemplazo de este.

**Qué no hace.** No marca ni corrige muestras: las no aptas llegan como `None` y
simplemente no cuentan. No decide si una preparación fue buena; mide cuánto se
apartó. El firmware calcula los mismos indicadores con la misma regla
(`extraction_session.c`); los casos de sus pruebas reproducen los de aquí.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from enum import Enum
from typing import Any

#: Versión de la regla de la ventana. Se archiva con cada indicador para que un
#: cambio futuro de regla no haga parecer comparables cifras que no lo son.
WINDOW_RULE = "pressure-first-within-tolerance-v1"

#: Dónde se mide la temperatura que se juzga.
TEMPERATURE_SITE_BOILER = "boiler"


class WindowStatus(str, Enum):
    # El cero es el estado conservador: nadie ha determinado la ventana.
    UNDETERMINED = "undetermined"
    STABILIZED = "stabilized"
    NEVER_STABILIZED = "never-stabilized"
    #: El perfil no da un rango de presión: sin rango no hay instante de entrada.
    NO_PRESSURE_RANGE = "no-pressure-range"


@dataclass(frozen=True)
class Requested:
    """Lo que el perfil pidió para un canal. `None` es «no declarado»."""

    target: float | None
    tolerance: float | None = None


def stability(
    samples: Iterable[Mapping[str, Any]],
    temperature: Requested,
    pressure: Requested,
) -> dict[str, Any]:
    """Indicadores de estabilidad sobre la ventana estabilizada.

    Cada muestra lleva `elapsedMs`, `temperatureC` y `pressureBar`; un canal en
    `None` es una medida no apta o ausente.
    """
    # El minuto previo a pulsar (elapsedMs negativo) no es la preparación: lo
    # juzga `machine` con su propia banda, no estos indicadores.
    ordered = sorted(
        (sample for sample in samples if int(sample["elapsedMs"]) >= 0),
        key=lambda sample: int(sample["elapsedMs"]),
    )
    status, start_ms = _window(ordered, pressure)
    window = [
        sample
        for sample in ordered
        if start_ms is not None and int(sample["elapsedMs"]) >= start_ms
    ]
    return {
        "window": {"rule": WINDOW_RULE, "status": status.value, "startMs": start_ms},
        "temperatureSite": TEMPERATURE_SITE_BOILER,
        "temperatureMaeC": _mean_error(window, "temperatureC", temperature),
        "pressureMaeBar": _mean_error(window, "pressureBar", pressure),
        "temperatureWithinTolerancePercent": _within_percent(window, "temperatureC", temperature),
        "pressureWithinTolerancePercent": _within_percent(window, "pressureBar", pressure),
    }


def _window(
    samples: list[Mapping[str, Any]], pressure: Requested
) -> tuple[WindowStatus, int | None]:
    if pressure.target is None or pressure.tolerance is None:
        return WindowStatus.NO_PRESSURE_RANGE, None
    for sample in samples:
        value = sample.get("pressureBar")
        if value is not None and abs(float(value) - pressure.target) <= pressure.tolerance:
            return WindowStatus.STABILIZED, int(sample["elapsedMs"])
    return WindowStatus.NEVER_STABILIZED, None


def _values(window: list[Mapping[str, Any]], field: str) -> list[float]:
    return [float(sample[field]) for sample in window if sample.get(field) is not None]


def _mean_error(window: list[Mapping[str, Any]], field: str, requested: Requested) -> float | None:
    values = _values(window, field)
    if requested.target is None or not values:
        return None
    return round(sum(abs(value - requested.target) for value in values) / len(values), 3)


def _within_percent(
    window: list[Mapping[str, Any]], field: str, requested: Requested
) -> float | None:
    values = _values(window, field)
    if requested.target is None or requested.tolerance is None or not values:
        return None
    inside = sum(abs(value - requested.target) <= requested.tolerance for value in values)
    return round(100 * inside / len(values), 1)
