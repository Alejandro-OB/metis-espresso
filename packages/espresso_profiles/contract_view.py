"""Traducción de una configuración a la forma del contrato del dispositivo."""

from __future__ import annotations

from typing import Any

# Cada magnitud del contrato, con el nombre que recibe en cada una de las tres
# formas en que el sistema describe una configuración.
_MAGNITUDES = {
    "temperature": {
        "setpoint": "temperatureSetpointC",
        "tolerance": "temperatureToleranceC",
        "grouped": "temperatureC",
        "properties": {"temperature", "water-temperature"},
    },
    "pressure": {
        "setpoint": "pressureSetpointBar",
        "tolerance": "pressureToleranceBar",
        "grouped": "pressureBar",
        "properties": {"pressure", "extraction-pressure"},
    },
}


def profile_reference(
    profile: dict[str, Any], magnitude: str
) -> tuple[float | None, float | None]:
    """Referencia y tolerancia de una magnitud, buscándolas en las tres formas.

    Devuelve ``(None, None)`` cuando la configuración no declara esa magnitud:
    una referencia ausente no se sustituye por un valor plausible, porque
    gobernaría una máquina real.
    """
    names = _MAGNITUDES.get(magnitude)
    if names is None:
        raise ValueError(f"magnitud desconocida: {magnitude}")

    if profile.get(names["setpoint"]) is not None:
        tolerance = profile.get(names["tolerance"])
        return float(profile[names["setpoint"]]), (
            None if tolerance is None else float(tolerance)
        )

    targets = profile.get("targets") or {}
    tolerances = profile.get("tolerances") or {}
    if targets.get(names["grouped"]) is not None:
        tolerance = tolerances.get(names["grouped"])
        return float(targets[names["grouped"]]), (
            None if tolerance is None else float(tolerance)
        )

    for specification in profile.get("specifications") or []:
        if specification.get("property") not in names["properties"]:
            continue
        if specification.get("strategy") == "target":
            target = specification.get("target")
            tolerance = specification.get("tolerance")
            return (
                None if target is None else float(target),
                None if tolerance is None else float(tolerance),
            )
        minimum = specification.get("minimum")
        maximum = specification.get("maximum")
        if minimum is not None and maximum is not None:
            # Un intervalo declara los extremos y no su centro: la referencia es
            # el punto medio y la tolerancia, la mitad de su anchura.
            low, high = float(minimum), float(maximum)
            return (low + high) / 2, (high - low) / 2
    return None, None


def profile_maximum_time_ms(profile: dict[str, Any]) -> int | None:
    """Tiempo máximo declarado, en milisegundos, si la configuración lo trae."""
    declared = profile.get("maximumTimeMs")
    if declared is not None:
        return int(declared)
    for specification in profile.get("specifications") or []:
        if specification.get("property") not in {"extraction-time", "time"}:
            continue
        maximum = specification.get("maximum")
        if maximum is None:
            continue
        # El catálogo declara el tiempo en segundos; el contrato, en milisegundos.
        unit = str(specification.get("unit") or "").lower()
        seconds = float(maximum)
        return int(seconds * 1000) if unit in {"s", "seg", "segundos"} else int(seconds)
    return None


def profile_contract_view(profile: dict[str, Any]) -> dict[str, Any]:
    """Configuración en la forma que el dispositivo sabe leer.

    Devuelve solo los campos del contrato. Las magnitudes que la configuración
    no declare quedan ausentes, de modo que quien la valide pueda rechazarla en
    lugar de recibir una referencia inventada.
    """
    temperature, temperature_tolerance = profile_reference(profile, "temperature")
    pressure, pressure_tolerance = profile_reference(profile, "pressure")
    maximum_time = profile_maximum_time_ms(profile)

    view: dict[str, Any] = {
        "id": profile.get("id"),
        "name": profile.get("name"),
        "definedBy": profile.get("definedBy")
        or (profile.get("provenance") or {}).get("actor"),
        "version": profile.get("version"),
        "status": profile.get("status"),
    }
    if temperature is not None:
        view["temperatureSetpointC"] = temperature
    if temperature_tolerance is not None:
        view["temperatureToleranceC"] = temperature_tolerance
    if pressure is not None:
        view["pressureSetpointBar"] = pressure
    if pressure_tolerance is not None:
        view["pressureToleranceBar"] = pressure_tolerance
    if maximum_time is not None:
        view["maximumTimeMs"] = maximum_time
    if profile.get("expectedBeverageMassG") is not None:
        view["expectedBeverageMassG"] = float(profile["expectedBeverageMassG"])
    # Un perfil anterior al campo no lo declara: se omite y la placa conserva el
    # corte con que se compiló. Enviar null o un 10000 de relleno afirmaría una
    # decisión que ese perfil no tomó, y el firmware rechaza el null.
    if profile.get("purgeMaximumTimeMs") is not None:
        view["purgeMaximumTimeMs"] = int(profile["purgeMaximumTimeMs"])
    # La subida de caldera del primer uso, tal cual. Sin declarar no se envía:
    # un 0 de relleno sería una petición que nadie hizo, y la placa trata la
    # ausencia como «sin subida».
    if profile.get("boilerOffsetC") is not None:
        view["boilerOffsetC"] = float(profile["boilerOffsetC"])
    return view
