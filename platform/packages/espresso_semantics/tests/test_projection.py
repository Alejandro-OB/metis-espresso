from __future__ import annotations

from copy import deepcopy

from espresso_semantics import simulation_graph, simulation_jsonld, validate_simulation_graph
from rdflib import RDF, URIRef

ESIM = "https://w3id.org/espresso-dt/simulation#"
EDT = "https://w3id.org/espresso-dt#"


def completed_run() -> dict:
    metric = {
        "unit": "Cel",
        "initial": 20.0,
        "final": 93.0,
        "minimum": 20.0,
        "maximum": 93.2,
        "target": 93.0,
        "tolerance": 1.0,
        "meanAbsoluteError": 2.0,
        "withinTolerancePercent": 70.0,
    }
    pressure = dict(metric, unit="bar", initial=0.0, final=9.0, minimum=0.0, maximum=9.1)
    return {
        "simulationRunId": "sim-semantic-test01",
        "status": "succeeded",
        "startedAt": "2026-09-07T10:00:00Z",
        "finishedAt": "2026-09-07T10:01:00Z",
        "durationSeconds": 60.0,
        "model": {
            "id": "espresso-detailed",
            "version": "1.0.0",
            "sha256": "a" * 64,
            "contractVersion": "1.0",
        },
        "profileSnapshot": {"profileId": "profile-1", "profileVersion": 3},
        "controller": {"id": "profile-tracking", "version": "1.0"},
        "artifact": {
            "dataReference": "series://sim-semantic-test01",
            "sha256": "b" * 64,
            "sampleCount": 600,
        },
        "summary": {"metrics": {"temperatureC": metric, "pressureBar": pressure}},
    }


def test_simulation_projection_is_valid_and_distinct_from_physical_extraction():
    graph = simulation_graph(completed_run())
    assert validate_simulation_graph(graph)["conforms"] is True
    virtual = next(graph.subjects(RDF.type, URIRef(f"{ESIM}VirtualExtraction")))
    assert (virtual, RDF.type, URIRef(f"{EDT}EspressoExtraction")) not in graph
    assert len(list(graph.subjects(RDF.type, URIRef(f"{ESIM}SimulationSeries")))) == 2
    document = simulation_jsonld(completed_run())
    assert document["validation"]["conforms"] is True
    assert document["graphIri"].endswith("sim-semantic-test01")


def test_missing_model_checksum_fails_shacl():
    graph = simulation_graph(deepcopy(completed_run()))
    model = next(graph.subjects(RDF.type, URIRef(f"{ESIM}FmuModelArtifact")))
    graph.remove((model, URIRef(f"{ESIM}sha256"), None))
    assert validate_simulation_graph(graph)["conforms"] is False


def test_measured_type_on_simulated_observation_fails_shacl():
    graph = simulation_graph(completed_run())
    observation = next(graph.subjects(RDF.type, URIRef(f"{ESIM}SimulatedObservation")))
    graph.add((observation, RDF.type, URIRef(f"{EDT}MeasuredObservation")))
    assert validate_simulation_graph(graph)["conforms"] is False


def comparison_document() -> dict:
    return {
        "comparisonId": "comparison-extraction-1-sim-semantic-test01",
        "extractionId": "extraction-1",
        "simulationRunId": "sim-semantic-test01",
        "status": "succeeded",
        "verdict": "within-model-tolerance",
        "alignment": {"coverageRatio": 0.84, "measuredSampleCount": 50},
        "channels": [
            {
                "channel": "temperatureC", "unit": "Cel",
                "comparedSampleCount": 42, "coverageRatio": 0.84,
                "meanSignedDifference": 0.6, "meanAbsoluteDifference": 0.6,
                "rootMeanSquareDifference": 0.62, "maxAbsoluteDifference": 1.1,
                "modelTolerance": 2.5, "verdict": "within-model-tolerance",
                "contributesToVerdict": True,
            },
            {
                "channel": "pressureBar", "unit": "bar",
                "comparedSampleCount": 42, "coverageRatio": 0.84,
                "meanSignedDifference": -0.21, "meanAbsoluteDifference": 0.21,
                "rootMeanSquareDifference": 1.38, "maxAbsoluteDifference": 8.99,
                "modelTolerance": None, "verdict": "no-tolerance-declared",
                "contributesToVerdict": False,
                "modelLimitation": "static-hydraulic-model",
            },
        ],
    }


def test_comparison_is_projected_with_its_explicit_comparability_declaration():
    """La comparación se publica junto a canBeComparedWith, que es lo que la habilita."""
    run = completed_run()
    run["comparison"] = comparison_document()
    graph = simulation_graph(run)
    assert validate_simulation_graph(graph)["conforms"] is True

    comparison = next(graph.subjects(RDF.type, URIRef(f"{ESIM}PhysicalVirtualComparison")))
    virtual = next(graph.subjects(RDF.type, URIRef(f"{ESIM}VirtualExtraction")))
    measured = next(graph.objects(comparison, URIRef(f"{ESIM}comparesMeasuredExtraction")))
    # la extracción medida es real y la virtual sigue siendo disjunta de ella
    assert (measured, RDF.type, URIRef(f"{EDT}EspressoExtraction")) in graph
    assert (virtual, RDF.type, URIRef(f"{EDT}EspressoExtraction")) not in graph
    assert (virtual, URIRef(f"{ESIM}canBeComparedWith"), measured) in graph

    diferencias = list(graph.subjects(RDF.type, URIRef(f"{ESIM}ChannelDifference")))
    assert len(diferencias) == 2
    limitaciones = list(graph.objects(None, URIRef(f"{ESIM}modelLimitation")))
    assert [str(value) for value in limitaciones] == ["static-hydraulic-model"]


def test_comparison_without_comparability_declaration_fails_shacl():
    """Sin canBeComparedWith la comparación no puede publicarse: la forma lo impide."""
    run = completed_run()
    run["comparison"] = comparison_document()
    graph = simulation_graph(run)
    virtual = next(graph.subjects(RDF.type, URIRef(f"{ESIM}VirtualExtraction")))
    measured = next(graph.objects(None, URIRef(f"{ESIM}comparesMeasuredExtraction")))
    graph.remove((virtual, URIRef(f"{ESIM}canBeComparedWith"), measured))
    report = validate_simulation_graph(graph)
    assert report["conforms"] is False
    assert "comparable" in report["report"]


def test_a_failed_comparison_is_not_projected():
    """Una comparación fallida no llega al grafo: no es un hecho publicable."""
    run = completed_run()
    run["comparison"] = dict(comparison_document(), status="failed", verdict=None, channels=[])
    graph = simulation_graph(run)
    assert not list(graph.subjects(RDF.type, URIRef(f"{ESIM}PhysicalVirtualComparison")))
    assert validate_simulation_graph(graph)["conforms"] is True


def test_the_temperature_channel_takes_the_quantity_that_the_model_declares():
    """Con el modelo 2.x el canal es el cuerpo de la caldera, no el agua."""
    from espresso_semantics import simulation_graph as project

    run = completed_run()
    run["model"] = {**run["model"], "version": "2.1.0", "contractVersion": "2.0", "channels": [
        {"id": "temperatureC", "quantity": "boiler-body-temperature", "unit": "Cel"},
        {"id": "pressureBar", "quantity": "extraction-pressure", "unit": "bar"}]}
    graph = project(run)
    series = URIRef("https://w3id.org/espresso-dt/resource/simulation-series/sim-semantic-test01-temperatureC")
    prop = URIRef(EDT + "seriesForProperty")
    assert (series, prop, URIRef(EDT + "BoilerBodyTemperature")) in graph
    assert (series, prop, URIRef(EDT + "WaterTemperature")) not in graph
    assert validate_simulation_graph(graph)["conforms"]


def test_a_model_without_declared_channels_keeps_the_water():
    graph = simulation_graph(completed_run())
    series = URIRef("https://w3id.org/espresso-dt/resource/simulation-series/sim-semantic-test01-temperatureC")
    assert (series, URIRef(EDT + "seriesForProperty"), URIRef(EDT + "WaterTemperature")) in graph
