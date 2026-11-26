from __future__ import annotations

from collections.abc import Iterator
from datetime import datetime, timedelta
from typing import Any

from sqlalchemy import (
    JSON,
    Boolean,
    DateTime,
    ForeignKey,
    Integer,
    String,
    Text,
    UniqueConstraint,
    cast,
    create_engine,
    delete,
    func,
    select,
)
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column, sessionmaker

from aplicacion_web.application.ports import Repository
from aplicacion_web.domain.errors import ConflictError
from aplicacion_web.domain.models import (
    Page,
    UserIdentity,
    has_publishable_period,
    utc_instant_text,
    utc_now,
)


class Base(DeclarativeBase):
    pass


class Document(Base):
    __tablename__ = "documents"

    kind: Mapped[str] = mapped_column(String(40), primary_key=True)
    identifier: Mapped[str] = mapped_column(String(80), primary_key=True)
    payload: Mapped[dict[str, Any]] = mapped_column(JSON, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now, onupdate=utc_now
    )


class Message(Base):
    __tablename__ = "messages"

    message_id: Mapped[str] = mapped_column(String(80), primary_key=True)
    message_type: Mapped[str] = mapped_column(String(60), nullable=False, index=True)
    device_id: Mapped[str] = mapped_column(String(120), nullable=False, index=True)
    sequence: Mapped[int] = mapped_column(Integer, nullable=False)
    occurred_at: Mapped[str] = mapped_column(String(40), nullable=False)
    payload: Mapped[dict[str, Any]] = mapped_column(JSON, nullable=False)
    received_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now
    )


class AuditRecord(Base):
    __tablename__ = "audit_records"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    actor: Mapped[str] = mapped_column(String(120), nullable=False, index=True)
    operation: Mapped[str] = mapped_column(String(120), nullable=False)
    resource: Mapped[str] = mapped_column(String(160), nullable=False)
    result: Mapped[str] = mapped_column(String(40), nullable=False)
    correlation_id: Mapped[str] = mapped_column(String(80), nullable=False, index=True)
    occurred_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now
    )


class UserAccount(Base):
    __tablename__ = "user_accounts"

    id: Mapped[str] = mapped_column(String(80), primary_key=True)
    username: Mapped[str] = mapped_column(String(80), nullable=False, unique=True, index=True)
    display_name: Mapped[str] = mapped_column(String(120), nullable=False)
    password_hash: Mapped[str] = mapped_column(Text, nullable=False)
    role: Mapped[str] = mapped_column(String(20), nullable=False)
    active: Mapped[bool] = mapped_column(Boolean, nullable=False, default=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now, onupdate=utc_now
    )


class UserSession(Base):
    __tablename__ = "user_sessions"

    token_hash: Mapped[str] = mapped_column(String(64), primary_key=True)
    user_id: Mapped[str] = mapped_column(
        ForeignKey("user_accounts.id", ondelete="CASCADE"), nullable=False, index=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now
    )
    expires_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, index=True
    )
    revoked_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)


class SimulationRunRecord(Base):
    __tablename__ = "simulation_runs"
    __table_args__ = (
        UniqueConstraint("requested_by", "idempotency_key", name="uq_simulation_idempotency"),
    )

    id: Mapped[str] = mapped_column(String(80), primary_key=True)
    requested_by: Mapped[str] = mapped_column(String(120), nullable=False, index=True)
    idempotency_key: Mapped[str] = mapped_column(String(120), nullable=False)
    request_hash: Mapped[str] = mapped_column(String(64), nullable=False)
    status: Mapped[str] = mapped_column(String(20), nullable=False, index=True)
    semantic_status: Mapped[str] = mapped_column(String(20), nullable=False, index=True)
    payload: Mapped[dict[str, Any]] = mapped_column(JSON, nullable=False)
    cancellation_requested: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    worker_id: Mapped[str | None] = mapped_column(String(120), nullable=True)
    lease_expires_at: Mapped[datetime | None] = mapped_column(
        DateTime(timezone=True), nullable=True
    )
    heartbeat_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now, onupdate=utc_now
    )


class SimulationArtifactRecord(Base):
    __tablename__ = "simulation_artifacts"

    id: Mapped[str] = mapped_column(String(100), primary_key=True)
    simulation_run_id: Mapped[str] = mapped_column(
        ForeignKey("simulation_runs.id", ondelete="CASCADE"), nullable=False, unique=True
    )
    manifest: Mapped[dict[str, Any]] = mapped_column(JSON, nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=utc_now
    )


def sample_quality(temperature: dict[str, Any], pressure: dict[str, Any]) -> str:
    """Calidad de la muestra completa, que el comparador exige como un solo valor.

    Una muestra solo es utilizable si ambos canales lo son: basta que uno no sea
    válido para que la muestra no pueda contribuir a una comparación. La calidad
    de cada canal se conserva aparte, en ``channelQuality``.
    """
    qualities = {temperature.get("quality"), pressure.get("quality")}
    qualities.discard(None)
    if qualities == {"valid"}:
        return "valid"
    if "simulated" in qualities:
        return "simulated"
    return "invalid"


class SqlRepository(Repository):
    def __init__(self, session_factory: sessionmaker[Session]) -> None:
        self.session_factory = session_factory

    def _session(self) -> Iterator[Session]:
        with self.session_factory() as session:
            yield session

    def add_document(self, kind: str, identifier: str, document: dict[str, Any]) -> dict[str, Any]:
        with self.session_factory.begin() as session:
            session.add(Document(kind=kind, identifier=identifier, payload=document))
            try:
                session.flush()
            except IntegrityError as error:
                raise ConflictError(f"{kind} {identifier} ya existe") from error
        return document

    def upsert_document(
        self, kind: str, identifier: str, document: dict[str, Any]
    ) -> dict[str, Any]:
        with self.session_factory.begin() as session:
            record = session.get(Document, (kind, identifier))
            if record is None:
                session.add(Document(kind=kind, identifier=identifier, payload=document))
            else:
                record.payload = document
                record.updated_at = utc_now()
        return document

    def get_document(self, kind: str, identifier: str) -> dict[str, Any] | None:
        with self.session_factory() as session:
            record = session.get(Document, (kind, identifier))
            return None if record is None else dict(record.payload)

    def list_documents(
        self,
        kind: str,
        *,
        page: int = 1,
        page_size: int = 25,
        query: str | None = None,
        filters: dict[str, str] | None = None,
    ) -> Page:
        condition = Document.kind == kind
        if query:
            pattern = f"%{query.lower()}%"
            condition = condition & func.lower(cast(Document.payload, Text)).like(pattern)
        for field, value in (filters or {}).items():
            if field == "finishedAtBefore":
                condition = condition & (Document.payload["finishedAt"].as_string() < value)
            elif field == "finishedAtFrom":
                condition = condition & (Document.payload["finishedAt"].as_string() >= value)
            elif field == "startedAtBefore":
                # La preparación anterior a otra se busca por su pulsado, no
                # entre las 50 más recientes del almacén (8-nov-2026).
                condition = condition & (Document.payload["startedAt"].as_string() < value)
            else:
                condition = condition & (Document.payload[field].as_string() == value)
        with self.session_factory() as session:
            total = session.scalar(select(func.count()).select_from(Document).where(condition)) or 0
            records = session.scalars(
                select(Document)
                .where(condition)
                .order_by(Document.created_at.desc())
                .offset((page - 1) * page_size)
                .limit(page_size)
            ).all()
        return Page([dict(record.payload) for record in records], page, page_size, total)

    def ingest_message(self, envelope: dict[str, Any]) -> bool:
        with self.session_factory.begin() as session:
            if session.get(Message, envelope["messageId"]) is not None:
                return False
            session.add(
                Message(
                    message_id=envelope["messageId"],
                    message_type=envelope["messageType"],
                    device_id=envelope["deviceId"],
                    sequence=envelope["sequence"],
                    occurred_at=envelope["occurredAt"],
                    payload=envelope["payload"],
                )
            )
            message_type = envelope["messageType"]
            payload = dict(envelope["payload"])
            if message_type == "telemetry.sample":
                temperature = payload.get("temperature") or {}
                pressure = payload.get("pressure") or {}
                if "measurements" not in payload and (temperature or pressure):
                    payload["measurements"] = {
                        "temperatureC": temperature.get("value"),
                        "pressureBar": pressure.get("value"),
                    }
                if temperature or pressure:
                    payload["channelQuality"] = {
                        "temperature": temperature.get("quality"),
                        "pressure": pressure.get("quality"),
                    }
                identifier = envelope["messageId"]
                # La muestra se archiva como parte de la serie de su preparación:
                # es la serie medida contra la que se compara el modelo virtual
                # (ADR-012 § 4.1). El número de orden viaja en el sobre y la
                # procedencia distingue una medición de una muestra generada por
                # el simulador de la propia aplicación.
                payload.update(
                    {
                        "id": identifier,
                        "deviceId": envelope["deviceId"],
                        "occurredAt": envelope["occurredAt"],
                        "sequence": envelope["sequence"],
                        "extractionId": payload.get("preparationId"),
                        # El instante normalizado a UTC y ancho fijo permite
                        # buscar muestras por intervalo comparando texto, y es
                        # lo que empareja una lectura de referencia del agua con
                        # el cuerpo (ADR-019 § 4.3). Solo lo llevan las muestras
                        # de telemetría: las de la serie comparten el instante
                        # del sobre y no tienen uno propio.
                        "occurredAtUtc": utc_instant_text(envelope["occurredAt"]),
                        "temperatureC": temperature.get("value"),
                        "pressureBar": pressure.get("value"),
                        "quality": sample_quality(temperature, pressure),
                        # Sin origen declarado la muestra no es una medición:
                        # afirmar lo contrario convertiría un dato incompleto en
                        # evidencia física.
                        "source": payload.get("source") or "unknown",
                    }
                )
                session.add(Document(kind="live-sample", identifier=identifier, payload=payload))
            elif message_type == "extraction.completed":
                identifier = str(payload["id"])
                # La misma preparación puede llegar otra vez con otro mensaje, por
                # ejemplo si el dispositivo se reinició antes de recibir el acuse.
                # La primera queda como evidencia y no se reescribe; el mensaje se
                # conserva en la bitácora y se cuenta como duplicado. Antes, la
                # segunda rompía la unicidad y la sincronización entera fallaba,
                # perdiendo también los demás mensajes del lote.
                if session.get(Document, ("extraction", identifier)) is not None:
                    return False
                payload.setdefault("deviceId", envelope["deviceId"])
                payload.setdefault("ingestedAt", envelope["occurredAt"])
                session.add(Document(kind="extraction", identifier=identifier, payload=payload))
            elif message_type == "extraction.series":
                self._ingest_series_chunk(session, envelope, payload)
            elif message_type == "operational.event":
                identifier = envelope["messageId"]
                payload.update({"id": identifier, "occurredAt": envelope["occurredAt"]})
                session.add(Document(kind="event", identifier=identifier, payload=payload))
            elif message_type == "profile.proposal":
                identifier = str(payload["id"])
                record = session.get(Document, ("profile", identifier))
                if record is None:
                    session.add(Document(kind="profile", identifier=identifier, payload=payload))
                else:
                    record.payload = payload
        return True

    @staticmethod
    def _ingest_series_chunk(
        session: Session, envelope: dict[str, Any], payload: dict[str, Any]
    ) -> None:
        """Archiva un tramo de la serie medida de una preparación terminada.

        La serie llega al cerrar la preparación y **sustituye** a las muestras
        que la telemetría en vivo hubiera dejado de esa misma preparación. Son
        dos cosas distintas que hasta ahora compartían sitio: la telemetría
        observa el proceso mientras ocurre, con la cadencia que pide la pantalla
        y perdiéndose si no hay enlace; la serie es la evidencia sobre la que se
        compara el modelo, a la rejilla que declara el plan de comparación, y
        sobrevive a un corte. Conservar ambas dejaría dos versiones de lo mismo
        y un recuento que no cuadra con lo que declara el resumen.

        Las muestras propias de la serie se reconocen por su identificador, de
        modo que el reemplazo funcione aunque los tramos lleguen desordenados:
        cada tramo borra lo provisional y nunca lo que otro tramo ya archivó.
        """
        preparation_id = str(payload["preparationId"])
        provisional = session.scalars(
            select(Document).where(
                Document.kind == "live-sample",
                Document.payload["extractionId"].as_string() == preparation_id,
            )
        ).all()
        for record in provisional:
            if not str(record.identifier).startswith("series-"):
                session.delete(record)

        source = payload.get("source") or "unknown"
        for offset, sample in enumerate(payload.get("samples") or []):
            temperature = sample.get("temperature") or {}
            pressure = sample.get("pressure") or {}
            elapsed_ms = int(sample["elapsedMs"])
            session.add(
                Document(
                    kind="live-sample",
                    identifier=f"{envelope['messageId']}-{offset:02d}",
                    payload={
                        "id": f"{envelope['messageId']}-{offset:02d}",
                        "deviceId": envelope["deviceId"],
                        "extractionId": preparation_id,
                        "occurredAt": envelope["occurredAt"],
                        # El orden de la serie es el del proceso, y el instante
                        # relativo es lo único monótono que traen las muestras:
                        # el número de secuencia del sobre es del mensaje, no de
                        # la muestra, y se repetiría dentro de cada tramo.
                        "sequence": elapsed_ms,
                        "elapsedMs": elapsed_ms,
                        "temperatureC": temperature.get("value"),
                        "pressureBar": pressure.get("value"),
                        "quality": sample_quality(temperature, pressure),
                        "channelQuality": {
                            "temperature": temperature.get("quality"),
                            "pressure": pressure.get("quality"),
                        },
                        "source": source,
                    },
                )
            )

    def count_device_samples_between(self, device_id: str, start_utc: str, end_utc: str) -> int:
        """Cuántas muestras de la placa hay en el intervalo, sin leerlas.

        Es la huella barata de `device_samples_between`: si no cambió, lo que
        se calculó con esas muestras sigue valiendo.
        """
        condition = (
            (Document.kind == "live-sample")
            & (Document.payload["deviceId"].as_string() == device_id)
            & (Document.payload["occurredAtUtc"].as_string() >= start_utc)
            & (Document.payload["occurredAtUtc"].as_string() <= end_utc)
        )
        with self.session_factory() as session:
            return int(
                session.scalar(select(func.count()).select_from(Document).where(condition)) or 0
            )

    def device_samples_between(
        self, device_id: str, start_utc: str, end_utc: str
    ) -> list[dict[str, Any]]:
        condition = (
            (Document.kind == "live-sample")
            & (Document.payload["deviceId"].as_string() == device_id)
            & (Document.payload["occurredAtUtc"].as_string() >= start_utc)
            & (Document.payload["occurredAtUtc"].as_string() <= end_utc)
        )
        with self.session_factory() as session:
            records = session.scalars(select(Document).where(condition)).all()
        return [dict(record.payload) for record in records]

    def latest_machine_telemetry(self) -> dict[str, Any] | None:
        """La última telemetría de estado de la placa.

        `latest_telemetry` devuelve la última muestra de cualquier clase, y una
        serie de preparación que llega en bloque la tapa: esas muestras no
        llevan el estado de la máquina.
        """
        with self.session_factory() as session:
            record = session.scalar(
                select(Document)
                .where(Document.kind == "live-sample")
                .where(Document.payload["state"].as_string().is_not(None))
                .order_by(Document.created_at.desc())
                .limit(1)
            )
            if record is None:
                return None
            # Cuándo llegó, con el reloj de la plataforma: el de la placa puede
            # estar corrido (26-oct-2026, 10 h atrás al arrancar).
            return {**dict(record.payload), "receivedAtUtc": utc_instant_text(record.created_at)}

    def preparation_telemetry(self, preparation_id: str) -> list[dict[str, Any]]:
        """Las muestras de estado que la placa mandó durante una preparación.

        Solo las de telemetría (llevan `state`): la serie que llega al terminar
        repite la misma preparación y se lee aparte, con su propia ruta.
        """
        condition = (
            (Document.kind == "live-sample")
            & (Document.payload["extractionId"].as_string() == preparation_id)
            & (Document.payload["state"].as_string().is_not(None))
        )
        with self.session_factory() as session:
            records = session.scalars(select(Document).where(condition)).all()
        return [dict(record.payload) for record in records]

    def machine_state_entered_at(self, state: str) -> dict[str, Any] | None:
        """Desde cuándo dice la telemetría que la máquina está en `state`.

        Devuelve el instante de la primera muestra con ese estado después de la
        última que tenía otro, y la de antes (`previous`), que acota el cambio.
        Sin una muestra anterior distinta, el estado empezó antes de lo que hay
        archivado y `previous` es None: el instante es una cota, no el cambio.
        """
        with_state = (Document.kind == "live-sample") & (
            Document.payload["state"].as_string().is_not(None)
        )
        with self.session_factory() as session:
            previous = session.scalar(
                select(Document)
                .where(with_state, Document.payload["state"].as_string() != state)
                .order_by(Document.created_at.desc())
                .limit(1)
            )
            first = select(Document).where(with_state)
            if previous is not None:
                first = first.where(Document.created_at > previous.created_at)
            entered = session.scalar(first.order_by(Document.created_at).limit(1))
        if entered is None:
            return None
        return {
            "enteredAt": entered.payload.get("occurredAtUtc"),
            "previousAt": None if previous is None else previous.payload.get("occurredAtUtc"),
        }

    def latest_telemetry(self) -> dict[str, Any] | None:
        with self.session_factory() as session:
            record = session.scalar(
                select(Document)
                .where(Document.kind == "live-sample")
                .order_by(Document.created_at.desc())
                .limit(1)
            )
            return None if record is None else dict(record.payload)

    def create_simulation(
        self,
        document: dict[str, Any],
        idempotency_key: str,
        request_hash: str,
    ) -> tuple[dict[str, Any], bool]:
        actor = document["requestedBy"]
        with self.session_factory.begin() as session:
            existing = session.scalar(
                select(SimulationRunRecord).where(
                    SimulationRunRecord.requested_by == actor,
                    SimulationRunRecord.idempotency_key == idempotency_key,
                )
            )
            if existing is not None:
                if existing.request_hash != request_hash:
                    raise ConflictError("la clave de idempotencia ya se usó con otra solicitud")
                return dict(existing.payload), False
            record = SimulationRunRecord(
                id=document["simulationRunId"],
                requested_by=actor,
                idempotency_key=idempotency_key,
                request_hash=request_hash,
                status=document["status"],
                semantic_status=document["semanticStatus"],
                payload=document,
            )
            session.add(record)
            try:
                session.flush()
            except IntegrityError as error:
                raise ConflictError(
                    "otra solicitud está usando la misma clave de idempotencia"
                ) from error
        return document, True

    def get_simulation(self, identifier: str) -> dict[str, Any] | None:
        with self.session_factory() as session:
            record = session.get(SimulationRunRecord, identifier)
            return None if record is None else dict(record.payload)

    def list_simulations(self, page: int, page_size: int) -> Page:
        with self.session_factory() as session:
            total = session.scalar(select(func.count()).select_from(SimulationRunRecord)) or 0
            records = session.scalars(
                select(SimulationRunRecord)
                .order_by(SimulationRunRecord.created_at.desc())
                .offset((page - 1) * page_size)
                .limit(page_size)
            ).all()
        return Page([dict(record.payload) for record in records], page, page_size, total)

    def update_simulation(self, document: dict[str, Any]) -> dict[str, Any]:
        with self.session_factory.begin() as session:
            record = session.get(SimulationRunRecord, document["simulationRunId"])
            if record is None:
                raise ConflictError("la simulación no existe")
            record.status = document["status"]
            record.semantic_status = document["semanticStatus"]
            record.payload = document
            record.updated_at = utc_now()
        return document

    def request_simulation_cancellation(self, identifier: str) -> dict[str, Any] | None:
        with self.session_factory.begin() as session:
            record = session.get(SimulationRunRecord, identifier)
            if record is None:
                return None
            if record.status == "queued":
                payload = dict(record.payload)
                payload["status"] = "cancelled"
                payload["finishedAt"] = utc_now().isoformat().replace("+00:00", "Z")
                record.status = "cancelled"
                record.payload = payload
            elif record.status == "running":
                record.cancellation_requested = True
                payload = dict(record.payload)
                payload["cancelRequested"] = True
                record.payload = payload
            return dict(record.payload)

    def claim_simulation(self, worker_id: str, lease_seconds: int = 30) -> dict[str, Any] | None:
        now = utc_now()
        with self.session_factory.begin() as session:
            statement = (
                select(SimulationRunRecord)
                .where(
                    (SimulationRunRecord.status == "queued")
                    | (
                        (SimulationRunRecord.status == "running")
                        & (SimulationRunRecord.lease_expires_at < now)
                    )
                )
                .order_by(SimulationRunRecord.created_at)
                .limit(1)
                .with_for_update(skip_locked=True)
            )
            record = session.scalar(statement)
            if record is None:
                return None
            payload = dict(record.payload)
            payload["status"] = "running"
            payload["startedAt"] = payload.get("startedAt") or now.isoformat().replace(
                "+00:00", "Z"
            )
            record.status = "running"
            record.worker_id = worker_id
            record.heartbeat_at = now
            record.lease_expires_at = now + timedelta(seconds=lease_seconds)
            record.payload = payload
            return payload

    def simulation_cancel_requested(self, identifier: str) -> bool:
        with self.session_factory() as session:
            record = session.get(SimulationRunRecord, identifier)
            return record is not None and record.cancellation_requested

    def heartbeat_simulation(
        self, identifier: str, worker_id: str, completed: int, total: int, lease_seconds: int = 30
    ) -> None:
        now = utc_now()
        with self.session_factory.begin() as session:
            record = session.get(SimulationRunRecord, identifier)
            if record is None or record.worker_id != worker_id or record.status != "running":
                raise ConflictError("el trabajador ya no posee la simulación")
            payload = dict(record.payload)
            payload["progress"] = {
                "completedSteps": completed,
                "totalSteps": total,
                "percent": round(100 * completed / total, 2),
            }
            record.payload = payload
            record.heartbeat_at = now
            record.lease_expires_at = now + timedelta(seconds=lease_seconds)

    def save_simulation_artifact(self, manifest: dict[str, Any]) -> None:
        with self.session_factory.begin() as session:
            record = session.scalar(
                select(SimulationArtifactRecord).where(
                    SimulationArtifactRecord.simulation_run_id == manifest["simulationRunId"]
                )
            )
            if record is None:
                session.add(
                    SimulationArtifactRecord(
                        id=manifest["artifactId"],
                        simulation_run_id=manifest["simulationRunId"],
                        manifest=manifest,
                    )
                )
            else:
                record.manifest = manifest

    def get_simulation_artifact(self, run_id: str) -> dict[str, Any] | None:
        with self.session_factory() as session:
            record = session.scalar(
                select(SimulationArtifactRecord).where(
                    SimulationArtifactRecord.simulation_run_id == run_id
                )
            )
            return None if record is None else dict(record.manifest)

    def claim_semantic_publication(self) -> dict[str, Any] | None:
        with self.session_factory.begin() as session:
            record = session.scalar(
                select(SimulationRunRecord)
                .where(
                    SimulationRunRecord.status == "succeeded",
                    SimulationRunRecord.semantic_status == "pending",
                )
                .order_by(SimulationRunRecord.created_at)
                .limit(1)
                .with_for_update(skip_locked=True)
            )
            if record is None:
                return None
            payload = dict(record.payload)
            payload["semanticStatus"] = "validating"
            record.semantic_status = "validating"
            record.payload = payload
            return payload

    def save_semantic_publication(self, run_id: str, publication: dict[str, Any]) -> None:
        with self.session_factory.begin() as session:
            record = session.get(SimulationRunRecord, run_id)
            if record is None:
                raise ConflictError("la simulación no existe")
            payload = dict(record.payload)
            payload["semanticStatus"] = publication["status"]
            record.semantic_status = publication["status"]
            record.payload = payload
            semantic = session.get(Document, ("simulation-semantic", run_id))
            if semantic is None:
                session.add(
                    Document(
                        kind="simulation-semantic",
                        identifier=run_id,
                        payload=publication,
                    )
                )
            else:
                semantic.payload = publication
                semantic.updated_at = utc_now()

    def get_semantic_publication(self, run_id: str) -> dict[str, Any] | None:
        return self.get_document("simulation-semantic", run_id)

    def request_semantic_rebuild(self, run_id: str | None = None) -> int:
        with self.session_factory.begin() as session:
            statement = select(SimulationRunRecord).where(SimulationRunRecord.status == "succeeded")
            if run_id is not None:
                statement = statement.where(SimulationRunRecord.id == run_id)
            records = session.scalars(statement.with_for_update()).all()
            for record in records:
                payload = dict(record.payload)
                payload["semanticStatus"] = "pending"
                record.semantic_status = "pending"
                record.payload = payload
            extraction_count = 0
            if run_id is None:
                result = session.execute(
                    delete(Document).where(Document.kind == "extraction-semantic")
                )
                extraction_count = result.rowcount or 0
            return len(records) + extraction_count

    def next_extraction_for_semantics(
        self, projection_version: str | None = None
    ) -> dict[str, Any] | None:
        """La próxima extracción cuyo grafo publicado falta o quedó viejo.

        Lo nuevo y lo que falló por el almacén, como siempre. Además, con
        `projection_version`, la publicada con otra versión de la proyección y
        la que recibió una revisión (medidas a mano, catación) después de
        publicarse: su grafo ya no dice lo que la plataforma sabe. Una rechazada
        solo vuelve si cambió la versión, porque con la misma daría lo mismo y,
        devuelta primero en cada vuelta, impediría publicar las siguientes.
        """
        extractions = self.list_documents("extraction", page=1, page_size=1000).items
        # Se cuenta cuántas revisiones tiene cada una, no la hora de la última:
        # la hora la pone quien registra y una adelantada haría republicar sin fin.
        revised: dict[str, int] = {}
        if projection_version is not None:
            for revision in self.list_documents(
                "extraction-revision", page=1, page_size=10000
            ).items:
                key = str(revision.get("resourceId"))
                revised[key] = revised.get(key, 0) + 1
        for extraction in extractions:
            identifier = str(extraction["id"])
            if not has_publishable_period(extraction):
                continue
            publication = self.get_document("extraction-semantic", identifier)
            if publication is None or publication.get("status") == "pending":
                return extraction
            if projection_version is None:
                continue
            if publication.get("projectionVersion") != projection_version:
                return extraction
            if publication.get("status") == "published" and revised.get(identifier, 0) != int(
                publication.get("revisionCount") or 0
            ):
                return extraction
        return None

    def extractions_without_end(self) -> list[str]:
        """Extracciones que la publicación semántica salta por no tener final.

        Son las que `next_extraction_for_semantics` no devuelve nunca: sin esta
        lista, el trabajador las dejaba fuera sin que constara en ningún sitio.
        """
        extractions = self.list_documents("extraction", page=1, page_size=1000).items
        return [str(item["id"]) for item in extractions if not has_publishable_period(item)]

    def save_extraction_publication(self, extraction_id: str, publication: dict[str, Any]) -> None:
        self.upsert_document("extraction-semantic", extraction_id, publication)

    def audit(
        self, actor: str, operation: str, resource: str, result: str, correlation_id: str
    ) -> None:
        with self.session_factory.begin() as session:
            session.add(
                AuditRecord(
                    actor=actor,
                    operation=operation,
                    resource=resource,
                    result=result,
                    correlation_id=correlation_id,
                )
            )

    def list_audit(self, page: int, page_size: int) -> Page:
        with self.session_factory() as session:
            total = session.scalar(select(func.count()).select_from(AuditRecord)) or 0
            rows = session.scalars(
                select(AuditRecord)
                .order_by(AuditRecord.occurred_at.desc())
                .offset((page - 1) * page_size)
                .limit(page_size)
            ).all()
        items = [
            {
                "id": row.id,
                "actor": row.actor,
                "operation": row.operation,
                "resource": row.resource,
                "result": row.result,
                "correlationId": row.correlation_id,
                "occurredAt": row.occurred_at.isoformat(),
            }
            for row in rows
        ]
        return Page(items, page, page_size, total)

    def counts(self) -> dict[str, int]:
        with self.session_factory() as session:
            rows = session.execute(
                select(Document.kind, func.count()).group_by(Document.kind)
            ).all()
        return {kind: count for kind, count in rows}

    def coffee_identifier_exists(self, field: str, value: str) -> bool:
        with self.session_factory() as session:
            match = session.scalar(
                select(Document.identifier)
                .where(
                    Document.kind == "coffee",
                    func.lower(Document.payload[field].as_string()) == value.lower(),
                )
                .limit(1)
            )
        return match is not None

    def save_user(
        self,
        identifier: str,
        username: str,
        display_name: str,
        password_hash: str,
        role: str,
        *,
        active: bool = True,
    ) -> None:
        with self.session_factory.begin() as session:
            record = session.get(UserAccount, identifier)
            if record is None:
                session.add(
                    UserAccount(
                        id=identifier,
                        username=username,
                        display_name=display_name,
                        password_hash=password_hash,
                        role=role,
                        active=active,
                    )
                )
            else:
                record.username = username
                record.display_name = display_name
                record.role = role
                record.active = active
                record.updated_at = utc_now()

    def user_credentials(self, username: str) -> tuple[UserIdentity, str] | None:
        with self.session_factory() as session:
            record = session.scalar(
                select(UserAccount).where(func.lower(UserAccount.username) == username.lower())
            )
            if record is None:
                return None
            return (
                UserIdentity(
                    record.id, record.username, record.display_name, record.role, record.active
                ),
                record.password_hash,
            )

    def list_users(self, page: int, page_size: int) -> Page:
        with self.session_factory() as session:
            total = session.scalar(select(func.count()).select_from(UserAccount)) or 0
            records = session.scalars(
                select(UserAccount)
                .order_by(UserAccount.display_name, UserAccount.username)
                .offset((page - 1) * page_size)
                .limit(page_size)
            ).all()
        return Page(
            [
                UserIdentity(
                    record.id,
                    record.username,
                    record.display_name,
                    record.role,
                    record.active,
                ).as_dict()
                | {"active": record.active}
                for record in records
            ],
            page,
            page_size,
            total,
        )

    def update_user(
        self, identifier: str, display_name: str, role: str, active: bool
    ) -> UserIdentity | None:
        with self.session_factory.begin() as session:
            record = session.get(UserAccount, identifier)
            if record is None:
                return None
            record.display_name = display_name
            record.role = role
            record.active = active
            record.updated_at = utc_now()
            return UserIdentity(
                record.id,
                record.username,
                record.display_name,
                record.role,
                record.active,
            )

    def create_session(self, token_hash: str, user_id: str, expires_at: datetime) -> None:
        with self.session_factory.begin() as session:
            session.add(UserSession(token_hash=token_hash, user_id=user_id, expires_at=expires_at))

    def user_for_session(self, token_hash: str, now: datetime) -> UserIdentity | None:
        with self.session_factory() as session:
            record = session.execute(
                select(UserAccount)
                .join(UserSession, UserSession.user_id == UserAccount.id)
                .where(
                    UserSession.token_hash == token_hash,
                    UserSession.revoked_at.is_(None),
                    UserSession.expires_at > now,
                    UserAccount.active.is_(True),
                )
            ).scalar_one_or_none()
            if record is None:
                return None
            return UserIdentity(
                record.id, record.username, record.display_name, record.role, record.active
            )

    def revoke_session(self, token_hash: str) -> None:
        with self.session_factory.begin() as session:
            record = session.get(UserSession, token_hash)
            if record is not None and record.revoked_at is None:
                record.revoked_at = utc_now()


def build_database(url: str) -> tuple[object, sessionmaker[Session]]:
    connect_args = {"check_same_thread": False} if url.startswith("sqlite") else {}
    engine = create_engine(url, pool_pre_ping=True, connect_args=connect_args)
    return engine, sessionmaker(engine, expire_on_commit=False)
