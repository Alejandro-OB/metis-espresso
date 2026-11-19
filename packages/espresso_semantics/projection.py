"""Transformación determinista del contrato de simulación al módulo EKO."""

from __future__ import annotations

import json
from decimal import Decimal
from pathlib import Path
from typing import Any
from urllib.parse import quote

from pyshacl import validate
from rdflib import RDF, XSD, Graph, Literal, Namespace, URIRef
from rdflib.namespace import PROV

EDT = Namespace("https://w3id.org/espresso-dt#")
ESIM = Namespace("https://w3id.org/espresso-dt/simulation#")
RESOURCE = "https://w3id.org/espresso-dt/resource/"
SEMANTIC_SCHEMA_VERSION = "1.1.1"
ROOT = Path(__file__).resolve().parents[2]
SHAPES = ROOT / "knowledge/ontology/ontology/espresso-simulation-shapes.ttl"
SCHEMA = ROOT / "knowledge/ontology/ontology/espresso-simulation.ttl"


def resource_iri(kind: str, identifier: str) -> URIRef:
    return URIRef(f"{RESOURCE}{quote(kind, safe='')}/{quote(identifier, safe='')}")


def graph_iri(simulation_run_id: str) -> str:
    return f"{RESOURCE}graph/simulation/{quote(simulation_run_id, safe='')}"


def _decimal(value: float | int) -> Literal:
    return Literal(Decimal(str(value)), datatype=XSD.decimal)


def simulation_graph(run: dict[str, Any]) -> Graph:
    if run.get("status") != "succeeded" or not run.get("artifact") or not run.get("summary"):
        raise ValueError("solo una simulación exitosa y completa puede proyectarse")
    graph = Graph(identifier=URIRef(graph_iri(run["simulationRunId"])))
    graph.bind("edt", EDT)
    graph.bind("esim", ESIM)
    graph.bind("prov", PROV)

    run_id = str(run["simulationRunId"])
    run_node = resource_iri("simulation-run", run_id)
    model = run["model"]
    model_node = resource_iri("fmu", f"{model['id']}@{model['version']}")
    scenario_node = resource_iri("simulation-scenario", run_id)
    virtual_node = resource_iri("virtual-extraction", run_id)
    executor_node = resource_iri("software-agent", "espresso-simulation-worker-v1")
    profile = run["profileSnapshot"]
    profile_node = resource_iri("profile", str(profile["profileId"]))

    graph.add((run_node, RDF.type, ESIM.SimulationRun))
    graph.add((run_node, ESIM.usedModel, model_node))
    graph.add((run_node, ESIM.usedScenario, scenario_node))
    graph.add((run_node, ESIM.generatedVirtualExtraction, virtual_node))
    graph.add((run_node, PROV.wasAssociatedWith, executor_node))
    graph.add((run_node, PROV.startedAtTime, Literal(run["startedAt"], datatype=XSD.dateTime)))
    graph.add((run_node, PROV.endedAtTime, Literal(run["finishedAt"], datatype=XSD.dateTime)))
    graph.add((run_node, ESIM.durationSeconds, _decimal(run["durationSeconds"])))
    graph.add((run_node, ESIM.semanticSchemaVersion, Literal(SEMANTIC_SCHEMA_VERSION)))

    graph.add((model_node, RDF.type, ESIM.FmuModelArtifact))
    graph.add((model_node, ESIM.modelId, Literal(model["id"])))
    graph.add((model_node, ESIM.modelVersion, Literal(model["version"])))
    graph.add((model_node, ESIM.contractVersion, Literal(model["contractVersion"])))
    graph.add((model_node, ESIM.sha256, Literal(model["sha256"])))

    graph.add((scenario_node, RDF.type, ESIM.SimulationScenario))
    graph.add((scenario_node, ESIM.profileId, Literal(profile["profileId"])))
    graph.add((scenario_node, ESIM.profileVersion, Literal(profile["profileVersion"])))
    graph.add((scenario_node, ESIM.controllerId, Literal(run["controller"]["id"])))
    graph.add((scenario_node, ESIM.controllerVersion, Literal(run["controller"]["version"])))
    graph.add((executor_node, RDF.type, ESIM.SimulationExecutor))
    graph.add((profile_node, RDF.type, EDT.ExtractionProfile))
    graph.add((virtual_node, RDF.type, ESIM.VirtualExtraction))
    graph.add((virtual_node, ESIM.simulatesProfile, profile_node))
    graph.add((virtual_node, PROV.wasGeneratedBy, run_node))

    artifact = run["artifact"]
    channel_terms = _channel_terms(model)
    for channel, metric in run["summary"]["metrics"].items():
        if channel not in channel_terms:
            continue
        property_node, unit_node = channel_terms[channel]
        series_node = resource_iri("simulation-series", f"{run_id}-{channel}")
        observation_node = resource_iri("simulation-observation", f"{run_id}-{channel}-final")
        graph.add((property_node, RDF.type, EDT.ObservableProperty))
        graph.add((unit_node, RDF.type, EDT.Unit))
        graph.add((series_node, RDF.type, ESIM.SimulationSeries))
        graph.add((series_node, EDT.seriesForProperty, property_node))
        graph.add((series_node, EDT.hasUnit, unit_node))
        graph.add(
            (
                series_node,
                EDT.dataReference,
                Literal(artifact["dataReference"], datatype=XSD.anyURI),
            )
        )
        graph.add((series_node, ESIM.sha256, Literal(artifact["sha256"])))
        graph.add(
            (series_node, ESIM.sampleCount, Literal(artifact["sampleCount"], datatype=XSD.integer))
        )
        graph.add((series_node, PROV.wasGeneratedBy, run_node))
        graph.add((virtual_node, ESIM.hasSimulationSeries, series_node))
        graph.add((observation_node, RDF.type, ESIM.SimulatedObservation))
        graph.add((observation_node, EDT.observedProperty, property_node))
        graph.add((observation_node, EDT.featureOfInterest, virtual_node))
        graph.add((observation_node, EDT.numericValue, _decimal(metric["final"])))
        graph.add((observation_node, EDT.hasUnit, unit_node))
        graph.add((observation_node, PROV.wasGeneratedBy, run_node))

    _add_comparison(graph, run, run_node, virtual_node, channel_terms)
    return graph


#: Magnitud que declara el registro de modelos para cada canal, y su término en EKO.
QUANTITY_TERMS = {
    "boiler-body-temperature": (EDT.BoilerBodyTemperature, EDT.DegreeCelsius),
    "water-temperature": (EDT.WaterTemperature, EDT.DegreeCelsius),
    "extraction-pressure": (EDT.ExtractionPressure, EDT.Bar),
}


def _channel_terms(model: dict[str, Any]) -> dict[str, tuple[URIRef, URIRef]]:
    """El término de cada canal sale de la magnitud que el modelo declara.

    Antes `temperatureC` era siempre el agua, y desde el modelo 2.0 ese canal es
    el termistor del cuerpo de la caldera (registro de modelos,
    `boiler-body-temperature`): la ontología publicaba como agua sobre el café
    la diferencia del cuerpo (2026-11-05, preguntas del módulo de simulación).
    Un modelo que no declara sus canales es anterior al contrato 2.0, cuyo
    canal de temperatura era el agua; para él rige esa correspondencia.
    """
    terms = {
        "temperatureC": (EDT.WaterTemperature, EDT.DegreeCelsius),
        "pressureBar": (EDT.ExtractionPressure, EDT.Bar),
    }
    for channel in model.get("channels") or []:
        term = QUANTITY_TERMS.get(str(channel.get("quantity")))
        if channel.get("id") in terms and term is not None:
            terms[str(channel["id"])] = term
    return terms


def _add_comparison(
    graph: Graph,
    run: dict[str, Any],
    run_node: URIRef,
    virtual_node: URIRef,
    channel_terms: dict[str, tuple[URIRef, URIRef]],
) -> None:
    """Proyecta la comparación físico-virtual, si la ejecución replica una preparación.

    Emite `esim:canBeComparedWith`, que es la declaración que habilita la
    comparación: compartir perfil no basta para afirmar equivalencia. Solo se
    publican métricas agregadas; las muestras alineadas permanecen en el
    artefacto de series, conforme a RD-005.
    """
    comparison = run.get("comparison")
    if not comparison or comparison.get("status") != "succeeded":
        return
    measured_node = resource_iri("extraction", str(comparison["extractionId"]))
    comparison_node = resource_iri("twin-comparison", str(comparison["comparisonId"]))
    graph.add((measured_node, RDF.type, EDT.EspressoExtraction))
    graph.add((virtual_node, ESIM.canBeComparedWith, measured_node))
    graph.add((comparison_node, RDF.type, ESIM.PhysicalVirtualComparison))
    graph.add((comparison_node, ESIM.comparesVirtualExtraction, virtual_node))
    graph.add((comparison_node, ESIM.comparesMeasuredExtraction, measured_node))
    graph.add((comparison_node, PROV.wasGeneratedBy, run_node))
    graph.add((comparison_node, ESIM.comparisonVerdict, Literal(comparison["verdict"])))
    graph.add(
        (comparison_node, ESIM.coverageRatio, _decimal(comparison["alignment"]["coverageRatio"]))
    )
    for channel in comparison.get("channels", []):
        terms = channel_terms.get(channel["channel"])
        if terms is None:
            continue
        property_node, unit_node = terms
        difference_node = resource_iri(
            "channel-difference", f"{comparison['comparisonId']}-{channel['channel']}"
        )
        graph.add((difference_node, RDF.type, ESIM.ChannelDifference))
        graph.add((difference_node, EDT.observedProperty, property_node))
        graph.add((difference_node, EDT.hasUnit, unit_node))
        graph.add((difference_node, ESIM.comparisonVerdict, Literal(channel["verdict"])))
        graph.add(
            (
                difference_node,
                ESIM.comparedSampleCount,
                Literal(channel["comparedSampleCount"], datatype=XSD.integer),
            )
        )
        graph.add((difference_node, ESIM.coverageRatio, _decimal(channel["coverageRatio"])))
        for key, term in (
            ("meanSignedDifference", ESIM.meanSignedDifference),
            ("meanAbsoluteDifference", ESIM.meanAbsoluteDifference),
            ("rootMeanSquareDifference", ESIM.rootMeanSquareDifference),
            ("maxAbsoluteDifference", ESIM.maxAbsoluteDifference),
            ("modelTolerance", ESIM.modelTolerance),
        ):
            value = channel.get(key)
            if value is not None:
                graph.add((difference_node, term, _decimal(value)))
        if channel.get("modelLimitation"):
            graph.add(
                (difference_node, ESIM.modelLimitation, Literal(channel["modelLimitation"]))
            )
        graph.add((comparison_node, ESIM.hasChannelDifference, difference_node))


def validate_simulation_graph(graph: Graph) -> dict[str, Any]:
    conforms, report_graph, report_text = validate(
        graph,
        shacl_graph=str(SHAPES),
        ont_graph=str(SCHEMA),
        advanced=True,
    )
    return {
        "conforms": bool(conforms),
        "report": str(report_text),
        "reportTurtle": report_graph.serialize(format="turtle"),
        "shapes": SHAPES.name,
        "schemaVersion": SEMANTIC_SCHEMA_VERSION,
    }


def simulation_jsonld(run: dict[str, Any]) -> dict[str, Any]:
    graph = simulation_graph(run)
    report = validate_simulation_graph(graph)
    if not report["conforms"]:
        raise ValueError(f"la proyección RDF no cumple SHACL: {report['report']}")
    return {
        "@context": {"edt": str(EDT), "esim": str(ESIM), "prov": str(PROV)},
        "@id": str(resource_iri("simulation-run", run["simulationRunId"])),
        "@graph": json.loads(graph.serialize(format="json-ld")),
        "schemaVersion": SEMANTIC_SCHEMA_VERSION,
        "graphIri": graph_iri(run["simulationRunId"]),
        "validation": {key: value for key, value in report.items() if key != "reportTurtle"},
    }
