"""Persistencia atómica y métricas de series simuladas."""

from __future__ import annotations

import gzip
import hashlib
import json
import os
from collections.abc import Iterable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any


def utc_iso() -> str:
    return datetime.now(UTC).isoformat().replace("+00:00", "Z")


class FileSeriesStore:
    def __init__(self, root: str | Path, max_bytes: int | None = None) -> None:
        self.root = Path(root)
        self.max_bytes = max_bytes

    def write(
        self,
        run_id: str,
        samples: Iterable[dict[str, Any]],
        step_seconds: float,
    ) -> dict[str, Any]:
        self.root.mkdir(parents=True, exist_ok=True)
        final_path = self.root / f"{run_id}.jsonl.gz"
        temporary_path = self.root / f".{run_id}.jsonl.gz.tmp"
        count = 0
        last_time = 0.0
        try:
            with temporary_path.open("wb") as raw:
                with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
                    for sample in samples:
                        count += 1
                        last_time = float(sample["simulationTimeSeconds"])
                        line = json.dumps(
                            sample, sort_keys=True, separators=(",", ":"), allow_nan=False
                        )
                        compressed.write(line.encode() + b"\n")
                raw.flush()
                os.fsync(raw.fileno())
            if count == 0:
                raise ValueError("no se puede publicar una serie vacía")
            if self.max_bytes is not None and temporary_path.stat().st_size > self.max_bytes:
                raise ValueError("el artefacto supera el tamaño máximo")
            os.replace(temporary_path, final_path)
        finally:
            temporary_path.unlink(missing_ok=True)
        payload = final_path.read_bytes()
        return {
            "schemaVersion": "1.0",
            "artifactId": f"artifact-{run_id}-series",
            "simulationRunId": run_id,
            "kind": "simulation-series",
            "format": "application/x-ndjson",
            "compression": "gzip",
            "sampleSchema": "simulation-sample/1.0",
            "dataReference": f"series://{run_id}",
            "sampleCount": count,
            "communicationStepSeconds": step_seconds,
            "simulatedDurationSeconds": last_time,
            "sizeBytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "createdAt": utc_iso(),
        }

    def read(self, manifest: dict[str, Any]) -> list[dict[str, Any]]:
        path = self.root / f"{manifest['simulationRunId']}.jsonl.gz"
        payload = path.read_bytes()
        if hashlib.sha256(payload).hexdigest() != manifest["sha256"]:
            raise ValueError("la suma de integridad de la serie no coincide")
        with gzip.open(path, "rt", encoding="utf-8") as stream:
            return [json.loads(line) for line in stream if line.strip()]


def calculate_summary(
    run_id: str,
    samples: list[dict[str, Any]],
    targets: dict[str, float],
    tolerances: dict[str, float | None],
    units: dict[str, str],
    artifact_id: str,
    step_seconds: float,
) -> dict[str, Any]:
    if not samples:
        raise ValueError("no se puede resumir una serie vacía")
    metrics: dict[str, dict[str, float | str]] = {}
    for channel, target in targets.items():
        values = [float(sample["channels"][channel]) for sample in samples]
        # Sin tolerancia declarada en el perfil no hay porcentaje dentro de
        # ella: inventar un margen juzgaría la serie con algo que nadie fijó.
        tolerance = tolerances.get(channel)
        metrics[channel] = {
            "unit": units[channel],
            "initial": values[0],
            "final": values[-1],
            "minimum": min(values),
            "maximum": max(values),
            "target": target,
            "tolerance": tolerance,
            "meanAbsoluteError": sum(abs(value - target) for value in values) / len(values),
            "withinTolerancePercent": None
            if tolerance is None
            else 100 * sum(abs(value - target) <= tolerance for value in values) / len(values),
        }
    return {
        "schemaVersion": "1.0",
        "simulationRunId": run_id,
        "source": "simulation",
        "sampleCount": len(samples),
        "communicationStepSeconds": step_seconds,
        "simulatedDurationSeconds": float(samples[-1]["simulationTimeSeconds"]),
        "metrics": metrics,
        "seriesArtifactId": artifact_id,
    }
