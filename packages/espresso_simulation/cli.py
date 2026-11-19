"""Proceso hijo para una ejecución FMI aislada."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

from .artifacts import FileSeriesStore
from .runner import run_simulation


def atomic_json(path: Path, payload: dict) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload), encoding="utf-8")
    os.replace(temporary, path)


def main() -> int:
    job_path = Path(sys.argv[1])
    job = json.loads(job_path.read_text(encoding="utf-8"))
    result_path = Path(job["resultPath"])
    progress_path = Path(job["progressPath"])
    result = run_simulation(
        job["runId"],
        job["fmuPath"],
        job["durationSeconds"],
        job["profileSnapshot"],
        FileSeriesStore(job["artifactRoot"], job.get("artifactMaxBytes")),
        tuple(job["outputChannels"]),
        job["stepSeconds"],
        progress=lambda completed, total: atomic_json(
            progress_path, {"completed": completed, "total": total}
        ),
        controller_definition=job["controllerDefinition"],
        initial_conditions=job.get("initialConditions"),
        temperature_quantity=job.get("temperatureQuantity", "water-temperature"),
    )
    atomic_json(result_path, {"artifact": result.artifact, "summary": result.summary})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
