"""Acceso al razonador de EKO desde la plataforma (ADR-004 § 11.3).

El razonador vive en `ontology/reasoning/materialize.py` y sus reglas
no se copian aquí: la § 11.3.4 prohíbe una segunda formulación del mismo
conocimiento. Este módulo solo le arma el grafo, lo ejecuta y lee lo que
concluyó sobre una preparación.

Se carga por ruta y no como paquete instalado porque la ontología y su
razonador se versionan juntos en `ontology`; instalar el razonador
aparte permitiría ejecutar unas reglas con otra versión del vocabulario.

**Qué no hace.** No publica nada, no decide qué preparaciones son comparables
(eso lo dice la regla `comparable` del razonador sobre el grafo que recibe) y no
traduce las conclusiones a recomendaciones de la aplicación.
"""

from __future__ import annotations

import importlib.util
from functools import cache
from pathlib import Path
from types import ModuleType
from typing import Any

from pyshacl import validate
from rdflib import RDF, Graph, URIRef

from .extraction import EDT

ROOT = Path(__file__).resolve().parents[3]
ONTOLOGY = ROOT / "ontology/owl/espresso-dt.ttl"
SHAPES = ROOT / "ontology/owl/espresso-dt-shapes.ttl"
MATERIALIZER = ROOT / "ontology/reasoning/materialize.py"

#: Categoría de EKO y el estado que ya usaba la plataforma para ella.
CONFORMANCE = {
    EDT.TechnicallyConformant: "conformant",
    EDT.TechnicallyNonConformant: "non-conformant",
    EDT.TechnicalConformanceInconclusive: "insufficient-information",
}


@cache
def _materializer() -> ModuleType:
    spec = importlib.util.spec_from_file_location("eko_materialize", MATERIALIZER)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"no se encontró el razonador en {MATERIALIZER}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@cache
def _vocabulary() -> Graph:
    return Graph().parse(ONTOLOGY, format="turtle")


def reason(preparations: Graph) -> Graph:
    """Vocabulario más las preparaciones, con lo que el razonador concluyó."""
    graph = Graph()
    graph.bind("edt", EDT)
    for triple in _vocabulary():
        graph.add(triple)
    for triple in preparations:
        graph.add(triple)
    _materializer().materialize(graph)
    return graph


def technical_conformance(graph: Graph, extraction: URIRef) -> dict[str, Any] | None:
    """Lo que el razonador concluyó sobre la conformidad de una preparación.

    `None` si no hay evaluación: la preparación no tiene perfil en el grafo.
    """
    assessment = next(graph.subjects(EDT.assessesExtraction, extraction), None)
    if assessment is None:
        return None
    category = graph.value(assessment, EDT.hasTechnicalConformance)
    deviations = [
        {
            "property": str(graph.value(observation, EDT.observedProperty)),
            "code": str(graph.value(node, EDT.statusCode)),
            "description": str(graph.value(node, EDT.description)),
        }
        for node in graph.objects(assessment, EDT.derivedFrom)
        if (node, RDF.type, EDT.TechnicalDeviation) in graph
        for observation in graph.objects(node, EDT.derivedFrom)
        if graph.value(observation, EDT.observedProperty) is not None
    ]
    return {
        "status": CONFORMANCE.get(category, "insufficient-information"),
        "category": str(category),
        "description": str(graph.value(assessment, EDT.description)),
        "assessment": str(assessment),
        "deviations": deviations,
    }


def validate_conclusions(conclusions: Graph, preparations: Graph) -> tuple[bool, str]:
    """Las formas de EKO sobre lo concluido, junto a la evidencia que cita.

    Solas, las conclusiones no cumplen: las formas exigen que cada desviación y
    cada evaluación se apoyen en observaciones de la misma preparación, y esas
    observaciones están en el grafo de la preparación. Con la inferencia RDFS
    del vocabulario, un `MeasuredObservation` cuenta como `Observation`.
    """
    conforms, _, report = validate(
        conclusions + preparations,
        shacl_graph=str(SHAPES),
        ont_graph=_vocabulary(),
        inference="rdfs",
        advanced=True,
    )
    return bool(conforms), str(report)
