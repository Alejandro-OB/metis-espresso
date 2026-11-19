"""Cuánto varía una serie de preparaciones, magnitud por magnitud (8-nov-2026).

La monografía juzga la repetibilidad del perfil final con la masa en taza, el
tiempo y el agua estimada de sus tres preparaciones, y esas cifras salían de un
cálculo a mano sobre los registros. La plataforma ya comparaba cada preparación
con la anterior (`machine.water_between_preparations`); esto resume la serie que
el barista elige comparar: cuántas tienen el dato, el mínimo, el máximo, el
rango y la desviación estándar muestral.

**El rango va primero.** Con dos a cuatro preparaciones, la desviación estándar
casi no dice nada y el rango es lo que el barista puede contrastar con una banda
(±0,5 °C en el agua). La desviación se da igual, porque es la cifra que se
compara con la literatura, pero solo con tres o más valores.

**Qué no hace.** No rellena un dato que falta ni descarta un valor raro: una
preparación sin masa anotada no cuenta y se dice cuántas contaron. No decide si
las preparaciones son comparables; eso lo declara quien llama.
"""

from __future__ import annotations

from collections.abc import Sequence
from statistics import fmean, stdev
from typing import Any


def spread(values: Sequence[float | None], digits: int = 2) -> dict[str, Any]:
    """Resumen de una magnitud en una serie de preparaciones."""
    present = [float(value) for value in values if value is not None]
    result: dict[str, Any] = {
        "count": len(present),
        "missing": len(values) - len(present),
        "minimum": None,
        "maximum": None,
        "range": None,
        "mean": None,
        "standardDeviation": None,
    }
    if len(present) < 2:
        return result
    low, high = min(present), max(present)
    result.update(
        minimum=round(low, digits),
        maximum=round(high, digits),
        range=round(high - low, digits),
        mean=round(fmean(present), digits),
    )
    if len(present) >= 3:
        result["standardDeviation"] = round(stdev(present), digits)
    return result
