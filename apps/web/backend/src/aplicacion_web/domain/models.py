from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

from .errors import ValidationError

ALLOWED_MESSAGES = {
    "telemetry.sample",
    "operational.event",
    "extraction.completed",
    "extraction.series",
    "profile.proposal",
}


def utc_now() -> datetime:
    return datetime.now(UTC)


def iso_now() -> str:
    return utc_now().isoformat().replace("+00:00", "Z")


def require_text(value: Any, field: str, maximum: int = 2000) -> str:
    if not isinstance(value, str) or not value.strip() or len(value.strip()) > maximum:
        raise ValidationError(f"{field} debe ser texto no vacío de máximo {maximum} caracteres")
    return value.strip()


def parse_instant(value: Any, field: str) -> datetime:
    text = require_text(value, field, 80)
    try:
        parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError as error:
        raise ValidationError(f"{field} debe estar en RFC 3339") from error
    if parsed.tzinfo is None:
        raise ValidationError(f"{field} debe incluir zona horaria")
    return parsed


def utc_instant_text(value: Any) -> str:
    """Instante en UTC con ancho fijo, comparable como texto.

    Los sobres admiten cualquier desplazamiento RFC 3339, y dos textos del mismo
    instante con zonas distintas no se ordenan bien al compararlos. Normalizar
    permite buscar por intervalo en la base sin convertir cada fila.
    """
    instant = value if isinstance(value, datetime) else parse_instant(value, "occurredAt")
    return instant.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%S.%fZ")


#: Terminación de una preparación que un corte de energía dejó abierta.
POWER_LOSS_TERMINATION = "unexpected-power-loss"


def interrupted_by_power_loss(extraction: dict[str, Any]) -> bool:
    """Si la preparación la cerró el dispositivo al rearrancar tras un corte.

    El dispositivo no vio el final (ADR-005 § 3 y § 8 punto 10, RNF-006): no hay
    serie, ni indicadores, ni duración, y a veces tampoco inicio. Nada de lo que
    se juzga sobre una preparación puede juzgarse sobre esta, y quien la consuma
    tiene que saberlo en lugar de encontrar un hueco.
    """
    return extraction.get("termination") == POWER_LOSS_TERMINATION


def has_publishable_period(extraction: dict[str, Any]) -> bool:
    """Si la extracción tiene el periodo que exigen las formas de EKO 3.2.0.

    Inicio y fin son obligatorios salvo en una cortada por falta de energía,
    que se publica sin fin, y sin inicio si el dispositivo no tenía hora
    (`ExtractionStartShape` y `ExtractionEndShape`). Las demás sin final se
    quedan fuera y el trabajador semántico lo registra.
    """
    return bool(extraction.get("finishedAt")) or interrupted_by_power_loss(extraction)


def extraction_order_instant(extraction: dict[str, Any]) -> str:
    """Instante por el que se ordena una preparación en el historial.

    Es el final cuando consta. Una preparación cortada no lo tiene, y se ordena
    por el instante en que el dispositivo la cerró al rearrancar, que es el más
    temprano en que se supo de ella. Sirve para ordenar y para situarla en un
    intervalo de fechas; no se muestra ni se archiva como final, porque no lo es.
    """
    return str(extraction.get("finishedAt") or extraction.get("ingestedAt") or "")


def validate_envelope(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValidationError("el mensaje debe ser un objeto JSON")
    required = {
        "schemaVersion",
        "messageId",
        "messageType",
        "deviceId",
        "sequence",
        "occurredAt",
        "payload",
    }
    missing = required.difference(value)
    if missing:
        raise ValidationError(f"faltan campos: {', '.join(sorted(missing))}")
    if value["schemaVersion"] != "1.0":
        raise ValidationError("schemaVersion no soportada")
    require_text(value["messageId"], "messageId", 80)
    require_text(value["deviceId"], "deviceId", 120)
    if value["messageType"] not in ALLOWED_MESSAGES:
        raise ValidationError("messageType no soportado")
    if (
        not isinstance(value["sequence"], int)
        or isinstance(value["sequence"], bool)
        or value["sequence"] < 0
    ):
        raise ValidationError("sequence inválida")
    parse_instant(value["occurredAt"], "occurredAt")
    if not isinstance(value["payload"], dict):
        raise ValidationError("payload debe ser un objeto JSON")
    return value


@dataclass(frozen=True, slots=True)
class Identity:
    subject: str
    role: str

    def can_write(self) -> bool:
        return self.role in {"barista", "admin"}

    def can_admin(self) -> bool:
        return self.role == "admin"


@dataclass(frozen=True, slots=True)
class UserIdentity:
    id: str
    username: str
    display_name: str
    role: str
    active: bool = True

    def as_identity(self) -> Identity:
        return Identity(self.id, self.role)

    def as_dict(self) -> dict[str, Any]:
        return {
            "id": self.id,
            "username": self.username,
            "displayName": self.display_name,
            "role": self.role,
        }


@dataclass(frozen=True, slots=True)
class Page:
    items: list[dict[str, Any]]
    page: int
    page_size: int
    total: int

    def as_dict(self) -> dict[str, Any]:
        return {
            "items": self.items,
            "page": self.page,
            "pageSize": self.page_size,
            "total": self.total,
            "pages": max(1, (self.total + self.page_size - 1) // self.page_size),
        }
