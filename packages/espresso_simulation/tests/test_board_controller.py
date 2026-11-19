"""Propiedades de la réplica del control de la placa (`board-replica`).

Las plantas de estas pruebas son de prueba: primer orden con los números del
acta para la temperatura, y para la presión una recta con ordenada propia,
porque la ganancia del acta es incremental (entre mesetas altas) y sola no
llega a ninguna referencia útil. No pretenden ser la cafetera; solo dan algo
contra lo que cerrar el lazo.
"""

from __future__ import annotations

import hashlib
import json
import math
import random
from pathlib import Path

import pytest
from espresso_profiles import water_range
from espresso_simulation import (
    BoardReplicaController,
    FileSeriesStore,
    FmuOutput,
    load_registered_controller,
    run_simulation,
)
from espresso_simulation.board_controller import (
    PRESSURE_PERIOD_S,
    THERMAL_DECIMATION,
    boiler_setpoint_c,
    simc_gains,
)

ROOT = Path(__file__).resolve().parents[3]
ACTAS = ROOT / "device/docs/engineering/actas"
STEP = 0.1
THERMAL_PERIOD = PRESSURE_PERIOD_S * THERMAL_DECIMATION


def definition() -> dict:
    return load_registered_controller(ROOT, "board-replica", "1.0.0")


def controller(temperature=101.9, pressure=0.0, setpoint=101.9, pressure_setpoint=8.5, **kw):
    return BoardReplicaController.from_definition(
        setpoint, pressure_setpoint, STEP, temperature, pressure, definition(), **kw
    )


@pytest.mark.parametrize(
    ("loop", "file", "fields"),
    [
        ("temperature", "acta-temperatura-20261019.json",
         ("plant", "tuning", "feedforward", "water_reference")),
        ("pressure", "acta-presion-20261012.json", ("plant", "tuning", "preinfusion")),
    ],
)
def test_every_number_comes_from_its_acta(loop, file, fields):
    """Cada parámetro de la definición es el del acta que cita, con su huella."""
    path = ACTAS / file
    acta = json.loads(path.read_text(encoding="utf-8"))
    declared = definition()["configuration"][loop]
    assert declared["actaSha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
    assert declared["identifier"] == f"{acta['acta']}-v{acta['version']}"
    names = {
        "gain": "gain", "time_constant_s": "timeConstantS", "dead_time_s": "deadTimeS",
        "lambda_s": "lambdaS", "filter_time_constant_s": "filterTimeConstantS",
        "setpoint_rate": "setpointRate", "output_maximum": "outputMaximum",
        "margin_bar": "marginBar", "integral_start_percent": "integralStartPercent",
        "offset_c": "offsetC", "boiler_minimum_c": "boilerMinimumC",
        "boiler_maximum_c": "boilerMaximumC",
    }
    sections = {"plant": "plant", "tuning": "tuning", "feedforward": "feedforward",
                "water_reference": "waterReference", "preinfusion": "preinfusion"}
    for section in fields:
        for key, value in acta[section].items():
            if isinstance(value, int | float):
                assert declared[sections[section]][names.get(key, key)] == value, (section, key)


def test_water_reference_is_the_platform_v6_line():
    """La definición cita el acta v7, que trae la recta v6: la misma que la plataforma."""
    relation = definition()["configuration"]["temperature"]["waterReference"]
    v6 = water_range.LINE_V6
    assert relation["gain"] == v6.gain
    assert relation["offsetC"] == v6.offset_c
    assert boiler_setpoint_c(definition(), 92.0) == pytest.approx(v6.boiler_reference_c(92.0))
    assert boiler_setpoint_c(definition(), 92.0, 3.0) == pytest.approx(
        v6.boiler_reference_c(92.0) + 3.0
    )
    with pytest.raises(ValueError):
        boiler_setpoint_c(definition(), 95.0)


def test_thermal_gains_match_the_independent_replica():
    """La réplica de la 7.6 (`lazo16.py`) usa Kp 0,716 y Ti 356 s con esta planta."""
    thermal = definition()["configuration"]["temperature"]
    kp, ki = simc_gains(
        thermal["plant"]["gain"], thermal["plant"]["timeConstantS"],
        thermal["plant"]["deadTimeS"], thermal["tuning"]["lambdaS"], THERMAL_PERIOD,
    )
    assert kp == pytest.approx(0.716, abs=5e-4)
    assert kp / ki == pytest.approx(356.0)


class ThermalPlant:
    """Primer orden con retardo del acta, en régimen al empezar."""

    def __init__(self, temperature_c, ambient_c):
        plant = definition()["configuration"]["temperature"]["plant"]
        self.gain, self.tau = plant["gain"], plant["timeConstantS"]
        self.ambient = ambient_c
        self.value = temperature_c
        holding = (temperature_c - ambient_c) / self.gain
        self.delay = [holding] * round(plant["deadTimeS"] / STEP)

    def step(self, demand_percent):
        self.delay.append(demand_percent)
        applied = self.delay.pop(0)
        target = self.ambient + self.gain * applied
        self.value += (target - self.value) * STEP / self.tau
        return self.value


def test_without_pump_the_thermal_loop_sustains_the_reference():
    """En régimen y sin bomba, el PI sostiene el metal en la referencia.

    Con la densidad en cero la prealimentación no aporta nada y queda solo el
    PI, arrancado con la potencia de sostenimiento que declara la réplica. La
    demanda va en puntos enteros, como en la placa, y un punto son 32 °C en
    régimen: el lazo oscila alrededor de la referencia en vez de quedarse en
    ella, y lo que se exige es que la media sea la referencia y la oscilación
    quepa en la banda de «lista».
    """
    reference, ambient = 101.9, 23.7
    board = controller(temperature=reference, setpoint=reference, ambient_c=ambient)
    plant = ThermalPlant(reference, ambient)
    temperature = reference
    temperatures, demands = [], []
    for _ in range(round(1800 / THERMAL_PERIOD)):
        demand = board._thermal_tick(temperature, density=0)
        assert 0 <= demand <= 10
        demands.append(demand)
        for _ in range(round(THERMAL_PERIOD / STEP)):
            temperature = plant.step(demand)
        temperatures.append(temperature)
    assert sum(temperatures) / len(temperatures) == pytest.approx(reference, abs=0.1)
    assert max(abs(value - reference) for value in temperatures) < 0.5
    holding = (reference - ambient) / plant.gain
    assert sum(demands) / len(demands) == pytest.approx(holding, abs=0.1)


def test_feedforward_acts_only_with_pump_and_stops_without_water():
    still = controller()
    pumping = controller()
    without = still._thermal_tick(101.9, density=0)
    with_pump = pumping._thermal_tick(101.9, density=75)
    gain = definition()["configuration"]["temperature"]["feedforward"]["gain"]
    assert with_pump - without == pytest.approx(gain * 75, abs=1)
    # El metal sube con la bomba en marcha: no entra agua, y la aportación se corta.
    for _ in range(5):
        dry = pumping._thermal_tick(103.0, density=75)
    assert dry <= 10


def test_pi_output_never_exceeds_its_own_ceiling_without_pump():
    board = controller(temperature=60.0, setpoint=101.9)
    ceiling = definition()["configuration"]["temperature"]["tuning"]["outputMaximum"]
    assert all(board._thermal_tick(60.0, density=0) <= ceiling for _ in range(200))


class PressurePlant:
    """Recta de prueba (ordenada propia, pendiente del acta) con primer orden y retardo."""

    def __init__(self, start_bar):
        plant = definition()["configuration"]["pressure"]["plant"]
        self.gain, self.tau = plant["gain"], plant["timeConstantS"]
        self.value = start_bar
        self.delay = [0.0] * round(plant["deadTimeS"] / STEP)

    def step(self, density_percent):
        self.delay.append(density_percent)
        applied = self.delay.pop(0)
        target = 6.0 + self.gain * applied
        self.value += (target - self.value) * (1 - math.exp(-STEP / self.tau))
        return self.value


def test_pressure_reaches_the_reference_and_respects_the_ceiling():
    board = controller(pressure=0.0)
    plant = PressurePlant(0.0)
    ceiling = definition()["configuration"]["pressure"]["tuning"]["outputMaximum"] / 100
    temperature, pressure = 101.9, 0.0
    pumps = []
    for _ in range(round(20 / STEP)):
        heater, pump = board.command(temperature, pressure)
        assert 0 <= heater <= 1
        assert 0 <= pump <= ceiling + 1e-12
        pumps.append(pump)
        pressure = plant.step(pump * 100)
    # Mojando, la bomba va al techo; regulando, por debajo de él.
    assert pumps[0] == pytest.approx(ceiling)
    assert board.preinfusion.regulating
    assert pressure == pytest.approx(8.5, abs=0.2)
    assert pumps[-1] < ceiling


def test_without_closing_the_puck_the_pump_stays_at_the_ceiling():
    """La preparación real del 2026-10-20: la presión no pasa de la meseta de 7 bar."""
    board = controller(pressure=4.9)
    ceiling = definition()["configuration"]["pressure"]["tuning"]["outputMaximum"] / 100
    for _ in range(200):
        _, pump = board.command(101.9, 7.0)
        assert pump == pytest.approx(ceiling)
    assert not board.preinfusion.regulating


def test_heater_changes_only_every_thermal_period():
    board = controller()
    commands = [board.command(101.9 - 0.05 * index, 7.0)[0] for index in range(40)]
    assert all(commands[odd] == commands[odd - 1] for odd in range(1, 40, 2))


def test_commands_stay_in_unit_range_whatever_the_measurement():
    generator = random.Random(20261021)
    board = controller()
    values = [float("nan"), float("inf"), -40.0, 0.0, 200.0, 13.0]
    for _ in range(2000):
        temperature = generator.choice(values + [generator.uniform(-20, 160)])
        pressure = generator.choice(values + [generator.uniform(-2, 16)])
        heater, pump = board.command(temperature, pressure)
        assert 0 <= heater <= 1
        assert 0 <= pump <= 1


class FlatPlant:
    def __init__(self, _path, step_seconds, output_channels, parameters=None):
        self.step_seconds, self.time = step_seconds, 0.0

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return None

    def step(self, heater, pump):
        self.time += self.step_seconds
        return FmuOutput(round(self.time * 1000), {"temperatureC": 101.9, "pressureBar": 7.0})


def replica(tmp_path, quantity="boiler-body-temperature", offset=None, **extra):
    conditions = {"channels": {"temperatureC": 101.9, "pressureBar": 4.9}, **extra}
    if offset is not None:
        conditions["boilerOffsetAppliedC"] = offset
    return run_simulation(
        "sim-12345678", "unused.fmu", 2.0,
        {"targets": {"temperatureC": 92.0, "pressureBar": 8.5},
         "tolerances": {"temperatureC": 1.0, "pressureBar": 0.5}},
        FileSeriesStore(tmp_path), adapter_factory=FlatPlant,
        controller_definition=definition(), initial_conditions=conditions,
        temperature_quantity=quantity,
    )


def test_runner_drives_the_plant_with_the_board_replica(tmp_path):
    result = replica(tmp_path)
    samples = FileSeriesStore(tmp_path).read(result.artifact)
    assert all(sample["commands"]["pumpCommand"] == pytest.approx(0.75) for sample in samples)
    # Con la bomba al 75 % la prealimentación lleva el calentador muy por encima
    # del techo del PI, que es lo que la placa hace al pulsar.
    assert samples[0]["commands"]["heaterCommand"] > 0.5
    target = result.summary["metrics"]["temperatureC"]["target"]
    assert target == pytest.approx(water_range.LINE_V6.boiler_reference_c(92.0))


def test_runner_adds_the_first_use_offset_the_board_applied(tmp_path):
    result = replica(tmp_path, offset=2.0)
    target = result.summary["metrics"]["temperatureC"]["target"]
    assert target == pytest.approx(water_range.LINE_V6.boiler_reference_c(92.0) + 2.0)


def test_the_replica_converts_with_the_line_the_preparation_declares(tmp_path):
    """Desde la v11 la placa convierte con otra recta: la réplica, con la del café."""
    v11 = water_range.CURRENT
    declared = {"plant": v11.method, "gain": v11.gain, "offsetC": v11.offset_c,
                "boilerMinimumC": v11.boiler_minimum_c, "boilerMaximumC": v11.boiler_maximum_c}
    result = replica(tmp_path, offset=1.0, waterReference=declared)
    target = result.summary["metrics"]["temperatureC"]["target"]
    assert target == pytest.approx(v11.boiler_reference_c(92.0) + 1.0)
    # La definición registrada no cambia: la recta del café es solo para esta corrida.
    assert definition()["configuration"]["temperature"]["waterReference"]["gain"] == (
        water_range.LINE_V6.gain
    )
    with pytest.raises(ValueError, match="recta"):
        replica(tmp_path, waterReference=None)


def test_board_replica_refuses_a_model_without_the_boiler_channel(tmp_path):
    with pytest.raises(ValueError, match="caldera"):
        replica(tmp_path, quantity="water-temperature")


def test_the_v2_definition_cites_the_acta_the_board_runs_since_28_oct():
    """La 2.0.0 cita el acta de temperatura v12 con su huella; la de presión no cambia."""
    v2 = load_registered_controller(ROOT, "board-replica", "2.0.0")
    path = ACTAS / "acta-temperatura-20261028b.json"
    acta = json.loads(path.read_text(encoding="utf-8"))
    declared = v2["configuration"]["temperature"]
    assert declared["actaSha256"] == hashlib.sha256(path.read_bytes()).hexdigest()
    assert declared["identifier"] == f"{acta['acta']}-v{acta['version']}"
    assert declared["waterReference"]["gain"] == acta["water_reference"]["gain"]
    assert declared["waterReference"]["offsetC"] == acta["water_reference"]["offset_c"]
    for key, name in (("lambda_s", "lambdaS"), ("output_maximum", "outputMaximum")):
        assert declared["tuning"][name] == acta["tuning"][key]
    assert declared["feedforward"]["gain"] == acta["feedforward"]["gain"]
    assert v2["configuration"]["pressure"] == definition()["configuration"]["pressure"]


def test_the_replica_uses_the_feedforward_gain_the_preparation_declares(tmp_path):
    """La subida del primer uso cambia la ganancia de un café: la réplica usa la del café."""
    base = FileSeriesStore(tmp_path).read(replica(tmp_path).artifact)
    boosted = FileSeriesStore(tmp_path).read(
        replica(tmp_path, feedforwardGainApplied=1.25).artifact
    )
    assert boosted[0]["commands"]["heaterCommand"] > base[0]["commands"]["heaterCommand"]
    # Sin un valor válido, la del acta: una ganancia nula o booleana no se toma.
    same = FileSeriesStore(tmp_path).read(
        replica(tmp_path, feedforwardGainApplied=True).artifact
    )
    assert same[0]["commands"]["heaterCommand"] == base[0]["commands"]["heaterCommand"]
    assert definition()["configuration"]["temperature"]["feedforward"]["gain"] == 0.75
