import os
import tempfile
from pathlib import Path
from uuid import uuid4

import pytest
from fastapi.testclient import TestClient

database_file = Path(tempfile.gettempdir()) / f"aplicacion-web-test-{uuid4().hex}.sqlite3"
os.environ["APP_ENVIRONMENT"] = "test"
os.environ["APP_DATABASE_URL"] = f"sqlite+pysqlite:///{database_file}"
os.environ["APP_SEED_DEMO"] = "false"
os.environ["APP_INITIAL_BARISTA_PASSWORD"] = "espresso-barista"
os.environ["APP_INITIAL_ADMIN_PASSWORD"] = "espresso-admin"
os.environ["APP_PROJECT_ROOT"] = str(Path(__file__).resolve().parents[4])
os.environ["APP_SIMULATION_ARTIFACT_ROOT"] = str(
    Path(tempfile.gettempdir()) / f"aplicacion-web-series-{uuid4().hex}"
)

from aplicacion_web.adapters.database import (  # noqa: E402
    AuditRecord,
    Document,
    Message,
    SimulationArtifactRecord,
    SimulationRunRecord,
)
from aplicacion_web.main import app  # noqa: E402
from aplicacion_web.seed import seed_demo  # noqa: E402


@pytest.fixture()
def client():
    with TestClient(app) as test_client:
        with app.state.session_factory.begin() as session:
            session.query(AuditRecord).delete()
            session.query(Message).delete()
            session.query(Document).delete()
            session.query(SimulationArtifactRecord).delete()
            session.query(SimulationRunRecord).delete()
        seed_demo(app.state.repository)
        yield test_client


@pytest.fixture()
def barista_headers():
    return {"X-Test-Role": "barista", "X-Test-Actor": "barista-test"}
