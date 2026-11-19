"""Temperatura del agua de salida a partir de la del cuerpo de la caldera.

Módulo puro (ADR-019): no conoce la aplicación web, el almacenamiento ni el
reloj. Recibe lo que necesita y devuelve lo que decidió.

El NTC mide el cuerpo de aluminio, no el agua. La relación entre ambas solo
puede establecerla una transformación ajustada sobre pares medidos en la
campaña 1 del plan experimental; el modelo de dos nodos no la determina, porque
con una sola señal su conductancia interna no está identificada. Por eso este
módulo hace dos cosas y ninguna más:

- empareja una lectura de referencia del agua con la muestra del cuerpo más
  cercana, para que la temperatura del cuerpo salga de la serie y no se teclee;
- aplica una transformación ya establecida, y declara la temperatura del agua
  **no disponible** cuando no la hay o cuando cae fuera de lo que sus pares
  cubren.

**Lo que no hace.** No ajusta ninguna transformación: su forma se decide con los
pares delante, y fijarla antes obligaría a adivinarla. Tampoco sustituye nunca
la temperatura del agua por la del cuerpo ni por el nodo caliente del modelo:
un valor por omisión convertiría un dato inexistente en evidencia, y el
sostenimiento de ADR-018 corregiría hacia él.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from typing import Any, Literal, Protocol

#: Distancia máxima entre la lectura y la muestra con que se empareja. Es un
#: periodo de telemetría durante la preparación; en la campaña 1 el periodo
#: fuera de ella se iguala a este (ADR-019 § 4.3). Ensancharlo emparejaría,
#: justo después de una extracción, con un cuerpo que ya no estaba así.
PAIRING_MARGIN_MS = 1000

Condition = Literal["rest", "after-extraction", "during-extraction"]

#: El primer estado es el conservador: una estimación recién creada no afirma
#: ninguna temperatura del agua.
UNAVAILABLE = "unavailable"
ESTIMATED = "estimated"

PAIRED = "paired"
UNPAIRED = "unpaired"


class Transformation(Protocol):
    """Transformación versionada entre cuerpo y agua de salida (ADR-019 § 4.2).

    Su forma concreta se decidirá con los pares de la campaña 1; aquí solo se
    fija lo que cualquier forma debe ofrecer para poder aplicarse con trazabilidad.
    """

    @property
    def version(self) -> str: ...

    def covers(self, body_temperature_c: float, condition: Condition) -> bool:
        """Si la temperatura y la condición caen dentro de lo que sus pares cubren."""
        ...

    def apply(self, body_temperature_c: float, condition: Condition) -> float: ...


@dataclass(frozen=True)
class WaterTemperature:
    status: str = UNAVAILABLE
    value_c: float | None = None
    transformation_version: str | None = None
    reason: str | None = None

    def as_document(self) -> dict[str, Any]:
        return {
            "status": self.status,
            "valueC": self.value_c,
            "transformationVersion": self.transformation_version,
            "reason": self.reason,
        }


def estimate(
    body_temperature_c: float | None,
    condition: Condition,
    transformation: Transformation | None,
) -> WaterTemperature:
    """Temperatura del agua de salida, o no disponible con su causa."""
    if transformation is None:
        return WaterTemperature(reason="no-transformation")
    if body_temperature_c is None:
        return WaterTemperature(
            transformation_version=transformation.version, reason="no-body-temperature"
        )
    if not transformation.covers(body_temperature_c, condition):
        return WaterTemperature(
            transformation_version=transformation.version, reason="outside-validity"
        )
    return WaterTemperature(
        status=ESTIMATED,
        value_c=float(transformation.apply(body_temperature_c, condition)),
        transformation_version=transformation.version,
    )


def pair_with_body_sample(
    taken_at: datetime, samples: list[dict[str, Any]]
) -> dict[str, Any]:
    """Empareja una lectura del agua con la muestra del cuerpo más cercana.

    ``samples`` son muestras de telemetría del mismo dispositivo, con
    ``occurredAt`` en ISO 8601 con zona horaria. La calidad de la muestra se
    conserva tal cual, y aparte la del canal de temperatura: la de la muestra
    junta temperatura y presión, y una presión no apta no dice nada del cuerpo.
    Las dos se conservan: una muestra no apta se empareja y queda marcada, no se
    sustituye por otra, porque quien ajuste la transformación decide qué pares
    usa. Ante dos muestras a la misma distancia se toma la anterior, para que el
    resultado no dependa del orden en que llegaron.
    """
    best: tuple[int, datetime, dict[str, Any]] | None = None
    for sample in samples:
        occurred_at = datetime.fromisoformat(str(sample["occurredAt"]))
        gap_ms = abs(round((occurred_at - taken_at).total_seconds() * 1000))
        if gap_ms > PAIRING_MARGIN_MS:
            continue
        if best is None or (gap_ms, occurred_at) < (best[0], best[1]):
            best = (gap_ms, occurred_at, sample)
    if best is None:
        return {
            "status": UNPAIRED,
            "reason": "no-sample-within-margin",
            "marginMs": PAIRING_MARGIN_MS,
        }
    gap_ms, _, sample = best
    value = sample.get("temperatureC")
    return {
        "status": PAIRED,
        "sampleId": sample.get("id"),
        "sampleOccurredAt": sample["occurredAt"],
        "gapMs": gap_ms,
        "marginMs": PAIRING_MARGIN_MS,
        "bodyTemperatureC": float(value) if isinstance(value, int | float) else None,
        "quality": sample.get("quality"),
        "temperatureQuality": (sample.get("channelQuality") or {}).get("temperature"),
        "source": sample.get("source") or "unknown",
        # La preparación en curso según el propio dispositivo: la muestra con
        # que se emparejó dice qué preparación estaba corriendo en ese instante.
        "sampleExtractionId": sample.get("extractionId"),
    }
