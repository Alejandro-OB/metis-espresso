FROM python:3.12.11-slim-trixie AS builder
ENV PIP_DISABLE_PIP_VERSION_CHECK=1 PIP_NO_CACHE_DIR=1
WORKDIR /build
COPY platform/web/backend/requirements.lock .
RUN python -m venv /venv && /venv/bin/pip install --requirement requirements.lock

FROM python:3.12.11-slim-trixie AS test-builder
ENV PIP_DISABLE_PIP_VERSION_CHECK=1 PIP_NO_CACHE_DIR=1
WORKDIR /build
COPY platform/web/backend/requirements.lock platform/web/backend/requirements-test.lock ./
RUN python -m venv /venv && /venv/bin/pip install --requirement requirements-test.lock

FROM python:3.12.11-slim-trixie AS runtime
ENV PATH="/venv/bin:$PATH" PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1
RUN groupadd --system --gid 10001 app \
    && useradd --system --uid 10001 --gid app --home-dir /workspace app \
    && mkdir -p /data/simulations \
    && chown -R app:app /data
COPY --from=builder /venv /venv
WORKDIR /workspace/platform/web/backend
COPY platform/web/backend/alembic.ini ./
COPY platform/web/backend/alembic ./alembic
COPY platform/web/backend/src ./src
COPY platform/packages /workspace/platform/packages
COPY model /workspace/model
COPY ontology /workspace/ontology
COPY platform/contracts /workspace/platform/contracts
ENV PYTHONPATH=/workspace/platform/web/backend/src:/workspace/platform/packages APP_PROJECT_ROOT=/workspace
USER app
EXPOSE 8000
CMD ["uvicorn", "aplicacion_web.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]

FROM python:3.12.11-slim-trixie AS test
ENV PATH="/venv/bin:$PATH" PYTHONPATH=/workspace/platform/web/backend/src:/workspace/platform/packages APP_ENVIRONMENT=test APP_PROJECT_ROOT=/workspace
COPY --from=test-builder /venv /venv
WORKDIR /workspace/platform/web/backend
COPY platform/web/backend/src ./src
COPY platform/web/backend/tests ./tests
COPY platform/packages /workspace/platform/packages
COPY model /workspace/model
COPY ontology /workspace/ontology
COPY platform/contracts /workspace/platform/contracts
CMD ["pytest"]
