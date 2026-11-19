from __future__ import annotations

import json
from dataclasses import replace
from pathlib import Path

import pytest
from espresso_simulation import FileSeriesStore, FmuOutput, ModelRegistry, run_simulation

ROOT = Path(__file__).resolve().parents[3]


class FakePlant:
    def __init__(self, _path, step_seconds, output_channels, parameters=None):
        self.step_seconds = step_seconds
        self.output_channels = output_channels
        self.parameters = parameters or {}
        self.time = 0.0
        self.temperature = 92.6

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return None

    def step(self, heater, pump):
        self.time += self.step_seconds
        self.temperature += heater * 0.1
        return FmuOutput(
            round(self.time * 1000),
            {"temperatureC": self.temperature, "pressureBar": pump * 9},
        )


def profile():
    return {
        "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
        "tolerances": {"temperatureC": 0.5, "pressureBar": 0.5},
    }


def test_registry_exposes_only_canonical_model():
    registry = ModelRegistry(ROOT / "models/modelica/model-registry-v1.json", ROOT)
    assert [item["id"] for item in registry.public_models()] == ["espresso-detailed"]
    with pytest.raises(PermissionError):
        registry.get("espresso-reduced-fixture", "1.0.0")
    registry.get("espresso-detailed", "1.0.0").verify()


def test_the_canonical_model_advances_at_the_board_pressure_period():
    """La réplica avanza el modelo al periodo del lazo de presión de la placa.

    Con 0,1 s el controlador replicado hacía cinco ciclos con la misma lectura y
    el integrador de la FMU daba 12–14 bar en el primer paso, contra un máximo
    medido de 7,8 bar en el primer segundo: la comparación salía fuera de
    tolerancia por una sola muestra (evidencias/catacion-20261104/
    arranque-presion-replica.md). A 0,02 s el controlador ve cada lectura nueva.
    """
    from espresso_simulation.board_controller import PRESSURE_PERIOD_S

    registry = ModelRegistry(ROOT / "models/modelica/model-registry-v1.json", ROOT)
    model = registry.canonical("espresso-detailed")
    contract = json.loads((ROOT / model.data["contract"]["path"]).read_text(encoding="utf-8"))
    step = model.data["capabilities"]["communicationStepSeconds"]
    assert step == pytest.approx(PRESSURE_PERIOD_S)
    assert contract["communicationStepSeconds"] == step


def test_simulation_writes_reproducible_extensible_series(tmp_path):
    store = FileSeriesStore(tmp_path)
    result = run_simulation(
        "sim-12345678",
        "unused.fmu",
        1.0,
        profile(),
        store,
        adapter_factory=FakePlant,
    )
    samples = store.read(result.artifact)
    assert len(samples) == 10
    assert samples[-1]["channels"]["temperatureC"] > 92.6
    assert samples[-1]["channels"]["pressureBar"] == pytest.approx(9.0)
    assert samples[-1]["quality"] == "simulated"
    assert result.summary["metrics"]["pressureBar"]["withinTolerancePercent"] > 0
    assert json.dumps(samples, allow_nan=False)


def test_duration_must_align_with_fmi_step(tmp_path):
    with pytest.raises(ValueError, match="múltiplo"):
        run_simulation(
            "sim-12345678",
            "unused.fmu",
            0.15,
            profile(),
            FileSeriesStore(tmp_path),
            adapter_factory=FakePlant,
        )


def test_a_model_that_simulates_the_metal_regulates_on_the_boiler_reference(tmp_path):
    """Con la FMU v2 el perfil pide agua y la planta virtual, como la placa,
    regula el metal sobre la referencia de caldera del acta v6."""
    water_profile = {
        "targets": {"temperatureC": 92.0, "pressureBar": 9.0},
        "tolerances": {"temperatureC": 0.5, "pressureBar": 0.5},
    }
    as_water = run_simulation(
        "sim-agua", "unused.fmu", 0.1, water_profile, FileSeriesStore(tmp_path / "a"),
        adapter_factory=FakePlant, initial_conditions={"channels": {"temperatureC": 95.0}},
    )
    as_metal = run_simulation(
        "sim-metal", "unused.fmu", 0.1, water_profile, FileSeriesStore(tmp_path / "b"),
        adapter_factory=FakePlant, initial_conditions={"channels": {"temperatureC": 95.0}},
        temperature_quantity="boiler-body-temperature",
    )
    first_water = FileSeriesStore(tmp_path / "a").read(as_water.artifact)[0]["commands"]
    first_metal = FileSeriesStore(tmp_path / "b").read(as_metal.artifact)[0]["commands"]
    # Con 95 °C leídos: leídos como agua ya pasan de los 92 pedidos y la bomba
    # arranca; leídos como metal faltan ~6,9 °C para los ~101,9 de caldera que
    # fija la placa para ese agua, y la máquina no está lista.
    assert first_water["pumpCommand"] > 0.0
    assert first_metal["pumpCommand"] == 0.0
    assert first_metal["heaterCommand"] > 0.0


def test_the_registry_picks_the_artifact_of_the_machine_it_runs_on():
    """Una FMU trae binarios nativos: en la Raspberry va la de aarch64."""
    registry = ModelRegistry(ROOT / "models/modelica/model-registry-v1.json", ROOT)
    model = registry.get("espresso-detailed", "2.0.0")
    declared = model.data["artifactsByArchitecture"]
    for machine in ("x86_64", "aarch64"):
        chosen = replace(model, machine=machine)
        assert chosen.artifact == declared[machine]
        chosen.verify()
    with pytest.raises(PermissionError):
        _ = replace(model, machine="riscv64").artifact
    # Un modelo sin artefactos por arquitectura sigue usando el suyo.
    legacy = replace(registry.get("espresso-detailed", "1.0.0"), machine="aarch64")
    assert legacy.artifact == legacy.data["artifact"]
