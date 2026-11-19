"""Lo que el grafo de una preparación toma de otros registros (EKO 3.5.0).

La descripción del tostador, el sabor buscado, la ficha del café y los
equipos no viven en la preparación sino en el café, el perfil y los equipos,
así que quien arma el grafo los busca aquí y se los pasa a `extraction_graph`. El
perfil es el documento vigente de esa versión, no la copia que guarda la
preparación, que no trae el sabor buscado.
"""

from __future__ import annotations

from typing import Any, Protocol


class _Documents(Protocol):
    def get_document(self, kind: str, identifier: str) -> dict[str, Any] | None: ...


def sensory_references(repository: _Documents, extraction: dict[str, Any]) -> dict[str, Any]:
    coffee_id = extraction.get("coffeeId")
    profile_id = (extraction.get("profile") or {}).get("id")
    coffee = repository.get_document("coffee", str(coffee_id)) if coffee_id else None
    profile = repository.get_document("profile", str(profile_id)) if profile_id else None
    # La máquina y el molino que la preparación declara. `deviceId` es la
    # placa, no un equipo registrado; solo cuenta si coincide con uno.
    equipment = [
        document
        for identifier in dict.fromkeys(
            str(value) for value in (extraction.get("machineId"), extraction.get("grinderId"))
            if value
        )
        if (document := repository.get_document("equipment", identifier)) is not None
    ]
    return {
        "declared": (coffee or {}).get("declaredSensoryProfile"),
        "target": (profile or {}).get("sensoryTarget"),
        "coffee": coffee,
        "equipment": equipment,
    }
