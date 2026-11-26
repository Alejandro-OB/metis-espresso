"""Modelo Euler verificable que refleja las ecuaciones Modelica del prototipo."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class PlantOutput:
    time_seconds: float
    temperature_c: float
    pressure_bar: float


class ReferencePlant:
    """Oráculo de pruebas; no sustituye la FMU exportada."""

    def __init__(self, ambient_temperature_c: float = 22.0):
        self.ambient_temperature_c = ambient_temperature_c
        self.temperature_c = ambient_temperature_c
        self.pressure_bar = 0.0
        self.time_seconds = 0.0
        self.heater_rate_c_per_second = 8.0
        self.thermal_loss_per_second = 0.04
        self.maximum_pump_pressure_bar = 12.0
        self.pressure_time_constant_seconds = 0.4

    def step(self, heater_command: float, pump_command: float, step_seconds: float = 0.1) -> PlantOutput:
        for name, value in (("heater_command", heater_command), ("pump_command", pump_command)):
            if not 0.0 <= value <= 1.0:
                raise ValueError(f"{name} debe estar entre 0 y 1")
        if step_seconds <= 0.0 or step_seconds > self.pressure_time_constant_seconds:
            raise ValueError("step_seconds debe ser positivo y no mayor a 0.4 s")

        thermal_input = self.heater_rate_c_per_second * heater_command
        thermal_loss = self.thermal_loss_per_second * (
            self.temperature_c - self.ambient_temperature_c
        )
        self.temperature_c += (thermal_input - thermal_loss) * step_seconds
        pressure_target = self.maximum_pump_pressure_bar * pump_command
        self.pressure_bar += (
            (pressure_target - self.pressure_bar)
            * step_seconds
            / self.pressure_time_constant_seconds
        )
        self.time_seconds += step_seconds
        return PlantOutput(self.time_seconds, self.temperature_c, self.pressure_bar)
