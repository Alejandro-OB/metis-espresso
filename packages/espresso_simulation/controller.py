"""Controlador determinista destinado exclusivamente a la planta virtual."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass
class ProfileTrackingController:
    target_temperature_c: float
    target_pressure_bar: float
    temperature_tolerance_c: float
    full_power_band_c: float = 8.0
    medium_power_band_c: float = 2.0
    medium_action: float = 0.15
    near_action: float = 0.04
    extraction_boost: float = 0.8
    pump_pressure_at_full_command_bar: float = 9.0

    @classmethod
    def from_definition(
        cls,
        target_temperature_c: float,
        target_pressure_bar: float,
        temperature_tolerance_c: float,
        definition: dict[str, Any],
    ) -> ProfileTrackingController:
        configuration = definition["configuration"]
        return cls(
            target_temperature_c,
            target_pressure_bar,
            temperature_tolerance_c,
            full_power_band_c=configuration["fullPowerBandC"],
            medium_power_band_c=configuration["mediumPowerBandC"],
            medium_action=configuration["mediumAction"],
            near_action=configuration["nearAction"],
            extraction_boost=configuration["extractionBoost"],
            pump_pressure_at_full_command_bar=configuration[
                "pumpPressureAtFullCommandBar"
            ],
        )

    def command(self, temperature_c: float, pressure_bar: float) -> tuple[float, float]:
        del pressure_bar
        error = self.target_temperature_c - temperature_c
        if error >= self.full_power_band_c:
            thermal_action = 1.0
        elif error >= self.medium_power_band_c:
            thermal_action = self.medium_action
        elif error > 0:
            thermal_action = self.near_action
        else:
            thermal_action = 0.0
        ready = temperature_c >= self.target_temperature_c - self.temperature_tolerance_c
        pump = min(
            1.0,
            max(0.0, self.target_pressure_bar / self.pump_pressure_at_full_command_bar),
        )
        heater = min(1.0, thermal_action + (self.extraction_boost if ready else 0.0))
        return heater, pump if ready else 0.0


#: Definiciones versionadas de cada controlador, relativas a la raíz del
#: repositorio. Resolverlas por identificador y versión, y no con una ruta fija
#: en cada llamador, es lo que deja a la simulación libre con `profile-tracking`
#: y a la réplica de una preparación con `board-replica` sin que el trabajador
#: ejecute un controlador distinto del que se pidió.
CONTROLLER_DEFINITIONS: dict[tuple[str, str], str] = {
    ("profile-tracking", "1.0.0"): "models/modelica/controllers/profile-tracking-v1.json",
    ("board-replica", "1.0.0"): "models/modelica/controllers/board-replica-v1.json",
}


def load_registered_controller(
    root: str | Path, identifier: str, version: str
) -> dict[str, Any]:
    """La definición registrada para ese controlador, con su huella comprobada."""
    relative = CONTROLLER_DEFINITIONS.get((identifier, version))
    if relative is None:
        raise KeyError(f"controlador {identifier}@{version} no registrado")
    definition = load_controller_definition(Path(root) / relative)
    if (definition["controllerId"], definition["controllerVersion"]) != (identifier, version):
        raise ValueError(f"la definición de {relative} no es la de {identifier}@{version}")
    return definition


def load_controller_definition(path: str | Path) -> dict[str, Any]:
    definition = json.loads(Path(path).read_text(encoding="utf-8"))
    configuration = json.dumps(
        definition["configuration"], sort_keys=True, separators=(",", ":")
    ).encode()
    if hashlib.sha256(configuration).hexdigest() != definition["configurationSha256"]:
        raise ValueError("checksum inválido para la configuración del controlador")
    return definition
