from __future__ import annotations

from contextlib import asynccontextmanager
from uuid import uuid4

from espresso_semantics import FusekiClient, SemanticManifest
from espresso_simulation import FileSeriesStore, ModelRegistry
from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from starlette.exceptions import HTTPException as StarletteHTTPException

from aplicacion_web.adapters.database import Base, SqlRepository, build_database
from aplicacion_web.adapters.reasoner import ReasonerRecommendationAdapter
from aplicacion_web.adapters.security import Argon2PasswordHasher
from aplicacion_web.adapters.stubs import (
    DeterministicExplanationAdapter,
    FusekiSemanticAdapter,
    SemanticServiceUnavailable,
    TraceableSemanticAdapter,
)
from aplicacion_web.application.auth import AuthService
from aplicacion_web.application.services import EKO_VERSION, ApplicationService
from aplicacion_web.config import get_settings
from aplicacion_web.domain.errors import (
    AuthenticationError,
    DomainError,
    ForbiddenError,
    NotFoundError,
)
from aplicacion_web.presentation.routers import (
    admin_router,
    auth_router,
    resources_router,
    system_router,
)
from aplicacion_web.presentation.validation import actionable_validation_errors
from aplicacion_web.seed import seed_demo


@asynccontextmanager
async def lifespan(app: FastAPI):
    settings = get_settings()
    engine, session_factory = build_database(settings.database_url)
    if settings.environment in {"development", "test"}:
        Base.metadata.create_all(engine)
    repository = SqlRepository(session_factory)
    model_registry = ModelRegistry(settings.resolved_model_registry_path, settings.project_root)
    series_store = FileSeriesStore(
        settings.simulation_artifact_root, settings.simulation_artifact_max_bytes
    )
    semantic_manifest = SemanticManifest(
        settings.resolved_semantic_manifest_path, settings.project_root
    )
    semantic_manifest.verify()
    fuseki = FusekiClient(
        settings.fuseki_dataset_url,
        settings.fuseki_user,
        settings.fuseki_password,
    )
    semantics = (
        TraceableSemanticAdapter(repository)
        if settings.environment == "test"
        else FusekiSemanticAdapter(fuseki)
    )
    app.state.settings = settings
    app.state.engine = engine
    app.state.session_factory = session_factory
    app.state.repository = repository
    app.state.semantic_manifest = semantic_manifest
    app.state.fuseki = fuseki
    app.state.auth_service = AuthService(repository, Argon2PasswordHasher(), settings.session_hours)
    app.state.service = ApplicationService(
        repository,
        semantics,
        ReasonerRecommendationAdapter(EKO_VERSION),
        DeterministicExplanationAdapter(),
        model_registry,
        series_store,
        settings.simulation_max_duration_seconds,
    )
    if settings.seed_demo:
        seed_demo(repository)
    if settings.initial_barista_password:
        app.state.auth_service.ensure_user(
            "user-barista",
            settings.initial_barista_username,
            settings.initial_barista_name,
            settings.initial_barista_password,
            "barista",
        )
    if settings.initial_admin_password:
        app.state.auth_service.ensure_user(
            "user-admin",
            settings.initial_admin_username,
            settings.initial_admin_name,
            settings.initial_admin_password,
            "admin",
        )
    yield
    engine.dispose()


app = FastAPI(
    title="Aplicación web espresso API",
    summary="Consulta, evidencia y decisión humana sin autoridad sobre actuadores.",
    version="0.1.0",
    lifespan=lifespan,
    openapi_url="/api/openapi.json",
    docs_url="/api/docs",
)
settings = get_settings()
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origins,
    allow_credentials=True,
    allow_methods=["GET", "POST", "PUT", "OPTIONS"],
    allow_headers=[
        "Authorization",
        "Content-Type",
        "Idempotency-Key",
        "X-Correlation-ID",
        "X-Test-Role",
        "X-Test-Actor",
    ],
)


@app.middleware("http")
async def correlation_and_audit(request: Request, call_next):
    correlation_id = request.headers.get("X-Correlation-ID") or str(uuid4())
    request.state.correlation_id = correlation_id
    content_length = request.headers.get("Content-Length")
    request_size = int(content_length) if content_length and content_length.isdecimal() else 0
    if request_size > settings.max_request_bytes:
        response = JSONResponse(
            status_code=413,
            content={
                "error": {
                    "code": "request_too_large",
                    "message": "La solicitud supera el tamaño permitido.",
                    "fields": [],
                    "correlationId": correlation_id,
                }
            },
        )
    else:
        response = await call_next(request)
    response.headers["X-Correlation-ID"] = correlation_id
    if request.method in {"POST", "PUT", "PATCH", "DELETE"} and hasattr(
        request.app.state, "repository"
    ):
        current_identity = getattr(request.state, "identity", None)
        actor = current_identity.subject if current_identity else "unauthenticated"
        request.app.state.repository.audit(
            actor,
            f"{request.method} {request.url.path}",
            request.url.path,
            str(response.status_code),
            correlation_id,
        )
    return response


@app.exception_handler(DomainError)
async def domain_error(request: Request, error: DomainError) -> JSONResponse:
    status_code = 400
    if isinstance(error, NotFoundError):
        status_code = 404
    elif isinstance(error, AuthenticationError):
        status_code = 401
    elif isinstance(error, ForbiddenError):
        status_code = 403
    elif error.code == "conflict":
        status_code = 409
    return JSONResponse(
        status_code=status_code,
        content={
            "error": {
                "code": error.code,
                "message": str(error),
                "fields": [],
                "correlationId": request.state.correlation_id,
            }
        },
    )


@app.exception_handler(SemanticServiceUnavailable)
async def semantic_unavailable(
    request: Request, error: SemanticServiceUnavailable
) -> JSONResponse:
    """503, no 200 con lista vacía: no poder consultar no es no tener hechos."""
    return JSONResponse(
        status_code=503,
        content={
            "error": {
                "code": "semantic_service_unavailable",
                "message": str(error),
                "fields": [],
                "correlationId": request.state.correlation_id,
            }
        },
    )


@app.exception_handler(RequestValidationError)
async def request_validation_error(request: Request, error: RequestValidationError) -> JSONResponse:
    message, fields = actionable_validation_errors(error.errors())
    return JSONResponse(
        status_code=422,
        content={
            "error": {
                "code": "validation_error",
                "message": message,
                "fields": fields,
                "correlationId": request.state.correlation_id,
            }
        },
    )


@app.exception_handler(StarletteHTTPException)
async def http_error(request: Request, error: StarletteHTTPException) -> JSONResponse:
    return JSONResponse(
        status_code=error.status_code,
        content={
            "error": {
                "code": "not_found" if error.status_code == 404 else "http_error",
                "message": str(error.detail),
                "fields": [],
                "correlationId": request.state.correlation_id,
            }
        },
    )


app.include_router(system_router)
app.include_router(auth_router)
app.include_router(admin_router)
app.include_router(resources_router)
