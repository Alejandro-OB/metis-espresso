from __future__ import annotations

import csv
import io
import json
from typing import Annotated, Any, Literal

from fastapi import APIRouter, Depends, Header, Query, Request, Response, status

from aplicacion_web.domain.models import Identity
from aplicacion_web.presentation.dependencies import Admin, IdentityDep, Service, Writer
from aplicacion_web.presentation.schemas import (
    AdjustmentRegistration,
    CalibrationAdjustmentAdd,
    CalibrationClose,
    CalibrationStart,
    CalibrationTestAdd,
    CoffeeCreate,
    CoffeeUpdate,
    CompareRequest,
    DeclaredSensoryProfileInput,
    EpisodeCreate,
    EpisodeFollowUpLink,
    EpisodeOutcomeCreate,
    EquipmentCreate,
    ExtractionAlertPreferences,
    ExtractionContext,
    LegacyProfileCreate,
    LiveExtractionFinish,
    LiveExtractionStart,
    ManualMeasurements,
    ProfileActivation,
    ProfileCompareRequest,
    ProfileCreate,
    ProfileSensoryTarget,
    ProfileUpdate,
    RecommendationCreate,
    RecommendationDecision,
    SensoryEvaluation,
    SimulationCreate,
    SustainmentExclusion,
    SustainmentSuspension,
    SyncRequest,
    ToleranceFromExtractions,
    TwinComparisonRequest,
    WaterReferenceReading,
)

router = APIRouter(prefix="/api/v1")


def _list(
    service: Service,
    kind: str,
    page: int | None,
    page_size: int,
    query: str | None,
    filters: dict[str, str] | None = None,
) -> dict[str, Any]:
    result = service.list(kind, page or 1, page_size, query, filters)
    return result if page is not None else {"items": result["items"]}


@router.post("/sync", tags=["ingestion"])
def sync(
    payload: SyncRequest,
    service: Service,
    _: Writer,
    x_real_ip: Annotated[str | None, Header()] = None,
) -> dict[str, Any]:
    # nginx pone la dirección de quien llamó; con ella se prueba si la
    # máquina contesta cuando deja de enviar (application/reachability.py).
    return service.sync(payload.model_dump(), source_address=x_real_ip)


@router.get("/simulation-models", tags=["simulations"])
def simulation_models(service: Service, _: IdentityDep) -> dict[str, Any]:
    return {"items": service.simulation_models()}


@router.post("/simulations", tags=["simulations"], status_code=status.HTTP_202_ACCEPTED)
def create_simulation(
    payload: SimulationCreate,
    service: Service,
    identity: Writer,
    idempotency_key: str = Header(..., alias="Idempotency-Key", min_length=1, max_length=120),
) -> dict[str, Any]:
    result, _created = service.create_simulation(payload.model_dump(), identity, idempotency_key)
    return result


@router.get("/simulations", tags=["simulations"])
def simulations(
    service: Service,
    _: IdentityDep,
    page: int = Query(1, ge=1),
    page_size: int = Query(25, ge=1, le=100),
) -> dict[str, Any]:
    return service.simulations(page, page_size)


@router.get("/simulations/{simulation_id}", tags=["simulations"])
def simulation(simulation_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.simulation(simulation_id)


@router.get("/simulations/{simulation_id}/series", tags=["simulations"])
def simulation_series(simulation_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.simulation_series(simulation_id)


@router.post("/simulations/{simulation_id}/cancel", tags=["simulations"])
def cancel_simulation(simulation_id: str, service: Service, identity: Writer) -> dict[str, Any]:
    return service.cancel_simulation(simulation_id, identity)


@router.get("/simulations/{simulation_id}/semantic", tags=["simulations", "semantics"])
def simulation_semantic(simulation_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.simulation_semantic(simulation_id)


@router.get("/traceability/{kind}/{identifier}", tags=["traceability"])
def traceability(
    kind: Literal["barista", "profile", "extraction"],
    identifier: str,
    service: Service,
    _: IdentityDep,
) -> dict[str, Any]:
    return service.traceability(kind, identifier)


@router.get("/me/extraction-alerts", tags=["extractions"])
def extraction_alert_preferences(service: Service, identity: IdentityDep) -> dict[str, Any]:
    return service.extraction_alert_preferences(identity)


@router.get("/equipment", tags=["equipment"])
def equipment(service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.list("equipment", 1, 100)


@router.post("/equipment", tags=["equipment"], status_code=status.HTTP_201_CREATED)
def create_equipment(
    payload: EquipmentCreate, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.create("equipment", payload.model_dump(mode="json"), identity)


@router.get("/equipment/{equipment_id}", tags=["equipment"])
def equipment_detail(equipment_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.get("equipment", equipment_id)


@router.put("/me/extraction-alerts", tags=["extractions"])
def update_extraction_alert_preferences(
    payload: ExtractionAlertPreferences, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.update_extraction_alert_preferences(payload.model_dump(), identity)


@router.post("/telemetry", tags=["ingestion"], status_code=status.HTTP_202_ACCEPTED)
def telemetry(payload: dict[str, Any], service: Service, _: Writer) -> dict[str, Any]:
    return service.sync({"messages": [payload]})


@router.post("/extractions/live", tags=["extractions"], status_code=status.HTTP_201_CREATED)
def start_live_extraction(
    payload: LiveExtractionStart, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.start_live_extraction(payload.model_dump(), identity)


@router.get("/extractions/{extraction_id}/live", tags=["extractions"])
def live_extraction(
    extraction_id: str,
    request: Request,
    service: Service,
    identity: IdentityDep,
    elapsed_ms: int | None = Header(None, alias="X-Test-Elapsed-Ms", ge=0),
) -> dict[str, Any]:
    # El tiempo simulado solo puede fijarse en pruebas, igual que el rol.
    forced = elapsed_ms if request.app.state.settings.environment == "test" else None
    return service.live_extraction(extraction_id, identity, forced)


@router.post("/extractions/{extraction_id}/finish", tags=["extractions"])
def finish_live_extraction(
    extraction_id: str,
    payload: LiveExtractionFinish,
    service: Service,
    identity: Writer,
) -> dict[str, Any]:
    return service.finish_live_extraction(extraction_id, payload.model_dump(), identity)


@router.get("/extractions/{extraction_id}/series", tags=["extractions"])
def extraction_series(extraction_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.extraction_series(extraction_id)


def history_filters(
    query: str | None = Query(None, max_length=120),
    coffee_id: str | None = Query(None, alias="coffeeId", max_length=80),
    date_from: str | None = Query(None, alias="from", max_length=40),
    date_to: str | None = Query(None, alias="to", max_length=40),
    source: Literal["measured", "simulation"] | None = None,
    result: str | None = Query(None, max_length=80),
    barista: str | None = Query(None, max_length=120),
    variety: str | None = Query(None, max_length=120),
    process: str | None = Query(None, max_length=120),
    roast: str | None = Query(None, max_length=120),
    profile_id: str | None = Query(None, alias="profileId", max_length=80),
    profile_version: int | None = Query(None, alias="profileVersion", ge=1),
    machine_id: str | None = Query(None, alias="machineId", max_length=80),
    grinder_id: str | None = Query(None, alias="grinderId", max_length=80),
    calibration_id: str | None = Query(None, alias="calibrationId", max_length=80),
    technical_conformance: str | None = Query(None, alias="technicalConformance", max_length=40),
    sensory_acceptance: str | None = Query(None, alias="sensoryAcceptance", max_length=40),
    defect: str | None = Query(None, max_length=80),
) -> tuple[str | None, dict[str, str]]:
    """La búsqueda y los filtros del historial, compartidos con la exportación.

    Una sola definición para los dos: la exportación ignoraba los filtros y
    descargaba las preparaciones de todos aunque en pantalla se vieran solo las
    del barista de la sesión.
    """
    normalized_from = f"{date_from}T00:00:00Z" if date_from and len(date_from) == 10 else date_from
    normalized_to = f"{date_to}T23:59:59.999999Z" if date_to and len(date_to) == 10 else date_to
    filters = {
        key: value
        for key, value in {
            "coffeeId": coffee_id,
            "barista": barista,
            "variety": variety,
            "process": process,
            "roast": roast,
            "profileId": profile_id,
            "profileVersion": str(profile_version) if profile_version else None,
            "machineId": machine_id,
            "grinderId": grinder_id,
            "calibrationId": calibration_id,
            "technicalConformance": technical_conformance,
            "sensoryAcceptance": sensory_acceptance,
            "defect": defect,
            "source": source,
            "termination": result,
            "finishedAtFrom": normalized_from,
            "finishedAtBefore": normalized_to,
        }.items()
        if value is not None
    }
    return query, filters


HistoryFilters = Annotated[tuple[str | None, dict[str, str]], Depends(history_filters)]


def scoped_history(identity: Identity, filters: dict[str, str]) -> dict[str, str]:
    """Un barista ve solo sus preparaciones, en la lista y al exportar.

    Lo decide el servidor por la sesión y no el filtro que mande la pantalla:
    antes la casilla «Solo mis preparaciones» se podía desmarcar y cualquier
    barista veía los cafés de los demás. Quien administra o solo consulta
    sigue viendo todo y puede filtrar por barista.
    """
    if identity.role == "barista":
        # Las pasadas de agua archivadas no son cafés suyos (4-nov-2026); quien
        # administra las sigue viendo, con su marca.
        return {**filters, "barista": identity.subject, "purposeNot": "water-pass"}
    return filters


@router.get("/extractions", tags=["extractions"])
def extractions(
    service: Service,
    identity: IdentityDep,
    search: HistoryFilters,
    page: int | None = Query(None, ge=1),
    page_size: int = Query(25, ge=1, le=100),
) -> dict[str, Any]:
    query, filters = search
    filters = scoped_history(identity, filters)
    history = service.extraction_history(page or 1, page_size, query, filters)
    return history if page is not None else {"items": history["items"]}


@router.get("/extractions/claimable", tags=["extractions"])
def claimable_extractions(
    service: Service, identity: IdentityDep, limit: int = Query(10, ge=1, le=50)
) -> dict[str, Any]:
    """Los últimos cafés de la máquina que Preparar café puede tomar como suyos.

    No pasa por el historial (`scoped_history`): un café recién llegado todavía
    no tiene barista, porque se le asigna justo al encontrarlo, y el historial
    de un barista solo muestra los suyos. Con ese filtro Preparar café no
    encontraba nunca el café (26-oct-2026) y, además, cada consulta recorría
    todas las preparaciones en vez de leer solo la primera página.
    """
    return {"items": service.claimable_extractions(identity.subject, limit)}


@router.post("/extractions/compare", tags=["extractions"])
def compare(payload: CompareRequest, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.compare(payload.ids, payload.dimensions)


@router.get("/extractions/export", tags=["extractions"])
def export_extractions(
    service: Service,
    identity: IdentityDep,
    search: HistoryFilters,
    format: Literal["json", "csv"] = "json",
) -> Response:
    query, filters = search
    filters = scoped_history(identity, filters)
    # Lo mismo que el historial con esos filtros, en una sola página: el tope
    # de 1000 de antes cortaba en silencio la exportación de un historial largo.
    items = service.extraction_history(1, 1_000_000, query, filters)["items"]
    if format == "json":
        content = json.dumps(
            {"schemaVersion": "1.0", "exported": items}, ensure_ascii=False, indent=2
        )
        return Response(
            content,
            media_type="application/json",
            headers={"Content-Disposition": "attachment; filename=extracciones-v1.json"},
        )
    stream = io.StringIO()
    writer = csv.writer(stream)
    writer.writerow(
        [
            "id",
            "source",
            "startedAt",
            "finishedAt",
            "durationMs",
            "termination",
            "coffeeId",
            "barista",
        ]
    )
    for item in items:
        writer.writerow(
            [
                item.get(key)
                for key in (
                    "id",
                    "source",
                    "startedAt",
                    "finishedAt",
                    "durationMs",
                    "termination",
                    "coffeeId",
                    "barista",
                )
            ]
        )
    return Response(
        stream.getvalue(),
        media_type="text/csv",
        headers={"Content-Disposition": "attachment; filename=extracciones-v1.csv"},
    )


@router.get("/extractions/{extraction_id}/export", tags=["extractions"])
def export_extraction_record(extraction_id: str, service: Service, _: IdentityDep) -> Response:
    content = json.dumps(
        {
            "schemaVersion": "1.0",
            "record": service.extraction_history_record(extraction_id),
        },
        ensure_ascii=False,
        indent=2,
    )
    return Response(
        content,
        media_type="application/json",
        headers={"Content-Disposition": f'attachment; filename="preparacion-{extraction_id}.json"'},
    )


@router.get("/extractions/{extraction_id}/semantic", tags=["semantics"])
def semantic_extraction(extraction_id: str, service: Service, _: IdentityDep) -> Response:
    content = json.dumps(service.semantic_extraction(extraction_id), ensure_ascii=False, indent=2)
    return Response(
        content,
        media_type="application/ld+json",
        headers={
            "Content-Disposition": f'attachment; filename="preparacion-{extraction_id}.jsonld"'
        },
    )


@router.get("/extractions/{extraction_id}", tags=["extractions"])
def extraction(extraction_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.get("extraction", extraction_id)


@router.get("/extractions/{extraction_id}/history-record", tags=["extractions"])
def extraction_history_record(
    extraction_id: str, service: Service, _: IdentityDep
) -> dict[str, Any]:
    return service.extraction_history_record(extraction_id)


@router.get("/extractions/{extraction_id}/result", tags=["extractions"])
def extraction_result(extraction_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.extraction_result(extraction_id)


@router.get("/extractions/{extraction_id}/conformity-assessments", tags=["extractions"])
def conformity_assessments(
    extraction_id: str, service: Service, _: IdentityDep
) -> dict[str, Any]:
    service.get("extraction", extraction_id)
    # El grafo de conclusiones es para el trabajador semántico, no para la
    # pantalla: con él cada evaluación pesaba decenas de kilobytes.
    hidden = {"conclusionGraph", "conclusionValidation"}
    items = [
        {key: value for key, value in item.items() if key not in hidden}
        for item in service.conformity_assessments(extraction_id)
    ]
    return {"extractionId": extraction_id, "items": items, "total": len(items)}


@router.put("/extractions/{extraction_id}/context", tags=["sensory"])
def context(
    extraction_id: str,
    payload: ExtractionContext,
    service: Service,
    identity: Writer,
    idempotency_key: str | None = Header(None, alias="Idempotency-Key"),
) -> dict[str, Any]:
    return service.link_context(extraction_id, payload.model_dump(), identity, idempotency_key)


@router.put("/extractions/{extraction_id}/manual-measurements", tags=["extractions"])
def manual_measurements(
    extraction_id: str,
    payload: ManualMeasurements,
    service: Service,
    identity: Writer,
    idempotency_key: str | None = Header(None, alias="Idempotency-Key"),
) -> dict[str, Any]:
    return service.measurements(
        extraction_id, payload.model_dump(exclude_unset=True), identity, idempotency_key
    )


@router.put("/extractions/{extraction_id}/sensory-evaluation", tags=["sensory"])
def sensory(
    extraction_id: str,
    payload: SensoryEvaluation,
    service: Service,
    identity: Writer,
    idempotency_key: str | None = Header(None, alias="Idempotency-Key"),
) -> dict[str, Any]:
    return service.sensory(extraction_id, payload.model_dump(), identity, idempotency_key)


@router.post(
    "/extractions/{extraction_id}/twin-comparison",
    tags=["extractions", "simulations"],
    status_code=status.HTTP_202_ACCEPTED,
)
def request_twin_comparison(
    extraction_id: str,
    payload: TwinComparisonRequest,
    service: Service,
    identity: Writer,
    idempotency_key: str = Header(..., alias="Idempotency-Key", min_length=1, max_length=120),
) -> dict[str, Any]:
    document, _created = service.request_twin_comparison(
        extraction_id, payload.model_dump(), identity, idempotency_key
    )
    return document


@router.get("/extractions/{extraction_id}/twin-comparison", tags=["extractions", "simulations"])
def twin_comparison(extraction_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.twin_comparisons(extraction_id)


@router.get("/sustainment", tags=["sustainment"])
def sustainment_status(service: Service, _: IdentityDep) -> dict[str, Any]:
    """Suspensión activa y la subida del primer uso que rige, con su regla."""
    return service.sustainment.status()


@router.post("/sustainment/suspension", tags=["sustainment"], status_code=status.HTTP_201_CREATED)
def suspend_sustainment(
    payload: SustainmentSuspension, service: Service, identity: Admin
) -> dict[str, Any]:
    return service.sustainment.suspend(identity, payload.reason)


@router.delete("/sustainment/suspension", tags=["sustainment"])
def resume_sustainment(service: Service, identity: Admin) -> dict[str, Any]:
    return service.sustainment.resume(identity)


@router.get("/profiles/{profile_id}/sustainment-exclusion", tags=["profiles", "sustainment"])
def sustainment_exclusion(profile_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.sustainment.exclusion(profile_id)


@router.post("/profiles/{profile_id}/sustainment-exclusion", tags=["profiles", "sustainment"])
def exclude_from_sustainment(
    profile_id: str,
    service: Service,
    identity: Writer,
    payload: SustainmentExclusion | None = None,
) -> dict[str, Any]:
    return service.sustainment.exclude(profile_id, identity, payload.reason if payload else None)


@router.delete("/profiles/{profile_id}/sustainment-exclusion", tags=["profiles", "sustainment"])
def include_in_sustainment(profile_id: str, service: Service, identity: Writer) -> dict[str, Any]:
    return service.sustainment.include(profile_id, identity)


@router.get("/extractions/{extraction_id}/evidence", tags=["semantics"])
def evidence(extraction_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    service.get("extraction", extraction_id)
    return service.evidence(extraction_id)


@router.get("/coffees", tags=["coffees"])
def coffees(
    service: Service,
    _: IdentityDep,
    page: int | None = Query(None, ge=1),
    page_size: int = Query(25, ge=1, le=100),
    query: str | None = None,
) -> dict[str, Any]:
    return _list(service, "coffee", page, page_size, query)


@router.post("/coffees", tags=["coffees"], status_code=status.HTTP_201_CREATED)
def create_coffee(payload: CoffeeCreate, service: Service, identity: Writer) -> dict[str, Any]:
    return service.create("coffee", payload.model_dump(mode="json"), identity)


@router.get("/coffees/{coffee_id}", tags=["coffees"])
def coffee(coffee_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.get("coffee", coffee_id)


@router.put("/coffees/{coffee_id}", tags=["coffees"])
def update_coffee(
    coffee_id: str, payload: CoffeeUpdate, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.update_coffee(coffee_id, payload.model_dump(mode="json"), identity)


@router.put("/coffees/{coffee_id}/declared-sensory-profile", tags=["coffees", "sensory"])
def declared_sensory_profile(
    coffee_id: str,
    payload: DeclaredSensoryProfileInput,
    service: Service,
    identity: Writer,
) -> dict[str, Any]:
    return service.set_declared_sensory_profile(coffee_id, payload.model_dump(), identity)


@router.get("/learning-episodes", tags=["episodes"])
def episodes(
    service: Service,
    _: IdentityDep,
    page: int | None = Query(None, ge=1),
    page_size: int = Query(25, ge=1, le=100),
) -> dict[str, Any]:
    return _list(service, "episode", page, page_size, None)


@router.post("/learning-episodes", tags=["episodes"], status_code=status.HTTP_201_CREATED)
def create_episode(payload: EpisodeCreate, service: Service, identity: Writer) -> dict[str, Any]:
    return service.create_episode(payload.model_dump(), identity)


@router.post(
    "/learning-episodes/adjustments",
    tags=["episodes"],
    status_code=status.HTTP_201_CREATED,
)
def register_adjustment(
    payload: AdjustmentRegistration, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.register_adjustment(payload.model_dump(), identity)


@router.post("/learning-episodes/{episode_id}/verification", tags=["episodes"])
def start_episode_verification(
    episode_id: str, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.start_episode_verification(episode_id, identity)


@router.post("/learning-episodes/{episode_id}/follow-up", tags=["episodes"])
def link_episode_follow_up(
    episode_id: str, payload: EpisodeFollowUpLink, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.link_episode_follow_up(episode_id, payload.extractionId, identity)


@router.post("/learning-episodes/{episode_id}/outcome", tags=["episodes"])
def assess_episode(
    episode_id: str,
    payload: EpisodeOutcomeCreate,
    service: Service,
    identity: Writer,
) -> dict[str, Any]:
    return service.assess_episode(episode_id, payload.model_dump(), identity)


@router.get("/learning-episodes/{episode_id}", tags=["episodes"])
def episode(episode_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.get("episode", episode_id)


@router.get("/water-references", tags=["water-references"])
def water_references(
    service: Service,
    _: IdentityDep,
    page: int | None = Query(None, ge=1),
    page_size: int = Query(25, ge=1, le=100),
) -> dict[str, Any]:
    return _list(service, "water-reference", page, page_size, None)


@router.get("/water-references/export", tags=["water-references"])
def export_water_references(service: Service, _: IdentityDep) -> Response:
    """Pares cuerpo–agua en CSV, para ajustar la transformación de ADR-019."""
    stream = io.StringIO()
    writer = csv.writer(stream)
    columns = service.WATER_PAIR_COLUMNS
    writer.writerow(columns)
    for row in service.water_reference_pairs():
        writer.writerow(["" if row[key] is None else row[key] for key in columns])
    return Response(
        stream.getvalue(),
        media_type="text/csv",
        headers={"Content-Disposition": "attachment; filename=pares-cuerpo-agua-v1.csv"},
    )


@router.post(
    "/water-references", tags=["water-references"], status_code=status.HTTP_201_CREATED
)
def record_water_reference(
    payload: WaterReferenceReading, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.record_water_reference(payload.model_dump(mode="json"), identity)


@router.get("/calibrations", tags=["calibrations"])
def calibrations(
    service: Service,
    _: IdentityDep,
    page: int | None = Query(None, ge=1),
    page_size: int = Query(25, ge=1, le=100),
) -> dict[str, Any]:
    return _list(service, "calibration", page, page_size, None)


@router.post("/calibrations", tags=["calibrations"], status_code=status.HTTP_201_CREATED)
def start_calibration(
    payload: CalibrationStart, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.start_calibration(payload.model_dump(), identity)


@router.get("/calibrations/{calibration_id}", tags=["calibrations"])
def calibration(calibration_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.get("calibration", calibration_id)


@router.post("/calibrations/{calibration_id}/tests", tags=["calibrations"])
def add_calibration_test(
    calibration_id: str,
    payload: CalibrationTestAdd,
    service: Service,
    identity: Writer,
) -> dict[str, Any]:
    return service.add_calibration_test(calibration_id, payload.model_dump(), identity)


@router.post("/calibrations/{calibration_id}/adjustments", tags=["calibrations"])
def add_calibration_adjustment(
    calibration_id: str,
    payload: CalibrationAdjustmentAdd,
    service: Service,
    identity: Writer,
) -> dict[str, Any]:
    return service.add_calibration_adjustment(calibration_id, payload.model_dump(), identity)


@router.post("/calibrations/{calibration_id}/close", tags=["calibrations"])
def close_calibration(
    calibration_id: str,
    payload: CalibrationClose,
    service: Service,
    identity: Writer,
) -> dict[str, Any]:
    return service.close_calibration(calibration_id, payload.model_dump(), identity)


@router.get("/profiles", tags=["profiles"])
def profiles(
    request: Request,
    service: Service,
    identity: IdentityDep,
    page: int | None = Query(None, ge=1),
) -> dict[str, Any]:
    result = service.visible_profiles(
        identity, request.app.state.settings.shared_profile_id, page or 1, 100
    )
    return result if page is not None else {"items": result["items"]}


@router.post("/profiles", tags=["profiles"], status_code=status.HTTP_201_CREATED)
def create_profile(
    payload: ProfileCreate | LegacyProfileCreate, service: Service, identity: Writer
) -> dict[str, Any]:
    if isinstance(payload, LegacyProfileCreate):
        proposed = {**payload.model_dump(), "status": "proposed"}
        return service.create("profile", proposed, identity)
    return service.create_profile(payload.model_dump(), identity)


@router.post("/profiles/compare", tags=["profiles"])
def compare_profiles(
    payload: ProfileCompareRequest, service: Service, _: IdentityDep
) -> dict[str, Any]:
    return service.compare_profiles(payload.ids)


@router.get("/profiles/{profile_id}", tags=["profiles"])
def profile(profile_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.get("profile", profile_id)


@router.put("/profiles/{profile_id}", tags=["profiles"])
def update_profile(
    profile_id: str, payload: ProfileUpdate, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.update_profile(profile_id, payload.model_dump(), identity)


@router.post(
    "/profiles/{profile_id}/versions",
    tags=["profiles"],
    status_code=status.HTTP_201_CREATED,
)
def create_profile_version(
    profile_id: str, payload: ProfileUpdate, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.create_profile_version(profile_id, payload.model_dump(), identity)


@router.post("/profiles/{profile_id}/approve", tags=["profiles"])
def approve_profile(profile_id: str, service: Service, identity: Writer) -> dict[str, Any]:
    return service.approve_profile(profile_id, identity)


@router.post("/profiles/{profile_id}/activate", tags=["profiles"])
def activate_profile(
    profile_id: str,
    service: Service,
    identity: Writer,
    payload: ProfileActivation | None = None,
) -> dict[str, Any]:
    """Pone una configuración a regir en la máquina; no inicia ninguna preparación."""
    return service.activate_profile(
        profile_id, identity, payload.model_dump() if payload else None
    )


@router.post("/profiles/{profile_id}/tolerance-from-extractions", tags=["profiles"])
def tolerance_from_extractions(
    profile_id: str, payload: ToleranceFromExtractions, service: Service, identity: Writer
) -> dict[str, Any]:
    """Propone, o fija y pone a regir, la tolerancia que dan cafés repetidos."""
    return service.tolerance_from_extractions(
        profile_id, payload.extractionIds, identity, apply=payload.apply
    )


@router.post("/profiles/{profile_id}/retire", tags=["profiles"])
def retire_profile(profile_id: str, service: Service, identity: Writer) -> dict[str, Any]:
    return service.retire_profile(profile_id, identity)


@router.put("/profiles/{profile_id}/sensory-target", tags=["profiles", "sensory"])
def profile_sensory_target(
    profile_id: str,
    payload: ProfileSensoryTarget,
    service: Service,
    identity: Writer,
) -> dict[str, Any]:
    return service.set_profile_sensory_target(profile_id, payload.model_dump(), identity)


@router.get("/recommendations", tags=["recommendations"])
def recommendations(
    service: Service,
    _: IdentityDep,
    page: int = Query(1, ge=1),
    page_size: int = Query(25, ge=1, le=100),
) -> dict[str, Any]:
    return service.list("recommendation", page, page_size)


@router.post("/recommendations", tags=["recommendations"], status_code=status.HTTP_201_CREATED)
def create_recommendation(
    payload: RecommendationCreate, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.recommend(payload.extractionId, identity)


@router.get("/recommendations/{recommendation_id}", tags=["recommendations"])
def recommendation(recommendation_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.get("recommendation", recommendation_id)


@router.post("/recommendations/{recommendation_id}/decision", tags=["recommendations"])
def decision(
    recommendation_id: str, payload: RecommendationDecision, service: Service, identity: Writer
) -> dict[str, Any]:
    return service.decide(recommendation_id, payload.model_dump(), identity)


@router.post("/recommendations/{recommendation_id}/explanation", tags=["explanations"])
def explanation(recommendation_id: str, service: Service, _: IdentityDep) -> dict[str, Any]:
    return service.explain(recommendation_id)
