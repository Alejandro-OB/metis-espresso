"""¿Se le puede atribuir al ajuste lo que cambió en la taza? (8-nov-2026)

Un ajuste se comprueba con la preparación siguiente: si el defecto desaparece,
el razonador concluye que el ajuste funcionó (`materialize_sequences`). Esa
conclusión supone que entre las dos preparaciones solo cambió lo que el barista
ajustó. Si además cambió el agua sobre el café o la presión, la diferencia en la
taza puede venir de ahí y no del ajuste. Este módulo compara las dos
preparaciones **entre sí** —no cada una con su anterior, que en una sesión de
cata suele ser una pasada de agua— y dice qué condición cambió más que su banda.

**Las bandas son las de la monografía, no nuevas.**
- Agua: ±1 °C (`WATER_BAND_C`), la estabilidad térmica entre preparaciones,
  que sale del grado por encima o por debajo que el barista considera aceptable. Las dos aguas son
  estimadas desde el cuerpo al pulsar con la recta del metal de su acta; si las
  rectas son de actas distintas no se restan.
- Presión: ±0,18 bar (`PRESSURE_BAND_BAR`), la exactitud del transductor, sobre
  la presión media desde que entra en la banda de su referencia hasta el corte.

**Lo que el barista ajustó no se juzga.** Si ajustó la temperatura, que cambie
el agua es lo esperado; igual con la presión.

**La masa y la dosis se informan sin veredicto.** La masa es un resultado del
ajuste (el tiempo y la molienda la mueven a propósito), y para la dosis la
monografía no define una banda; inventarla afirmaría lo que no consta.

**Qué no hace.** No cambia la conclusión del razonador ni la clasificación del
barista: añade a su lado si el resultado es atribuible. Sin un dato, la
condición queda «sin datos» y el resultado «indeterminado», nunca atribuible:
el cero es el estado conservador.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from typing import Any

from espresso_stability.machine import AVERAGE_MS, PRESSURE_BAND_BAR, WATER_BAND_C

ATTRIBUTION_RULE = "adjustment-attribution-v1"

#: Propiedad ajustada (la del episodio) → condición que deja de juzgarse.
_ADJUSTED_CONDITION = {"water-temperature": "water", "pressure": "pressure"}


def regime_pressure_bar(
    samples: Iterable[Mapping[str, Any]], reference: float | None
) -> float | None:
    """Presión media desde el primer promedio de 1 s dentro de la banda hasta el corte.

    Es el mismo tramo que juzga la estabilidad de presión (`machine._pressure`).
    Sin referencia o si nunca entra en la banda, no hay tramo y no se inventa.
    """
    if reference is None:
        return None
    buckets: dict[int, list[float]] = {}
    for sample in samples:
        elapsed = int(sample["elapsedMs"])
        if elapsed < 0 or sample.get("pressureBar") is None:
            continue
        buckets.setdefault(elapsed // AVERAGE_MS, []).append(float(sample["pressureBar"]))
    averages = [sum(values) / len(values) for _, values in sorted(buckets.items())]
    start = next((i for i, value in enumerate(averages)
                  if abs(value - reference) <= PRESSURE_BAND_BAR), None)
    if start is None:
        return None
    tail = averages[start:]
    return round(sum(tail) / len(tail), 3)


def _check(name: str, initial: float | None, follow_up: float | None, band: float,
           skipped: bool, comparable: bool = True) -> dict[str, Any]:
    check: dict[str, Any] = {"condition": name, "initial": initial, "followUp": follow_up,
                             "band": band, "difference": None, "changed": None}
    if skipped:
        check["status"] = "adjusted"
        return check
    if initial is None or follow_up is None:
        check["status"] = "no-data"
        return check
    if not comparable:
        check["status"] = "line-changed"
        return check
    difference = round(follow_up - initial, 3)
    changed = abs(difference) > band
    check.update(difference=difference, changed=changed,
                 status="changed" if changed else "within")
    return check


def attribution(
    initial: Mapping[str, Any], follow_up: Mapping[str, Any], adjusted_property: str | None
) -> dict[str, Any]:
    """Si el resultado del ajuste se le puede atribuir, con cada condición comparada.

    `initial` y `follow_up` llevan `waterC`, `waterMethod`, `pressureBar`,
    `coffeeDoseG` y `beverageMassG`; cualquiera puede faltar.
    """
    skipped = _ADJUSTED_CONDITION.get(str(adjusted_property or ""))
    checks = [
        _check("water", initial.get("waterC"), follow_up.get("waterC"), WATER_BAND_C,
               skipped == "water",
               comparable=initial.get("waterMethod") == follow_up.get("waterMethod")),
        _check("pressure", initial.get("pressureBar"), follow_up.get("pressureBar"),
               PRESSURE_BAND_BAR, skipped == "pressure"),
    ]
    judged = [check for check in checks if check["status"] != "adjusted"]
    if any(check["status"] == "changed" for check in judged):
        status = "not-attributable"
    elif all(check["status"] == "within" for check in judged):
        status = "attributable"
    else:
        status = "undetermined"
    return {
        "rule": ATTRIBUTION_RULE,
        "status": status,
        "checks": checks,
        "reported": [
            {"condition": key, "initial": initial.get(key), "followUp": follow_up.get(key)}
            for key in ("coffeeDoseG", "beverageMassG")
        ],
    }
