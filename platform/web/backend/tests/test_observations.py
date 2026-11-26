"""La reducción a observaciones puntuales no inventa ni juzga."""

from aplicacion_web.application.observations import preparation_observations

SAMPLES = [
    {"elapsedMs": 0, "temperatureC": 88.0, "pressureBar": 1.0},
    {"elapsedMs": 4000, "temperatureC": 92.0, "pressureBar": 9.0},
    {"elapsedMs": 8000, "temperatureC": None, "pressureBar": 9.4},
    {"elapsedMs": 12000, "temperatureC": 93.5, "pressureBar": 8.8},
]


def extraction(**changes):
    return {
        "id": "prep-1",
        "startedAt": "2026-10-16T10:00:00Z",
        "finishedAt": "2026-10-16T10:00:30Z",
        "durationMs": 30000,
        "seriesRef": "serie",
        "sampleCount": len(SAMPLES),
        **changes,
    }


def by_key(observations):
    return {item["key"]: item for item in observations}


def test_the_window_extremes_ignore_the_ramp_and_missing_samples():
    found = by_key(preparation_observations(extraction(), SAMPLES,
                                            {"status": "stabilized", "startMs": 4000}))
    assert found["temperature-minimum"]["value"] == 92.0
    assert found["temperature-maximum"]["value"] == 93.5
    assert found["pressure-minimum"]["value"] == 8.8
    assert found["pressure-maximum"]["value"] == 9.4


def test_without_a_window_temperature_is_not_published():
    found = by_key(preparation_observations(extraction(), SAMPLES,
                                            {"status": "never-stabilized"}))
    assert "temperature-maximum" not in found
    # La presión que nunca llegó a su rango se juzga entera: ese es su fallo.
    assert found["pressure-minimum"]["value"] == 1.0


def test_a_calculation_missing_an_operand_is_not_published():
    manual = {"coffeeDoseG": 13.0, "beverageMassG": 26.0, "extractionYieldPercent": 18.0}
    found = by_key(preparation_observations(extraction(manualMeasurements=manual), [], None))
    assert found["brew-ratio"]["value"] == 2.0
    assert "extraction-yield" not in found
    assert "tds" not in found


def test_without_series_nothing_is_derived_from_it():
    found = by_key(preparation_observations(extraction(sampleCount=0), SAMPLES,
                                            {"status": "stabilized", "startMs": 0}))
    assert "thermal-stability" not in found
    assert "temperature-maximum" not in found


def test_a_stored_bed_condition_is_not_published():
    # La máquina no comprueba el lecho: una condición guardada antes del
    # 3-nov-2026 se conserva en el registro, pero no llega al razonador.
    found = by_key(preparation_observations(
        extraction(manualMeasurements={"bedCondition": "uneven"}), [], None))
    assert "bed-condition" not in found


def test_a_device_preparation_without_series_reference_still_has_its_series():
    """La placa no envía `seriesRef`; sus muestras siguen siendo su serie."""
    device = extraction(seriesRef=None, sampleCount=270)
    found = by_key(preparation_observations(device, SAMPLES,
                                            {"status": "stabilized", "startMs": 4000}))
    assert found["temperature-maximum"]["value"] == 93.5


def test_the_minute_before_pressing_is_not_part_of_the_preparation():
    """Desde el firmware d8a8b66 la serie trae el minuto previo con elapsedMs negativo."""
    before = [{"elapsedMs": -60_000 + i * 500, "temperatureC": 70.0, "pressureBar": 0.0}
              for i in range(120)]
    found = by_key(preparation_observations(extraction(), before + SAMPLES,
                                            {"status": "stabilized", "startMs": 4000}))
    # El rango sale solo de las muestras del café (88 a 93,5 °C), no del reposo a 70.
    assert found["thermal-stability"]["value"] == 5.5
