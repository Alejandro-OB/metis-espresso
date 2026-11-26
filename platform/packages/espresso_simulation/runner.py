"""Orquestación pura de una ejecución FMI."""

from __future__ import annotations

from collections.abc import Callable
from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from espresso_profiles.water_range import WATER_GAIN, boiler_reference_c

from .artifacts import FileSeriesStore, calculate_summary
from .board_controller import BOARD_REPLICA_ID, BoardReplicaController, boiler_setpoint_c
from .controller import ProfileTrackingController
from .fmi import FmuOutput, FmuPlantAdapter


class Plant(Protocol):
    """Planta ejecutable. El cuarto argumento de construcción son los parámetros
    de inicialización que fijan el estado de partida del modelo."""

    def __enter__(self) -> Plant: ...
    def __exit__(self, *_: object) -> None: ...
    def step(self, heater_command: float, pump_command: float) -> FmuOutput: ...


@dataclass(frozen=True)
class SimulationResult:
    artifact: dict
    summary: dict


def run_simulation(
    run_id: str,
    fmu_path: str | Path,
    duration_seconds: float,
    profile_snapshot: dict,
    series_store: FileSeriesStore,
    output_channels: tuple[str, ...] = ("temperatureC", "pressureBar"),
    step_seconds: float = 0.1,
    adapter_factory: Callable[..., Plant] = FmuPlantAdapter,
    progress: Callable[[int, int], None] | None = None,
    cancelled: Callable[[], bool] | None = None,
    controller_definition: dict | None = None,
    initial_conditions: dict | None = None,
    temperature_quantity: str = "water-temperature",
) -> SimulationResult:
    """Ejecuta una preparación virtual con el controlador que declara la definición.

    Sin definición, o con la de `profile-tracking`, el controlador de
    seguimiento de la planta virtual. Con la de `board-replica`, la réplica del
    control de la placa (`board_controller`), que arranca en el pulsado y por
    eso solo tiene sentido al replicar una preparación; la referencia de
    caldera es entonces la que fijaría la placa, con la subida del primer uso
    si la preparación declara haberla aplicado (`boilerOffsetAppliedC`).

    ``temperature_quantity`` es lo que el registro dice que mide el canal
    ``temperatureC`` del modelo. Desde la FMU v2 es el metal de la caldera,
    como en la placa, y el perfil pide agua: el controlador regula entonces
    sobre la referencia de caldera que fijaría la placa (recta vigente,
    `water_range.CURRENT`), y su banda
    de «lista» se lleva a metal con la misma pendiente.
    """
    total_steps = round(duration_seconds / step_seconds)
    if total_steps < 1 or abs(total_steps * step_seconds - duration_seconds) > 1e-9:
        raise ValueError("la duración debe ser un múltiplo del paso de comunicación")
    targets = profile_snapshot["targets"]
    tolerances = profile_snapshot["tolerances"]
    # El controlador de la planta virtual usa la tolerancia como umbral de
    # «listo para extraer». Sin tolerancia declarada espera a llegar al
    # objetivo, que es lo que hacía cuando la instantánea escribía un cero.
    ready_band_c = tolerances.get("temperatureC") or 0.0
    reference_c = targets["temperatureC"]
    if temperature_quantity == "boiler-body-temperature":
        reference_c = boiler_reference_c(reference_c)
        ready_band_c = ready_band_c / WATER_GAIN
    conditions = initial_conditions or {}
    channels_start = conditions.get("channels") or {}
    parameters = conditions.get("fmuParameters") or None
    # El controlador necesita un estado de partida para su primera decisión:
    # en modo réplica es el medido en la preparación, no un valor de arranque en frío.
    temperature = float(channels_start.get("temperatureC", 22.0))
    pressure = float(channels_start.get("pressureBar", 0.0))
    controller: ProfileTrackingController | BoardReplicaController
    if controller_definition is not None and (
        controller_definition["controllerId"] == BOARD_REPLICA_ID
    ):
        # La réplica de la placa regula el metal, como la placa: sin un canal
        # que sea el NTC del cuerpo no hay con qué cerrar su lazo térmico.
        if temperature_quantity != "boiler-body-temperature":
            raise ValueError("board-replica exige un modelo cuyo temperatureC sea la caldera")
        offset = conditions.get("boilerOffsetAppliedC")
        # La recta es la del café que se replica, si la declara: la de la
        # definición es la del acta con que se escribió la réplica, y desde la
        # v11 la placa convierte con otra. A null, no consta y no se replica.
        if "waterReference" in conditions:
            if not isinstance(conditions["waterReference"], dict):
                raise ValueError("la preparación no declara con qué recta convirtió el agua")
            controller_definition = deepcopy(controller_definition)
            controller_definition["configuration"]["temperature"]["waterReference"] = {
                key: conditions["waterReference"][key]
                for key in ("gain", "offsetC", "boilerMinimumC", "boilerMaximumC")
            }
        # La ganancia de la compensación durante la extracción también es la del
        # café: la subida del primer uso la cambia para esa sola preparación
        # (`note_first_use_offset`, extraction_runtime.c), y la de la definición
        # es la de uso continuo. Sin declararla, vale la del acta.
        gain = conditions.get("feedforwardGainApplied")
        if isinstance(gain, int | float) and not isinstance(gain, bool) and gain > 0:
            controller_definition = deepcopy(controller_definition)
            controller_definition["configuration"]["temperature"]["feedforward"]["gain"] = float(gain)
        reference_c = boiler_setpoint_c(
            controller_definition,
            targets["temperatureC"],
            float(offset) if isinstance(offset, int | float) else 0.0,
        )
        controller = BoardReplicaController.from_definition(
            reference_c,
            targets["pressureBar"],
            step_seconds,
            temperature,
            pressure,
            controller_definition,
            conditions.get("ambientC"),
        )
    elif controller_definition is not None:
        controller = ProfileTrackingController.from_definition(
            reference_c, targets["pressureBar"], ready_band_c, controller_definition
        )
    else:
        controller = ProfileTrackingController(reference_c, targets["pressureBar"], ready_band_c)
    samples: list[dict] = []
    with adapter_factory(fmu_path, step_seconds, output_channels, parameters) as plant:
        for index in range(1, total_steps + 1):
            if cancelled and cancelled():
                raise InterruptedError("simulación cancelada")
            heater, pump = controller.command(temperature, pressure)
            output = plant.step(heater, pump)
            temperature = output.temperature_c
            pressure = output.pressure_bar
            samples.append(
                {
                    "schemaVersion": "1.0",
                    "simulationRunId": run_id,
                    "sequence": index,
                    "simulationTimeSeconds": output.elapsed_ms / 1000,
                    "channels": output.channels,
                    "commands": {"heaterCommand": heater, "pumpCommand": pump},
                    "quality": "simulated",
                }
            )
            report_every = max(1, total_steps // 100)
            if progress and (index == total_steps or index % report_every == 0):
                progress(index, total_steps)
    artifact = series_store.write(run_id, samples, step_seconds)
    # El resumen juzga cada canal contra lo que de verdad regula: con el metal
    # simulado, contra la referencia de caldera y no contra el agua pedida.
    summary_targets = {**targets, "temperatureC": reference_c}
    summary_tolerances = dict(tolerances)
    if temperature_quantity == "boiler-body-temperature" and tolerances.get("temperatureC"):
        summary_tolerances["temperatureC"] = tolerances["temperatureC"] / WATER_GAIN
    summary = calculate_summary(
        run_id,
        samples,
        summary_targets,
        summary_tolerances,
        {"temperatureC": "Cel", "pressureBar": "bar"},
        artifact["artifactId"],
        step_seconds,
    )
    return SimulationResult(artifact, summary)
