"""Núcleo compartido de simulación FMI para el sistema EKO."""

from .artifacts import FileSeriesStore, calculate_summary
from .comparison import align, compare, interpolate
from .board_controller import BoardReplicaController
from .controller import (
    ProfileTrackingController,
    load_controller_definition,
    load_registered_controller,
)
from .fmi import FmuOutput, FmuPlantAdapter
from .registry import ModelRegistry, RegisteredModel
from .runner import SimulationResult, run_simulation

__all__ = [
    "BoardReplicaController",
    "FileSeriesStore",
    "align",
    "compare",
    "interpolate",
    "FmuOutput",
    "FmuPlantAdapter",
    "ModelRegistry",
    "ProfileTrackingController",
    "RegisteredModel",
    "SimulationResult",
    "calculate_summary",
    "run_simulation",
    "load_controller_definition",
    "load_registered_controller",
]
