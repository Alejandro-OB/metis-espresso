"""Adaptador FMI 2.0 sin dependencias de la aplicación web ni del hardware."""

from __future__ import annotations

import math
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory


@dataclass(frozen=True)
class FmuOutput:
    elapsed_ms: int
    channels: dict[str, float]

    @property
    def source(self) -> str:
        return "simulation"

    @property
    def temperature_c(self) -> float:
        return self.channels["temperatureC"]

    @property
    def pressure_bar(self) -> float:
        return self.channels["pressureBar"]


class FmuPlantAdapter:
    """Ejecuta una FMU registrada y expone únicamente sus canales declarados."""

    def __init__(
        self,
        fmu_path: str | Path,
        step_seconds: float = 0.1,
        output_channels: tuple[str, ...] = ("temperatureC", "pressureBar"),
        parameters: dict[str, float] | None = None,
    ) -> None:
        if step_seconds <= 0:
            raise ValueError("step_seconds debe ser positivo")
        try:
            from fmpy import extract, read_model_description
            from fmpy.fmi2 import FMU2Slave
        except ImportError as error:
            raise RuntimeError(
                "Se requiere el paquete opcional 'fmpy' para ejecutar la FMU"
            ) from error

        self._temporary = TemporaryDirectory(prefix="eko-fmu-")
        self._step_seconds = step_seconds
        self._time = 0.0
        self._output_channels = output_channels
        self._description = read_model_description(str(fmu_path))
        unzip_directory = extract(str(fmu_path), unzipdir=self._temporary.name)
        co_simulation = self._description.coSimulation
        if co_simulation is None:
            raise ValueError("la FMU no expone co-simulación FMI 2.0")
        self._references = {
            variable.name: variable.valueReference for variable in self._description.modelVariables
        }
        required = {"heaterCommand", "pumpCommand", *output_channels}
        missing = required.difference(self._references)
        if missing:
            raise ValueError(f"faltan variables FMI: {', '.join(sorted(missing))}")
        self._fmu = FMU2Slave(
            guid=self._description.guid,
            unzipDirectory=unzip_directory,
            modelIdentifier=co_simulation.modelIdentifier,
            instanceName="espresso-digital-twin-simulation",
        )
        self._fmu.instantiate()
        self._fmu.setupExperiment(startTime=0.0)
        self._applied_parameters = self._apply_parameters(parameters)
        self._fmu.enterInitializationMode()
        self._fmu.exitInitializationMode()

    def _apply_parameters(self, parameters: dict[str, float] | None) -> dict[str, float]:
        """Fija parámetros de inicialización en modo Instantiated, antes de inicializar.

        Solo admite nombres declarados por la FMU: un parámetro desconocido se
        rechaza en lugar de ignorarse en silencio, para que una condición inicial
        mal escrita no produzca una simulación aparentemente válida.
        """
        if not parameters:
            return {}
        unknown = sorted(set(parameters).difference(self._references))
        if unknown:
            raise ValueError(f"parámetros no declarados por la FMU: {', '.join(unknown)}")
        applied = {name: float(value) for name, value in sorted(parameters.items())}
        self._fmu.setReal(
            [self._references[name] for name in applied],
            list(applied.values()),
        )
        return applied

    @property
    def applied_parameters(self) -> dict[str, float]:
        """Parámetros efectivamente escritos en la FMU, para conservarlos como procedencia."""
        return dict(self._applied_parameters)

    def step(self, heater_command: float, pump_command: float) -> FmuOutput:
        for name, value in (("heaterCommand", heater_command), ("pumpCommand", pump_command)):
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} debe estar entre 0 y 1")
        self._fmu.setReal(
            [self._references["heaterCommand"], self._references["pumpCommand"]],
            [heater_command, pump_command],
        )
        self._fmu.doStep(
            currentCommunicationPoint=self._time,
            communicationStepSize=self._step_seconds,
        )
        self._time += self._step_seconds
        values = self._fmu.getReal([self._references[name] for name in self._output_channels])
        channels = dict(zip(self._output_channels, values, strict=True))
        if not all(math.isfinite(value) for value in channels.values()):
            raise ArithmeticError("la FMU produjo un valor no finito")
        return FmuOutput(round(self._time * 1000), channels)

    def close(self) -> None:
        if getattr(self, "_fmu", None) is not None:
            self._fmu.terminate()
            self._fmu.freeInstance()
            self._fmu = None
        self._temporary.cleanup()

    def __enter__(self) -> FmuPlantAdapter:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
