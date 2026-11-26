"""Tolerancia de un perfil a partir de cafés repetidos con él.

La receta de la catación es el perfil del barista con la tolerancia que dan sus
propias repeticiones (protocolo de catación, paso 2): el razonador juzga «dentro
de la receta» contra esa banda, y una banda supuesta haría que un café igual a
los del barista saliera fuera, o que uno distinto quedara dentro.

**El criterio.** La tolerancia de cada magnitud es la mayor diferencia entre los
cafés y el objetivo que declaró el barista, de modo que todos sus cafés queden
dentro. Se descartó la desviación típica: con tres cafés es poco fiable, y un
múltiplo de ella sería un número elegido sin respaldo. Si la mayor diferencia
es cero, la tolerancia es la resolución con que se anotaron los valores (la
última cifra que traen), porque una banda nula dejaría fuera la misma medida
repetida. Si el perfil no declara la magnitud, como suele pasar con el TDS, el
objetivo es la media de los cafés y la tolerancia se mide desde ella.

**Qué no hace.** No toca una magnitud declarada como intervalo: en el tiempo,
el máximo del intervalo es el corte de la placa, y cambiarlo cambiaría cuándo
se detiene la bomba, no solo cómo se juzga el café; de ella solo se informa
cuánto variaron los cafés. Tampoco rellena: si a un
café le falta el valor, esa magnitud queda como estaba y se dice por qué.
"""

from __future__ import annotations

import math
from typing import Any

# Las magnitudes que fija la repetición de la receta, con la unidad del catálogo.
DISPERSION_PROPERTIES: dict[str, str] = {
    "coffee-dose": "g",
    "beverage-mass": "g",
    "extraction-time": "s",
    "tds": "%",
}


def _decimals(value: float) -> int:
    text = f"{value:.6f}".rstrip("0").rstrip(".")
    return len(text.split(".")[1]) if "." in text else 0


def _round_up(value: float, decimals: int) -> float:
    """Redondea hacia arriba, para que ningún café quede fuera por redondeo."""
    scale = 10**decimals
    return math.ceil(round(value * scale, 6)) / scale


def tolerance_from_repetitions(
    specifications: list[dict[str, Any]],
    values: dict[str, list[float | None]],
) -> dict[str, Any]:
    """Las especificaciones con la tolerancia de las repeticiones, y el detalle.

    `values` da, por magnitud, el valor de cada café en el mismo orden; `None`
    es un valor que no se anotó.
    """
    result = [dict(item) for item in specifications]
    by_property = {item.get("property"): item for item in result}
    details: list[dict[str, Any]] = []
    for prop, unit in DISPERSION_PROPERTIES.items():
        observed = values.get(prop) or []
        detail: dict[str, Any] = {"property": prop, "unit": unit, "values": observed}
        details.append(detail)
        if not observed or any(value is None for value in observed):
            detail["status"] = "missing"
            continue
        numbers = [float(value) for value in observed if value is not None]
        decimals = max(_decimals(value) for value in numbers)
        resolution = 10**-decimals
        specification = by_property.get(prop)
        if specification is not None and specification.get("strategy") != "target":
            # Se informa cuánto variaron, para quien tenga que mover el corte a
            # mano, pero el intervalo no se toca.
            mean = round(sum(numbers) / len(numbers), decimals)
            spread = max(_round_up(max(abs(value - mean) for value in numbers), decimals),
                         resolution)
            detail.update(status="range", mean=mean, spread=round(spread, decimals))
            continue
        if specification is not None and specification.get("target") is not None:
            objective = float(specification["target"])
            status = "updated"
        else:
            objective = round(sum(numbers) / len(numbers), decimals)
            status = "added"
        deviation = max(abs(value - objective) for value in numbers)
        tolerance = max(_round_up(deviation, decimals), resolution)
        tolerance = round(tolerance, decimals)
        detail.update(objective=objective, tolerance=tolerance, status=status)
        if specification is None:
            specification = {"property": prop, "unit": unit, "strategy": "target"}
            result.append(specification)
        specification.update(target=objective, tolerance=tolerance)
        specification.pop("minimum", None)
        specification.pop("maximum", None)
    return {"specifications": result, "details": details}
