FROM python:3.12.11-slim-trixie AS builder
ENV PIP_DISABLE_PIP_VERSION_CHECK=1 PIP_NO_CACHE_DIR=1
WORKDIR /build
COPY apps/web/backend/requirements.lock .
RUN python -m venv /venv && /venv/bin/pip install --requirement requirements.lock

FROM python:3.12.11-slim-trixie AS test-builder
ENV PIP_DISABLE_PIP_VERSION_CHECK=1 PIP_NO_CACHE_DIR=1
WORKDIR /build
COPY apps/web/backend/requirements.lock apps/web/backend/requirements-test.lock ./
RUN python -m venv /venv && /venv/bin/pip install --requirement requirements-test.lock

FROM python:3.12.11-slim-trixie AS runtime
ENV PATH="/venv/bin:$PATH" PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1
RUN groupadd --system --gid 10001 app \
    && useradd --system --uid 10001 --gid app --home-dir /workspace app \
    && mkdir -p /data/simulations \
    && chown -R app:app /data
COPY --from=builder /venv /venv
WORKDIR /workspace/apps/web/backend
COPY apps/web/backend/alembic.ini ./
COPY apps/web/backend/alembic ./alembic
COPY apps/web/backend/src ./src
COPY packages /workspace/packages
COPY models/modelica /workspace/models/modelica
COPY knowledge/ontology /workspace/knowledge/ontology
COPY contracts /workspace/contracts
ENV PYTHONPATH=/workspace/apps/web/backend/src:/workspace/packages APP_PROJECT_ROOT=/workspace
USER app
EXPOSE 8000
CMD ["uvicorn", "aplicacion_web.main:app", "--host", "0.0.0.0", "--port", "8000", "--workers", "1"]

FROM python:3.12.11-slim-trixie AS test
ENV PATH="/venv/bin:$PATH" PYTHONPATH=/workspace/apps/web/backend/src:/workspace/packages APP_ENVIRONMENT=test APP_PROJECT_ROOT=/workspace
COPY --from=test-builder /venv /venv
WORKDIR /workspace/apps/web/backend
COPY apps/web/backend/src ./src
COPY apps/web/backend/tests ./tests
COPY packages /workspace/packages
COPY models/modelica /workspace/models/modelica
COPY knowledge/ontology /workspace/knowledge/ontology
COPY contracts /workspace/contracts
CMD ["pytest"]
