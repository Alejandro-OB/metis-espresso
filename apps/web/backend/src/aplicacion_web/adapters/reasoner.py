"""Recomendación del razonador de EKO, a pedido del barista (ADR-004 § 11.3).

Traduce lo que el razonador concluyó sobre una preparación (diagnóstico,
explicación y prueba recomendada) a la forma de recomendación que ya usa la
aplicación. No formula ninguna regla: si el razonador no concluye nada, se
abstiene y lo dice (§ 11.4).

La propuesta del razonador es una prueba de verificación: repetir la
preparación sosteniendo o moviendo una variable para confirmar o descartar el
diagnóstico. La dirección la da el razonador cuando la regla que la apoya la
declara (EKO 3.4.0: acortar el tiempo, bajar la dosis, subir el agua); sin ella,
el ajuste propuesto es «mantener» esa variable, con su objetivo si el perfil lo
declara. Nunca lleva un comando para la máquina y siempre exige revisión humana
(`HumanRecommendationValidationPolicy`).

Las conclusiones viajan en Turtle dentro del documento para que el trabajador
semántico, que es el único que escribe en el almacén, las publique en su grafo
nombrado.

**Qué no hace.** No decide qué preparaciones se comparan (la regla `comparable`
del razonador lo hace sobre el grafo que recibe), no publica y no aplica nada.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any
from uuid import uuid4

from espresso_semantics.extraction import EDT, PROPERTIES, UNITS, stable_iri
from espresso_semantics.reasoning import reason, validate_conclusions
from rdflib import DCTERMS, RDF, RDFS, SKOS, Graph, URIRef

from aplicacion_web.domain.models import iso_now

#: Término de EKO y nombre de la propiedad en la aplicación. Si varios nombres
#: comparten término, vale el primero, que es el que usa el perfil.
_PROPERTY_NAMES: dict[URIRef, str] = {}
for _name, _term in PROPERTIES.items():
    _PROPERTY_NAMES.setdefault(_term, _name)
_UNIT_NAMES = {term: name for name, term in UNITS.items()}
REASONER = EDT.EvidenceBasedReasoner
#: Dirección del razonador y la de la aplicación. Sin dirección, o con una que
#: la aplicación no representa (más fino, más grueso), la propuesta es mantener.
_DIRECTIONS = {EDT.Increase: "increase", EDT.Decrease: "decrease", EDT.Maintain: "maintain"}


class ReasonerRecommendationAdapter:
    def __init__(self, ontology_version: str) -> None:
        self.ontology_version = ontology_version

    def recommend(
        self,
        extraction_id: str,
        evidence: list[dict[str, Any]],
        knowledge: Callable[[], Graph | None] | None = None,
    ) -> dict[str, Any]:
        preparations = knowledge() if knowledge is not None else None
        if preparations is None:
            return self._abstain(
                extraction_id, evidence, "PREPARATION_NOT_MEASURED",
                "Solo una preparación medida y completa puede motivar una propuesta: "
                "una simulada o cortada por falta de energía no dice nada de la máquina.",
            )
        reasoned = reason(preparations)
        extraction = stable_iri("extraction", extraction_id)
        actions = sorted(reasoned.objects(extraction, EDT.hasRecommendedAction), key=str)
        withheld = sorted(reasoned.objects(extraction, EDT.hasWithheldRecommendation), key=str)
        if not actions and withheld:
            # El razonador analizó la preparación y no recomienda: ninguna
            # variable que la máquina comprueba aísla una causa (3-nov-2026).
            return self._abstain(
                extraction_id, evidence, "NO_MEASURABLE_CAUSE",
                str(reasoned.value(withheld[0], EDT.description) or
                    "Con las variables que mide la máquina no se aísla una causa; "
                    "la decisión sobre qué probar queda en manos del barista."),
            )
        if not actions:
            return self._abstain(
                extraction_id, evidence, "NO_REASONER_CONCLUSION",
                "El razonador no encontró en esta preparación ni en las comparables "
                "un diagnóstico que justifique una prueba.",
            )
        # Primero las que responden a un defecto que el barista registró en la
        # cata de esta preparación: pide la recomendación porque algo no le
        # gustó, y la principal debe hablar de eso. Después, las que se apoyan
        # en una regla con fuente (EKO 3.4.0), que dicen hacia dónde mover la
        # variable; las demás piden repetir. El nombre solo desempata. Hasta el
        # 2026-11-05 el nombre decidía entre reglas con fuente: las que responden
        # a un defecto (`…-Excessive…`) quedaban antes que la de la masa
        # (`…-InferredDeviation…`) solo por cómo se escriben, y una regla nueva
        # con otro nombre habría cambiado la recomendación principal sin que
        # nadie lo decidiera.
        perceived = {
            defect
            for evaluation in reasoned.objects(extraction, EDT.hasSensoryEvaluation)
            for defect in reasoned.objects(evaluation, EDT.hasPerceivedDefect)
        }

        def answers_defect(node: URIRef) -> bool:
            diagnosis = reasoned.value(node, EDT.respondsTo)
            return diagnosis is not None and any(
                target in perceived for target in reasoned.objects(diagnosis, EDT.isDiagnosisOf)
            )

        actions.sort(
            key=lambda node: (
                not answers_defect(node),
                reasoned.value(node, EDT.supportedByHeuristic) is None,
                str(node),
            )
        )
        action = actions[0]
        diagnosis = reasoned.value(action, EDT.respondsTo)
        target = reasoned.value(action, EDT.adjustsProperty)
        explanations = [
            str(reasoned.value(node, EDT.description))
            for node in sorted(reasoned.subjects(EDT.explains, diagnosis), key=str)
        ] if diagnosis is not None else []
        specification = self._specification(reasoned, extraction, target)
        procedure = reasoned.value(action, EDT.followsProcedure) or REASONER
        conclusion_ids = [str(node) for node in (diagnosis, action) if node is not None]
        heuristic = reasoned.value(action, EDT.supportedByHeuristic) or (
            reasoned.value(diagnosis, EDT.supportedByHeuristic) if diagnosis is not None else None
        )
        conclusions = _conclusions(reasoned, preparations, extraction)
        conforms, report = validate_conclusions(conclusions, preparations)
        return {
            **self._base(extraction_id, evidence, conclusion_ids),
            "status": "pending-human-review",
            "reasonCode": "REASONER_PROPOSAL",
            "summary": str(reasoned.value(action, EDT.description)),
            "respondsTo": {
                "code": str(reasoned.value(diagnosis, EDT.statusCode) or "diagnosis"),
                "summary": str(reasoned.value(diagnosis, EDT.description) or ""),
            },
            "diagnosis": {
                "iri": str(diagnosis) if diagnosis is not None else None,
                "summary": str(reasoned.value(diagnosis, EDT.description) or ""),
                "explanations": explanations,
            },
            "proposedAdjustment": {
                "property": _PROPERTY_NAMES.get(target, str(target)),
                "direction": _DIRECTIONS.get(
                    reasoned.value(action, EDT.recommendedDirection), "maintain"
                ),
                "value": specification[0],
                "unit": specification[1],
            },
            # La regla y su fuente, para que el barista juzgue de dónde sale la
            # propuesta y no solo qué propone.
            "heuristic": None if heuristic is None else {
                "label": str(reasoned.value(heuristic, RDFS.label) or ""),
                "description": str(reasoned.value(heuristic, EDT.description) or ""),
                "source": str(reasoned.value(heuristic, DCTERMS.source) or ""),
                "confidence": str(reasoned.value(heuristic, SKOS.note) or ""),
            },
            "alternatives": [str(reasoned.value(node, EDT.description)) for node in actions[1:]],
            "uncertainty": {"value": None, "methodReference": str(procedure)},
            "applicability": (
                f"Propuesta del razonador de EKO {self.ontology_version} sobre esta "
                "preparación y las comparables del mismo día, café y perfil. Es una "
                "prueba para confirmar o descartar el diagnóstico y requiere "
                "validación humana."
            ),
            "requiresHumanReview": True,
            "conclusionGraph": conclusions.serialize(format="turtle"),
            # La acción que el barista acepta, cambia o rechaza: la decisión se
            # publica sobre ella (PC19).
            "actionIri": str(action),
            # El trabajador semántico solo publica lo que cumple las formas; el
            # informe queda aquí para saber qué corregir si no.
            "conclusionValidation": {
                "conforms": conforms,
                "report": None if conforms else report[:4000],
            },
        }

    @staticmethod
    def _specification(
        graph: Graph, extraction: URIRef, target: URIRef | None
    ) -> tuple[float | None, str | None]:
        profile = graph.value(extraction, EDT.usesProfile)
        for spec in graph.objects(profile, EDT.hasParameterSpecification) if profile else ():
            if graph.value(spec, EDT.forProperty) != target:
                continue
            value = graph.value(spec, EDT.targetValue)
            unit = graph.value(spec, EDT.hasUnit)
            return (
                float(value) if value is not None else None,
                _UNIT_NAMES.get(unit) if unit is not None else None,
            )
        return None, None

    def _base(
        self, extraction_id: str, evidence: list[dict[str, Any]], conclusions: list[str]
    ) -> dict[str, Any]:
        evidence_ids = [item["id"] for item in evidence] + conclusions
        return {
            "schemaVersion": "1.0",
            "origin": "reasoner",
            "model": None,
            "recommendationId": f"rec-{uuid4().hex[:12]}",
            "inferenceId": f"reasoning-{uuid4().hex[:12]}",
            "extractionId": extraction_id,
            "evidenceIds": list(dict.fromkeys(evidence_ids))
            or [f"missing-evidence:{extraction_id}"],
            "knowledgeSource": {"name": "EKO", "version": self.ontology_version},
            "actuatorCommand": None,
            "predictedSensoryProfile": None,
            "createdAt": iso_now(),
        }

    def _abstain(
        self, extraction_id: str, evidence: list[dict[str, Any]], code: str, summary: str
    ) -> dict[str, Any]:
        return {
            **self._base(extraction_id, evidence, []),
            "status": "abstained",
            "reasonCode": code,
            "summary": summary,
            "respondsTo": {"code": code.lower().replace("_", "-"), "summary": summary},
            "proposedAdjustment": None,
            "uncertainty": {"value": None, "methodReference": str(REASONER)},
            "applicability": "Sin propuesta: no hay nada que validar.",
            "requiresHumanReview": False,
        }


def _following(reasoned: Graph, preparations: Graph, seeds: list[Any]) -> Graph:
    """Los nodos que el razonador infirió a partir de `seeds`, con sus triples.

    Sigue los nodos inferidos que cita cada uno (diagnósticos, desviaciones,
    explicaciones) y lleva consigo las reglas con fuente en que se apoyan.
    """
    inferred = {
        node for node in reasoned.subjects(EDT.generatedBy, REASONER)
        if (node, None, None) not in preparations
    }
    pending = list(seeds)
    selected: set[URIRef] = set()
    while pending:
        node = pending.pop()
        if node in selected:
            continue
        selected.add(node)
        for value in reasoned.objects(node, None):
            if value in inferred:
                pending.append(value)
        for explanation in reasoned.subjects(EDT.explains, node):
            if explanation in inferred:
                pending.append(explanation)
    result = Graph()
    result.bind("edt", EDT)
    for node in selected:
        for triple in reasoned.triples((node, None, None)):
            result.add(triple)
    # La conclusión viaja con el conocimiento que cita (EKO 3.4.0): la regla con
    # su descripción y su fuente. Sin ellas las formas no pueden comprobar que
    # la heurística declara de dónde sale.
    for node in selected:
        for heuristic in reasoned.objects(node, EDT.supportedByHeuristic):
            for triple in reasoned.triples((heuristic, None, None)):
                result.add(triple)
    return result


def _conclusions(
    reasoned: Graph, preparations: Graph, extraction: URIRef, *, with_actions: bool = True
) -> Graph:
    """Lo que el razonador añadió sobre esta preparación, sin el vocabulario.

    Parte de su evaluación de conformidad y, con `with_actions`, de sus
    acciones recomendadas, y sigue los nodos inferidos que citan (diagnósticos,
    desviaciones, explicaciones). Las demás preparaciones del día quedan fuera:
    sus conclusiones se publican cuando alguien pide analizarlas. Sin acciones
    es lo que se publica de cada café al evaluar su conformidad: las propuestas
    solo salen cuando el barista las pide (ADR-004 § 11.3.3).
    """
    pending = [
        *reasoned.subjects(EDT.assessesExtraction, extraction),
        *(reasoned.objects(extraction, EDT.hasRecommendedAction) if with_actions else ()),
    ]
    result = _following(reasoned, preparations, pending)
    result.add((extraction, RDF.type, EDT.EspressoExtraction))
    if with_actions:
        for action in reasoned.objects(extraction, EDT.hasRecommendedAction):
            result.add((extraction, EDT.hasRecommendedAction, action))
    return result


def adjustment_conclusions(
    reasoned: Graph, preparations: Graph, adjustment: URIRef
) -> dict[str, Any]:
    """La evaluación que el razonador hace de un ajuste, lista para publicar (PC21)."""
    conclusions = _following(
        reasoned, preparations, list(reasoned.subjects(EDT.assessesAdjustment, adjustment))
    )
    conforms, report = validate_conclusions(conclusions, preparations)
    return {
        "conclusionGraph": conclusions.serialize(format="turtle"),
        "conclusionValidation": {
            "conforms": conforms,
            "report": None if conforms else report[:4000],
        },
    }


def conformance_conclusions(
    reasoned: Graph, preparations: Graph, extraction: URIRef
) -> dict[str, Any]:
    """La evaluación de conformidad y sus desviaciones, listas para publicar."""
    conclusions = _conclusions(reasoned, preparations, extraction, with_actions=False)
    conforms, report = validate_conclusions(conclusions, preparations)
    return {
        "conclusionGraph": conclusions.serialize(format="turtle"),
        "conclusionValidation": {
            "conforms": conforms,
            "report": None if conforms else report[:4000],
        },
    }
