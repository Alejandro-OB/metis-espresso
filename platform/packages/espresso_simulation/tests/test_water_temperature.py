"""Pruebas de la temperatura del agua de salida (ADR-019)."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import UTC, datetime, timedelta

from espresso_simulation.water_temperature import (
    ESTIMATED,
    PAIRED,
    PAIRING_MARGIN_MS,
    UNAVAILABLE,
    UNPAIRED,
    WaterTemperature,
    estimate,
    pair_with_body_sample,
)

READING_AT = datetime(2026, 10, 1, 9, 0, 0, tzinfo=UTC)


def sample(offset_ms, temperature=93.0, *, quality="valid", identifier=None):
    occurred_at = READING_AT + timedelta(milliseconds=offset_ms)
    return {
        "id": identifier or f"sample{offset_ms:+d}",
        "occurredAt": occurred_at.isoformat(),
        "temperatureC": temperature,
        "quality": quality,
        "source": "measured",
    }


@dataclass(frozen=True)
class FixedOffset:
    """Transformación de prueba: no pretende ser la forma que se ajustará."""

    version: str = "test-1"
    offset_c: float = -2.0
    lowest_c: float = 85.0
    highest_c: float = 100.0

    def covers(self, body_temperature_c, condition):
        return self.lowest_c <= body_temperature_c <= self.highest_c

    def apply(self, body_temperature_c, condition):
        return body_temperature_c + self.offset_c


def test_a_fresh_estimate_asserts_no_water_temperature():
    assert WaterTemperature().status == UNAVAILABLE
    assert WaterTemperature().value_c is None


def test_without_transformation_the_water_temperature_is_unavailable_not_the_body_one():
    result = estimate(93.0, "rest", None)

    assert result.status == UNAVAILABLE
    assert result.value_c is None
    assert result.reason == "no-transformation"


def test_outside_the_validity_range_the_water_temperature_is_unavailable():
    result = estimate(70.0, "rest", FixedOffset())

    assert result.status == UNAVAILABLE
    assert result.value_c is None
    assert result.transformation_version == "test-1"
    assert result.reason == "outside-validity"


def test_a_missing_body_temperature_does_not_become_a_water_temperature():
    result = estimate(None, "rest", FixedOffset())

    assert result.status == UNAVAILABLE
    assert result.reason == "no-body-temperature"


def test_an_estimate_declares_the_version_that_produced_it():
    result = estimate(93.0, "during-extraction", FixedOffset())

    assert result.status == ESTIMATED
    assert result.value_c == 91.0
    assert result.as_document()["transformationVersion"] == "test-1"


def test_a_reading_pairs_with_the_nearest_sample_within_one_period():
    result = pair_with_body_sample(
        READING_AT, [sample(-900, 92.0), sample(300, 93.5), sample(1400, 94.0)]
    )

    assert result["status"] == PAIRED
    assert result["sampleId"] == "sample+300"
    assert result["gapMs"] == 300
    assert result["bodyTemperatureC"] == 93.5


def test_a_reading_without_a_sample_within_the_margin_is_kept_unpaired():
    result = pair_with_body_sample(
        READING_AT, [sample(-(PAIRING_MARGIN_MS + 1)), sample(PAIRING_MARGIN_MS + 1)]
    )

    assert result["status"] == UNPAIRED
    assert result["reason"] == "no-sample-within-margin"
    assert "bodyTemperatureC" not in result


def test_an_unfit_sample_is_paired_and_marked_not_replaced_by_a_fit_one():
    result = pair_with_body_sample(
        READING_AT, [sample(100, 93.0, quality="out-of-range"), sample(800, 93.2)]
    )

    assert result["sampleId"] == "sample+100"
    assert result["quality"] == "out-of-range"


def test_the_temperature_channel_quality_is_kept_apart_from_the_sample_one():
    """Una presión no apta deja la muestra entera como no apta, pero la
    temperatura del cuerpo puede seguir siendo válida, y quien ajuste la
    transformación necesita saberlo."""
    unfit_pressure = {
        **sample(100, 93.0, quality="invalid"),
        "channelQuality": {"temperature": "valid", "pressure": "invalid"},
    }

    result = pair_with_body_sample(READING_AT, [unfit_pressure])

    assert result["quality"] == "invalid"
    assert result["temperatureQuality"] == "valid"


def test_a_sample_without_channel_quality_does_not_invent_one():
    assert pair_with_body_sample(READING_AT, [sample(100)])["temperatureQuality"] is None


def test_the_pair_keeps_the_preparation_the_sample_belonged_to():
    during = {**sample(100), "extractionId": "prep-0042"}

    assert pair_with_body_sample(READING_AT, [during])["sampleExtractionId"] == "prep-0042"
    assert pair_with_body_sample(READING_AT, [sample(100)])["sampleExtractionId"] is None


def test_a_tie_takes_the_earlier_sample_whatever_the_arrival_order():
    later, earlier = sample(500, 94.0), sample(-500, 92.0)

    assert pair_with_body_sample(READING_AT, [later, earlier])["sampleId"] == "sample-500"
    assert pair_with_body_sample(READING_AT, [earlier, later])["sampleId"] == "sample-500"


def test_a_sample_without_declared_origin_is_not_passed_as_measured():
    undeclared = sample(0)
    undeclared.pop("source")

    assert pair_with_body_sample(READING_AT, [undeclared])["source"] == "unknown"
