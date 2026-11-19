"""El contrato de una preparación cortada por falta de energía.

ADR-005 § 3 y § 8 punto 10 y RNF-006: al rearrancar, el dispositivo cierra la
preparación que nunca terminó y la envía como cualquier otra. No sabe cuándo
acabó ni cuánto duró, y si no tenía hora de pared tampoco sabe cuándo empezó.
Esos tres campos pueden faltar en ese caso y solo en ese: en cualquier otra
terminación un nulo diría que no consta algo que el dispositivo sí midió.
"""

import json
from copy import deepcopy
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator, FormatChecker
from referencing import Registry, Resource

CONTRACTS = Path(__file__).resolve().parents[4] / "device/software/contracts/v1"


def _validator(name: str) -> Draft202012Validator:
    registry = Registry()
    schemas = {}
    for path in sorted(CONTRACTS.glob("*.schema.json")):
        schema = json.loads(path.read_text(encoding="utf-8"))
        schemas[path.name] = schema
        if schema.get("$id"):
            registry = registry.with_resource(schema["$id"], Resource.from_contents(schema))
    return Draft202012Validator(
        schemas[name], registry=registry, format_checker=FormatChecker()
    )


def _errors(payload: dict) -> list[str]:
    validator = _validator("extraction.schema.json")
    return [
        f"{'/'.join(str(p) for p in error.path)}: {error.message}"
        for error in validator.iter_errors(payload)
    ]


#: Lo que el dispositivo envía al rearrancar, tal como lo acordó el firmware.
POWER_LOSS = {
    "id": "prep-cortada-0001",
    "source": "measured",
    "profile": {
        "id": "profile-placa-01",
        "name": "Perfil de la placa",
        "version": 1,
        "temperatureSetpointC": 93.0,
        "temperatureToleranceC": 0.5,
        "pressureSetpointBar": 9.0,
        "pressureToleranceBar": 0.5,
        "maximumTimeMs": 30000,
        "status": "accepted",
    },
    "calibration": {"id": "ntc-2026-10-09", "version": 4},
    "firmwareVersion": "861347f",
    "startedAt": None,
    "finishedAt": None,
    "durationMs": None,
    "termination": "unexpected-power-loss",
    "sampleCount": 0,
    "statistics": {
        "temperatureMaeC": None,
        "pressureMaeBar": None,
        "temperatureWithinTolerancePercent": None,
        "pressureWithinTolerancePercent": None,
        "window": {
            "rule": "pressure-first-within-tolerance-v1",
            "status": "undetermined",
            "startMs": None,
        },
    },
}


def test_a_power_loss_without_wall_clock_is_accepted():
    assert _errors(POWER_LOSS) == []


def test_a_power_loss_keeps_the_start_when_the_device_had_wall_clock():
    payload = {**deepcopy(POWER_LOSS), "startedAt": "2026-10-12T09:00:00Z"}
    assert _errors(payload) == []


@pytest.mark.parametrize(
    "termination",
    ["manual-normal-stop", "maximum-extraction-time-reached", "safety-protection-trip"],
)
@pytest.mark.parametrize("field", ["startedAt", "finishedAt", "durationMs"])
def test_any_other_termination_rejects_a_missing_instant_or_duration(termination, field):
    payload = {
        **deepcopy(POWER_LOSS),
        "startedAt": "2026-10-12T09:00:00Z",
        "finishedAt": "2026-10-12T09:00:30Z",
        "durationMs": 30000,
        "termination": termination,
    }
    assert _errors(payload) == []
    payload[field] = None
    errors = _errors(payload)
    assert errors and all(error.startswith(field) for error in errors), errors


def test_the_published_example_still_validates():
    message = json.loads(
        (CONTRACTS / "examples/extraction.simulation.json").read_text(encoding="utf-8")
    )
    assert _errors(message["payload"]) == []
