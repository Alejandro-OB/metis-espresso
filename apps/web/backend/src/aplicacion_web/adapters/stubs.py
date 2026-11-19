from __future__ import annotations

from typing import Any
from uuid import uuid4

from aplicacion_web.application.ports import (
    ExplanationPort,
    RecommendationPort,
    Repository,
    SemanticPort,
)
from aplicacion_web.domain.models import iso_now


class SemanticServiceUnavailable(RuntimeError):
    """El grafo no respondió; no equivale a que no existan hechos."""


class FusekiSemanticAdapter(SemanticPort):
    def __init__(self, client: Any) -> None:
        self.client = client

    def evidence_for(self, resource_id: str) -> list[dict[str, Any]]:
        from espresso_semantics.extraction import extraction_graph_iri, stable_iri

        try:
            return self.client.evidence_for(
                str(stable_iri("extraction", resource_id)),
                extraction_graph_iri(resource_id),
            )
        except Exception as error:
            # Devolver una lista vacía presentaría un servicio caído como
            # ausencia de hechos, que son cosas distintas para quien interpreta
            # la evidencia.
            raise SemanticServiceUnavailable(
                "El grafo semántico no está disponible; la ausencia de hechos no "
                "puede afirmarse."
            ) from error


class TraceableSemanticAdapter(SemanticPort):
    def __init__(self, repository: Repository) -> None:
        self.repository = repository

    def evidence_for(self, resource_id: str) -> list[dict[str, Any]]:
        extraction = self.repository.get_document("extraction", resource_id)
        if extraction is None:
            return []
        facts = [
            {
                "id": f"fact:{resource_id}:source",
                "predicate": "prov:wasDerivedFrom",
                "value": extraction.get("source", "unknown"),
                "origin": "confirmed-history",
            }
        ]
        statistics = extraction.get("statistics") or {}
        for name in ("temperatureMaeC", "pressureMaeBar"):
            if name in statistics:
                facts.append(
                    {
                        "id": f"fact:{resource_id}:{name}",
                        "predicate": name,
                        "value": statistics[name],
                        "origin": "calculated-summary",
                    }
                )
        return facts


class AbstainingRecommendationAdapter(RecommendationPort):
    """Referencia técnica segura hasta aprobar un modelo experimental."""

    def recommend(
        self,
        extraction_id: str,
        evidence: list[dict[str, Any]],
        knowledge: Any = None,
    ) -> dict[str, Any]:
        evidence_ids = [item["id"] for item in evidence] or [f"missing-evidence:{extraction_id}"]
        return {
            "schemaVersion": "1.0",
            "origin": "machine-learning",
            "model": {
                "name": "Modelo contextual",
                "version": None,
                "approvalStatus": "unavailable",
            },
            "recommendationId": f"rec-{uuid4().hex[:12]}",
            "inferenceId": f"inference-{uuid4().hex[:12]}",
            "extractionId": extraction_id,
            "status": "abstained",
            "reasonCode": "MODEL_NOT_APPROVED",
            "summary": "No existe un modelo contextual aprobado para proponer un ajuste.",
            "respondsTo": {
                "code": "model-not-approved",
                "summary": "No hay una recomendación disponible para esta preparación.",
            },
            "proposedAdjustment": None,
            "uncertainty": {
                "value": None,
                "methodReference": "https://example.invalid/methods/technical-abstention-v1",
            },
            "evidenceIds": evidence_ids,
            "knowledgeSource": {"name": "EKO", "version": "3.5.0"},
            "applicability": (
                "Referencia técnica con datos sintéticos; no es válida para actuación física."
            ),
            "requiresHumanReview": False,
            "actuatorCommand": None,
            "predictedSensoryProfile": None,
            "createdAt": iso_now(),
        }


class DeterministicExplanationAdapter(ExplanationPort):
    def explain(
        self, recommendation: dict[str, Any], evidence: list[dict[str, Any]]
    ) -> dict[str, Any]:
        abstained = recommendation.get("status") == "abstained"
        return {
            "recommendationId": recommendation["recommendationId"],
            "summary": recommendation["summary"],
            "reasoning": (
                "No se sugiere un cambio porque la fuente disponible aún no está aprobada."
                if abstained
                else "La sugerencia responde al hallazgo registrado y cita la evidencia disponible."
            ),
            "evidenceIds": [item["id"] for item in evidence],
            "limits": [
                "La explicación resume hechos registrados.",
                "No constituye evidencia y no ordena cambios en la cafetera.",
            ],
            "provider": {"name": "deterministic-test-adapter", "version": "1.0"},
        }
