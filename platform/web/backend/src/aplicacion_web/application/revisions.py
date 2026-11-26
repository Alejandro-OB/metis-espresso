"""Un documento con sus revisiones aplicadas, sin pasar por el servicio.

Las medidas a mano, la catación y el retiro de un perfil no reescriben el
documento: se guardan como revisiones. El servicio las aplica al leer
(`ApplicationService._project_resource`); el trabajador semántico no tiene el
servicio y publicaba la preparación tal como se cerró. Esta función es la misma
regla, para que los dos lean lo mismo.
"""

from __future__ import annotations

from copy import deepcopy
from typing import Any


def revisions_of(repository: Any, kind: str, identifier: str) -> list[dict[str, Any]]:
    items = repository.list_documents(
        f"{kind}-revision", page=1, page_size=1000, filters={"resourceId": identifier}
    ).items
    return sorted(items, key=lambda item: (item["recordedAt"], item["id"]))


def with_revisions(repository: Any, kind: str, document: dict[str, Any]) -> dict[str, Any]:
    return apply_revisions(document, revisions_of(repository, kind, str(document["id"])))


def revisions_by_resource(repository: Any, kind: str) -> dict[str, list[dict[str, Any]]]:
    """Todas las revisiones de un tipo, agrupadas y en el orden de `revisions_of`.

    Para listados: una consulta en vez de una por documento.
    """
    grouped: dict[str, list[dict[str, Any]]] = {}
    for item in repository.list_documents(f"{kind}-revision", page=1, page_size=1_000_000).items:
        grouped.setdefault(str(item.get("resourceId")), []).append(item)
    for items in grouped.values():
        items.sort(key=lambda item: (item["recordedAt"], item["id"]))
    return grouped


def apply_revisions(document: dict[str, Any], revisions: list[dict[str, Any]]) -> dict[str, Any]:
    projected = deepcopy(document)
    for revision in revisions:
        projected.update(deepcopy(revision["changes"]))
    if revisions:
        projected["revisions"] = [
            {key: value for key, value in revision.items() if key != "changes"}
            for revision in revisions
        ]
    return projected
