from __future__ import annotations

import asyncio
import json

from espresso_profiles.barista_guide import cup_mass, machine_facts, time_for_ratio
from fastapi import APIRouter, Query, Request
from fastapi.responses import StreamingResponse
from sqlalchemy import text

from aplicacion_web import __version__
from aplicacion_web.domain.models import iso_now
from aplicacion_web.presentation.dependencies import Admin, IdentityDep, Service
from aplicacion_web.presentation.schemas import DashboardResponse

router = APIRouter(tags=["system"])


@router.get("/health/live")
def live() -> dict[str, str]:
    return {"status": "alive", "version": __version__}


@router.get("/health/ready")
def ready(request: Request) -> dict[str, str]:
    with request.app.state.session_factory() as session:
        session.execute(text("SELECT 1"))
    return {"status": "ready", "version": __version__}


@router.get("/api/v1/health")
def health(request: Request) -> dict[str, object]:
    with request.app.state.session_factory() as session:
        session.execute(text("SELECT 1"))
    return {"status": "ok", "schemaVersion": "1.0", "applicationVersion": __version__}


@router.get("/api/v1/status")
def status(request: Request, _: IdentityDep) -> dict[str, object]:
    telemetry = request.app.state.repository.latest_telemetry()
    semantic_available = (
        True
        if request.app.state.settings.environment == "test"
        else request.app.state.fuseki.ping()
    )
    return {
        "connected": telemetry is not None,
        "latestTelemetry": telemetry,
        "services": [
            {"id": "database", "label": "Base histórica", "status": "available"},
            {
                "id": "semantic",
                "label": "Evidencia semántica",
                "status": "available" if semantic_available else "unavailable",
                "adapter": (
                    "in-memory-test"
                    if request.app.state.settings.environment == "test"
                    else "fuseki"
                ),
            },
            {
                "id": "recommendation",
                "label": "Modelo contextual",
                "status": "degraded",
                "detail": "Referencia técnica en abstención",
            },
        ],
        "checkedAt": iso_now(),
    }


@router.get("/api/v1/machine-state")
def machine_state(service: Service, _: IdentityDep) -> dict[str, object]:
    return service.machine_state()


@router.get("/api/v1/machine-state/live-preparation")
def live_preparation(service: Service, _: IdentityDep) -> dict[str, object]:
    return service.live_preparation()


@router.get("/api/v1/barista-guide")
def barista_guide(
    _: IdentityDep,
    cut_seconds: float | None = Query(default=None, alias="cutSeconds", gt=0, le=120),
    ratio: float | None = Query(default=None, gt=0, le=10),
    dose_g: float = Query(default=13.0, alias="doseG", gt=0, le=40),
) -> dict[str, object]:
    """Lo medido sobre la máquina y, si se piden, la masa para un corte y el
    corte para un ratio. Fuera de lo medido, «no hay datos» con su motivo."""
    return {
        "facts": machine_facts(),
        "cupMass": cup_mass(cut_seconds, dose_g) if cut_seconds is not None else None,
        "timeForRatio": time_for_ratio(ratio, dose_g) if ratio is not None else None,
    }


@router.get("/api/v1/dashboard", response_model=DashboardResponse)
def dashboard(service: Service, _: IdentityDep) -> dict[str, object]:
    return service.dashboard()


@router.get("/api/v1/system")
def system(request: Request, _: Admin) -> dict[str, object]:
    manifest = request.app.state.semantic_manifest
    return {
        "application": {"name": "Aplicación web espresso", "version": __version__},
        "contract": {"version": "1.0", "prefix": "/api/v1"},
        "ontology": {
            "name": manifest.data["ontology"]["id"],
            "version": manifest.data["ontology"]["version"],
            "sha256": manifest.data["ontology"]["sha256"],
            "adapter": (
                "in-memory-test" if request.app.state.settings.environment == "test" else "fuseki"
            ),
        },
        "counts": request.app.state.repository.counts(),
        "authority": "Consulta, análisis y decisión humana; sin rutas de actuación física.",
    }


@router.get("/api/v1/semantic/status")
def semantic_status(request: Request, _: Admin) -> dict[str, object]:
    client = request.app.state.fuseki
    manifest = request.app.state.semantic_manifest
    return {
        "status": "available" if client.ping() else "unavailable",
        "dataset": request.app.state.settings.fuseki_dataset_url,
        "ontology": {key: manifest.data["ontology"][key] for key in ("id", "version", "sha256")},
        "simulationModule": {
            key: manifest.data["simulationModule"][key] for key in ("id", "version", "sha256")
        },
        "checkedAt": iso_now(),
    }


@router.post("/api/v1/semantic/rebuild", status_code=202)
def semantic_rebuild(
    service: Service, _: Admin, simulation_run_id: str | None = None
) -> dict[str, object]:
    return service.rebuild_semantics(simulation_run_id)


@router.get("/api/v1/audit")
def audit(
    request: Request, _: Admin, page: int = Query(1, ge=1), page_size: int = Query(25, ge=1, le=100)
) -> dict[str, object]:
    return request.app.state.repository.list_audit(page, page_size).as_dict()


@router.get("/events")
async def events(_: IdentityDep) -> StreamingResponse:
    async def stream():
        while True:
            payload = json.dumps({"at": iso_now(), "status": "connected"})
            yield f"event: heartbeat\ndata: {payload}\n\n"
            await asyncio.sleep(20)

    return StreamingResponse(stream(), media_type="text/event-stream")
