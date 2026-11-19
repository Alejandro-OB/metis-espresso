"""Trabajador de simulaciones FMI reclamadas desde PostgreSQL."""

from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable
from copy import deepcopy
from pathlib import Path
from typing import Any

from espresso_simulation import FileSeriesStore, ModelRegistry, load_registered_controller
from espresso_simulation.comparison import DEFAULT_MINIMUM_COVERAGE, compare

from aplicacion_web.adapters.database import SqlRepository, build_database
from aplicacion_web.config import Settings, get_settings
from aplicacion_web.domain.models import iso_now


def run_controller_definition(run: dict[str, Any], root: Path) -> dict[str, Any]:
    """La definición del controlador que la ejecución pidió, con la huella que guardó.

    Antes el trabajador cargaba siempre la de `profile-tracking`, de modo que el
    controlador declarado en la ejecución no tenía por qué ser el que corría.
    Una definición cuya huella cambió desde que se encoló no se ejecuta: la
    simulación diría haberse hecho con una configuración que no es la que usó.
    """
    reference = run.get("controller") or {}
    definition = load_registered_controller(root, reference["id"], reference["version"])
    expected = reference.get("configurationSha256")
    if expected and expected != definition["configurationSha256"]:
        raise ValueError("la configuración del controlador cambió desde que se encoló")
    return definition


def execute_isolated(
    run: dict[str, Any],
    model: Any,
    store: FileSeriesStore,
    progress: Callable[[int, int], None],
    cancelled: Callable[[], bool],
    timeout_seconds: float,
) -> dict[str, Any]:
    with tempfile.TemporaryDirectory(prefix="espresso-simulation-job-") as directory:
        directory_path = Path(directory)
        # Docker ejecuta con /tmp montado como noexec; FMPy debe extraer la
        # biblioteca nativa de la FMU en un volumen ejecutable.
        fmu_tmp = store.root / ".fmu-tmp"
        fmu_tmp.mkdir(parents=True, exist_ok=True)
        job_path = directory_path / "job.json"
        result_path = directory_path / "result.json"
        progress_path = directory_path / "progress.json"
        job_path.write_text(
            json.dumps(
                {
                    "runId": run["simulationRunId"],
                    "fmuPath": str(model.artifact_path),
                    "durationSeconds": run["durationSeconds"],
                    "profileSnapshot": run["profileSnapshot"],
                    "artifactRoot": str(store.root),
                    "artifactMaxBytes": store.max_bytes,
                    "outputChannels": model.output_channels,
                    "stepSeconds": model.data["capabilities"]["communicationStepSeconds"],
                    "controllerDefinition": run_controller_definition(run, model.root),
                    "initialConditions": run.get("initialConditions"),
                    "temperatureQuantity": model.channel_quantity("temperatureC")
                    or "water-temperature",
                    "resultPath": str(result_path),
                    "progressPath": str(progress_path),
                }
            ),
            encoding="utf-8",
        )
        child_environment = {**os.environ, "TMPDIR": str(fmu_tmp)}
        process = subprocess.Popen(  # noqa: S603
            [sys.executable, "-m", "espresso_simulation.cli", str(job_path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            env=child_environment,
        )
        started = time.monotonic()
        last_progress: tuple[int, int] | None = None
        while process.poll() is None:
            if cancelled():
                process.terminate()
                process.wait(timeout=5)
                raise InterruptedError("simulación cancelada")
            if time.monotonic() - started > timeout_seconds:
                process.kill()
                process.wait(timeout=5)
                raise TimeoutError("la simulación superó el tiempo máximo")
            if progress_path.exists():
                current = json.loads(progress_path.read_text(encoding="utf-8"))
                progress_value = (current["completed"], current["total"])
                if progress_value != last_progress:
                    progress(*progress_value)
                    last_progress = progress_value
            time.sleep(0.05)
        if process.returncode != 0:
            message = (process.stderr.read() if process.stderr else "").strip()
            raise RuntimeError(message[-500:] or "el subproceso FMI finalizó con error")
        return json.loads(result_path.read_text(encoding="utf-8"))


def build_comparison(
    repository: SqlRepository,
    store: FileSeriesStore,
    run: dict[str, Any],
    artifact: dict[str, Any],
    channel_maturity: dict[str, str] | None = None,
) -> dict[str, Any] | None:
    """Compara la preparación replicada con su simulación, si el trabajo la replica.

    Un fallo al comparar no invalida la simulación, que es un resultado por sí
    misma: se registra el motivo y la ejecución se conserva como exitosa.
    """
    replicates = run.get("replicates")
    if not replicates:
        return None
    extraction_id = str(replicates["extractionId"])
    try:
        measured = sorted(
            repository.list_documents(
                "live-sample", page=1, page_size=1000, filters={"extractionId": extraction_id}
            ).items,
            key=lambda sample: int(sample.get("sequence", 0)),
        )
        simulated = store.read(artifact)
        tolerances = {
            key: value
            for key, value in (replicates.get("modelTolerances") or {}).items()
            if value is not None
        }
        minimum = replicates.get("minimumCoverageRatio")
        document = compare(
            extraction_id=extraction_id,
            simulation_run_id=run["simulationRunId"],
            measured=measured,
            simulated=simulated,
            model_tolerances=tolerances or None,
            minimum_coverage=DEFAULT_MINIMUM_COVERAGE if minimum is None else float(minimum),
            initial_conditions=(run.get("initialConditions") or {}).get("channels"),
            channel_maturity=channel_maturity,
        )
    except (ValueError, KeyError, OSError) as error:
        return {
            "schemaVersion": "1.0",
            "comparisonId": f"comparison-{extraction_id}-{run['simulationRunId']}",
            "extractionId": extraction_id,
            "simulationRunId": run["simulationRunId"],
            "status": "failed",
            "error": {"code": "COMPARISON_FAILED", "message": str(error)[:300]},
            "createdAt": iso_now(),
        }
    # Una comparación solo acredita el proceso físico si la serie medida procede
    # del dispositivo. La aplicación también genera series con su simulador
    # interno, y ambas viven en el mismo lugar: la procedencia debe viajar hasta
    # el resultado para poder distinguirlas después (ADR-012 § 4.1).
    sources = sorted({str(sample.get("source") or "unknown") for sample in measured})
    document.update(
        {
            "status": "succeeded",
            "model": deepcopy(run["model"]),
            "controller": deepcopy(run["controller"]),
            "provenance": {
                "actor": run.get("requestedBy"),
                "recordedAt": iso_now(),
                "source": "simulation-comparison",
                "measuredSeries": {
                    "sources": sources,
                    "mixed": len(sources) > 1,
                },
            },
            "createdAt": iso_now(),
        }
    )
    return document


ERRORS: tuple[tuple[type[BaseException], str, bool], ...] = (
    (InterruptedError, "CANCELLED_BY_USER", False),
    (FileNotFoundError, "MODEL_NOT_FOUND", False),
    (PermissionError, "MODEL_NOT_APPROVED", False),
    (ArithmeticError, "FMU_NUMERICAL_FAILURE", False),
    (TimeoutError, "FMU_TIMEOUT", True),
    (ValueError, "FMI_CONTRACT_MISMATCH", False),
)


def normalized_error(error: BaseException) -> dict[str, Any]:
    for error_type, code, retryable in ERRORS:
        if isinstance(error, error_type):
            return {"code": code, "message": str(error), "retryable": retryable}
    return {
        "code": "FMU_EXECUTION_FAILED",
        "message": "La ejecución FMI no pudo completarse.",
        "retryable": False,
    }


def process_next(
    repository: SqlRepository,
    registry: ModelRegistry,
    store: FileSeriesStore,
    worker_id: str,
    lease_seconds: int,
    timeout_seconds: float = 120.0,
    executor: Callable[..., dict[str, Any]] = execute_isolated,
) -> bool:
    run = repository.claim_simulation(worker_id, lease_seconds)
    if run is None:
        return False
    run_id = run["simulationRunId"]
    try:
        model = registry.get(run["model"]["id"], run["model"]["version"])
        model.verify()
        result = executor(
            run,
            model,
            store,
            lambda completed, total: repository.heartbeat_simulation(
                run_id, worker_id, completed, total, lease_seconds
            ),
            lambda: repository.simulation_cancel_requested(run_id),
            timeout_seconds,
        )
        repository.save_simulation_artifact(result["artifact"])
        comparison = build_comparison(
            repository, store, run, result["artifact"], model.channel_maturity
        )
        if comparison is not None:
            repository.upsert_document(
                "twin-comparison", comparison["comparisonId"], comparison
            )
        current = repository.get_simulation(run_id) or run
        current.update(
            {
                "status": "succeeded",
                "progress": {
                    "completedSteps": result["summary"]["sampleCount"],
                    "totalSteps": result["summary"]["sampleCount"],
                    "percent": 100.0,
                },
                "finishedAt": iso_now(),
                "summary": result["summary"],
                "artifact": result["artifact"],
                "cancelRequested": False,
                "error": None,
                "comparison": comparison,
            }
        )
        repository.update_simulation(current)
    except Exception as error:
        current = repository.get_simulation(run_id) or run
        failure = normalized_error(error)
        current.update(
            {
                "status": "cancelled" if failure["code"] == "CANCELLED_BY_USER" else "failed",
                "finishedAt": iso_now(),
                "cancelRequested": failure["code"] == "CANCELLED_BY_USER",
                "error": failure,
            }
        )
        repository.update_simulation(current)
    return True


def run_forever(settings: Settings | None = None) -> None:
    settings = settings or get_settings()
    engine, session_factory = build_database(settings.database_url)
    repository = SqlRepository(session_factory)
    registry = ModelRegistry(settings.resolved_model_registry_path, settings.project_root)
    store = FileSeriesStore(
        settings.simulation_artifact_root, settings.simulation_artifact_max_bytes
    )
    worker_id = f"simulation-worker:{socket.gethostname()}"
    try:
        while True:
            worked = process_next(
                repository,
                registry,
                store,
                worker_id,
                settings.simulation_worker_lease_seconds,
                settings.simulation_worker_timeout_seconds,
            )
            if not worked:
                time.sleep(settings.simulation_worker_poll_seconds)
    finally:
        engine.dispose()


if __name__ == "__main__":
    run_forever()
