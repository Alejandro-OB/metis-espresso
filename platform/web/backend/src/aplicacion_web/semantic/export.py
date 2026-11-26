"""Compatibilidad de la API con la transformación semántica compartida."""

from __future__ import annotations

from typing import Any

from espresso_semantics.extraction import (
    SemanticProjectionError,
    extraction_graph,
    stable_iri,
)
from espresso_semantics.extraction import (
    validated_jsonld as canonical_validated_jsonld,
)

from aplicacion_web.domain.errors import ValidationError


def validated_jsonld(
    extraction: dict[str, Any],
    profile: dict[str, Any],
    references: dict[str, Any] | None = None,
) -> dict[str, Any]:
    try:
        return canonical_validated_jsonld(extraction, profile, references=references)
    except SemanticProjectionError:
        raise ValidationError(
            "No se pudo comprobar la integridad de este registro. "
            "Revisa que tenga perfil, responsable, fechas y unidades."
        ) from None


__all__ = ["extraction_graph", "stable_iri", "validated_jsonld"]
