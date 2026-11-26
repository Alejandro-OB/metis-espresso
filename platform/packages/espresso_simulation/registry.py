"""Registro verificable de FMU autorizadas."""

from __future__ import annotations

import hashlib
import json
import platform
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass(frozen=True)
class RegisteredModel:
    data: dict[str, Any]
    root: Path
    #: Arquitectura en que se va a ejecutar. Una FMU trae binarios nativos: la
    #: de x86_64 no carga en la Raspberry (aarch64), y al revés.
    machine: str = field(default_factory=platform.machine)

    @property
    def identifier(self) -> str:
        return self.data["modelId"]

    @property
    def version(self) -> str:
        return self.data["modelVersion"]

    @property
    def artifact(self) -> dict[str, Any]:
        """El artefacto de esta arquitectura, con su ruta y su huella.

        Si el registro declara `artifactsByArchitecture`, se toma el de la
        máquina en que se corre y, si no hay, el modelo no se ejecuta aquí: dar
        el de otra arquitectura fallaría al cargar, o peor, sería otro binario
        sin la comprobación de equivalencia que acompaña a cada uno. Sin esa
        declaración vale `artifact`, como en los modelos anteriores.
        """
        by_architecture = self.data.get("artifactsByArchitecture")
        if not by_architecture:
            return self.data["artifact"]
        if self.machine not in by_architecture:
            raise PermissionError(
                f"modelo {self.identifier}@{self.version} sin artefacto para {self.machine}"
            )
        return by_architecture[self.machine]

    @property
    def artifact_path(self) -> Path:
        return self.root / self.artifact["path"]

    @property
    def output_channels(self) -> tuple[str, ...]:
        return tuple(channel["id"] for channel in self.data["capabilities"]["channels"])

    def channel_quantity(self, channel: str) -> str | None:
        """Qué magnitud física declara el registro para un canal del modelo."""
        for item in self.data["capabilities"]["channels"]:
            if item["id"] == channel:
                return item.get("quantity")
        return None

    @property
    def channel_maturity(self) -> dict[str, str]:
        """Madurez que el registro declara para cada canal del modelo."""
        return {
            channel["id"]: channel["maturity"]
            for channel in self.data["capabilities"]["channels"]
            if channel.get("maturity")
        }

    def verify(self) -> None:
        for declared in (self.artifact, self.data["contract"], self.data["source"]):
            path = self.root / declared["path"]
            if not path.is_file():
                raise FileNotFoundError(path)
            if file_sha256(path) != declared["sha256"]:
                raise ValueError(f"checksum inválido para {path}")


class ModelRegistry:
    def __init__(self, path: str | Path, root: str | Path | None = None) -> None:
        self.path = Path(path)
        self.root = Path(root) if root is not None else self.path.parents[1]
        payload = json.loads(self.path.read_text(encoding="utf-8"))
        if payload.get("schemaVersion") != "1.0":
            raise ValueError("versión de registro de modelos no soportada")
        self._models = {
            (item["modelId"], item["modelVersion"]): RegisteredModel(item, self.root)
            for item in payload["models"]
        }

    def get(
        self, identifier: str, version: str, *, include_fixtures: bool = False
    ) -> RegisteredModel:
        model = self._models.get((identifier, version))
        if model is None:
            raise KeyError(f"modelo {identifier}@{version} no registrado")
        if model.data["status"] != "approved-for-simulation":
            raise PermissionError(f"modelo {identifier}@{version} no aprobado")
        if model.data["role"] == "fixture" and not include_fixtures:
            raise PermissionError(f"modelo {identifier}@{version} reservado para pruebas")
        return model

    def canonical(self, identifier: str) -> RegisteredModel | None:
        """La versión aprobada que el registro declara vigente para ese modelo.

        La réplica de una preparación debe correr con ella cuando nadie pide
        otra: fijar una versión en el código dejó las comparaciones con la v1
        sustituida después de aprobarse la v2.
        """
        for model in self._models.values():
            if (
                model.identifier == identifier
                and model.data["role"] == "canonical"
                and model.data["status"] == "approved-for-simulation"
            ):
                return model
        return None

    def public_models(self) -> list[dict[str, Any]]:
        return [
            {
                "id": model.identifier,
                "version": model.version,
                "displayName": model.data["displayName"],
                "capabilities": model.data["capabilities"],
            }
            for model in self._models.values()
            if model.data["role"] == "canonical"
            and model.data["status"] == "approved-for-simulation"
        ]
