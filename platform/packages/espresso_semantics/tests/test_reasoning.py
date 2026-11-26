"""La conformidad la decide el razonador de EKO sobre el grafo de la preparación."""

from __future__ import annotations

from espresso_semantics.extraction import extraction_graph, stable_iri
from espresso_semantics.reasoning import reason, technical_conformance

PROFILE = {
    "id": "perfil-r",
    "status": "accepted",
    "specifications": [
        {"property": "pressure", "unit": "bar", "strategy": "target",
         "target": 9.0, "tolerance": 0.5},
        {"property": "coffee-dose", "unit": "g", "strategy": "range",
         "minimum": 12.0, "maximum": 14.0},
    ],
}
EXTRACTION = {
    "id": "prep-r",
    "barista": "barista-1",
    "coffeeId": "cafe-1",
    "startedAt": "2026-10-16T09:00:00Z",
    "finishedAt": "2026-10-16T09:00:28Z",
    "termination": "manual-normal-stop",
    "seriesRef": "serie",
    "sampleCount": 10,
}


def observation(key, prop, unit, value, **extra):
    return {"key": key, "property": prop, "unit": unit, "value": value,
            "kind": "measured", "resultTime": "2026-10-16T09:00:28Z", **extra}


def assess(observations):
    graph = reason(extraction_graph(EXTRACTION, PROFILE, observations))
    return technical_conformance(graph, stable_iri("extraction", "prep-r"))


def test_full_coverage_within_limits_is_conformant():
    result = assess([observation("pressure-max", "pressure", "bar", 9.3),
                     observation("dose", "coffee-dose", "g", 13.0)])
    assert result["status"] == "conformant"
    assert result["deviations"] == []


def test_one_extreme_outside_is_enough_for_non_conformance():
    result = assess([observation("pressure-min", "pressure", "bar", 8.9),
                     observation("pressure-max", "pressure", "bar", 9.6)])
    assert result["status"] == "non-conformant"
    assert [item["code"] for item in result["deviations"]] == ["above-profile"]


def test_a_missing_observation_is_not_conformance():
    result = assess([observation("pressure-max", "pressure", "bar", 9.0)])
    assert result["status"] == "insufficient-information"
