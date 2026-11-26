from functools import lru_cache
from pathlib import Path

from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="APP_", env_file=".env", extra="ignore")

    environment: str = "development"
    database_url: str = "sqlite+pysqlite:///./aplicacion-web.sqlite3"
    cors_origins: list[str] = ["http://localhost:5173"]
    max_request_bytes: int = 1_048_576
    seed_demo: bool = False
    viewer_token: str = "dev-viewer-token"
    barista_token: str = "dev-barista-token"
    admin_token: str = "dev-admin-token"
    session_cookie_name: str = "espresso_session"
    session_hours: int = 12
    initial_barista_username: str = "barista"
    initial_barista_password: str | None = None
    initial_barista_name: str = "Barista de laboratorio"
    initial_admin_username: str = "admin"
    initial_admin_password: str | None = None
    initial_admin_name: str = "Administración"
    # El único perfil que ven todos los baristas (pedido del usuario del
    # 3-nov-2026); los demás solo los ve quien los creó. Es el que la
    # administración creó el 23-oct en la plataforma desplegada como punto de
    # partida. No se marca en el propio perfil porque no hay pantalla para
    # marcarlo, y una marca sin pantalla quedaría puesta a mano en la base.
    shared_profile_id: str = "default-profile"
    project_root: Path = Path(__file__).resolve().parents[5]
    model_registry_path: Path | None = None
    simulation_artifact_root: Path = Path("./simulation-data")
    simulation_max_duration_seconds: float = 900.0
    simulation_artifact_max_bytes: int = 10_000_000
    simulation_worker_poll_seconds: float = 1.0
    simulation_worker_lease_seconds: int = 30
    simulation_worker_timeout_seconds: float = 120.0
    fuseki_dataset_url: str = "http://rdf-store:3030/espresso"
    fuseki_user: str | None = "admin"
    fuseki_password: str | None = None
    semantic_worker_poll_seconds: float = 1.0
    semantic_manifest_path: Path | None = None

    @property
    def resolved_model_registry_path(self) -> Path:
        return (
            self.model_registry_path or self.project_root / "model/model-registry-v1.json"
        )

    @property
    def resolved_semantic_manifest_path(self) -> Path:
        return self.semantic_manifest_path or (
            self.project_root / "ontology/semantic-manifest-v1.json"
        )

    @field_validator("environment")
    @classmethod
    def validate_environment(cls, value: str) -> str:
        if value not in {"development", "test", "production"}:
            raise ValueError("environment no soportado")
        return value


@lru_cache
def get_settings() -> Settings:
    return Settings()
