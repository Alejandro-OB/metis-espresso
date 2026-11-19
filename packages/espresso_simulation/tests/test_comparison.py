"""Pruebas del alineamiento y las métricas de la comparación físico-virtual."""

from __future__ import annotations

import pytest

from espresso_simulation.comparison import (
    INSUFFICIENT,
    OUTSIDE,
    WITHIN,
    align,
    compare,
    interpolate,
)


def measured(values, *, step_ms=500, quality="valid"):
    """Serie medida a 500 ms, formato del historial."""
    return [
        {
            "elapsedMs": index * step_ms,
            "temperatureC": value,
            "pressureBar": 9.0 if value is not None else None,
            "quality": quality if value is not None else "missing",
            "sequence": index,
        }
        for index, value in enumerate(values)
    ]


def simulated(count, *, step=0.1, temperature=lambda t: 93.0, pressure=lambda t: 9.0):
    """Serie simulada a 100 ms, formato del artefacto."""
    return [
        {
            "simulationTimeSeconds": round((index + 1) * step, 6),
            "channels": {
                "temperatureC": temperature((index + 1) * step),
                "pressureBar": pressure((index + 1) * step),
            },
            "quality": "simulated",
            "sequence": index + 1,
        }
        for index in range(count)
    ]


#: Estado en t=0 con el que se inicializó el modelo; completa la serie simulada.
INICIO = {"temperatureC": 93.0, "pressureBar": 9.0}


def run(measured_samples, simulated_samples, *, initial_conditions=INICIO, **kwargs):
    return compare(
        extraction_id="extraction-test",
        simulation_run_id="sim-test",
        measured=measured_samples,
        simulated=simulated_samples,
        initial_conditions=initial_conditions,
        **kwargs,
    )


def channel(document, name):
    return next(item for item in document["channels"] if item["channel"] == name)


def test_interpolates_between_simulated_samples_on_the_measured_grid():
    """La rejilla medida es más gruesa que la simulada: el valor se interpola."""
    series = [(0.0, 10.0), (1.0, 20.0)]
    assert interpolate(series, 0.5) == pytest.approx(15.0)
    assert interpolate(series, 0.25) == pytest.approx(12.5)
    assert interpolate(series, 1.0) == pytest.approx(20.0)
    # fuera del intervalo simulado no se extrapola
    assert interpolate(series, 1.5) is None
    assert interpolate(series, -0.1) is None

    points, excluded = align(
        measured([90.0, 91.0, 92.0]),
        simulated(20, temperature=lambda t: 90.0 + t),
        "temperatureC",
        {"temperatureC": 90.0},
    )
    assert [point.elapsed_ms for point in points] == [0, 500, 1000]
    assert points[0].simulated == pytest.approx(90.0)   # estado inicial declarado
    assert points[1].simulated == pytest.approx(90.5)   # interpolado entre 0,4 s y 0,6 s
    assert excluded == {"invalidQuality": 0, "outsideSimulatedRange": 0}


def test_without_declared_initial_state_the_first_instant_is_not_extrapolated():
    """La FMU no registra t=0: sin condición inicial, ese instante se descarta."""
    points, excluded = align(measured([90.0, 91.0]), simulated(20), "temperatureC")
    assert [point.elapsed_ms for point in points] == [500]
    assert excluded["outsideSimulatedRange"] == 1


def test_missing_samples_are_excluded_and_counted_never_imputed():
    """Una muestra sin lectura no se rellena: se descarta y se declara."""
    samples = measured([90.0, None, 92.0, None, 94.0])
    points, excluded = align(samples, simulated(60), "temperatureC", INICIO)
    assert len(points) == 3
    assert excluded["invalidQuality"] == 2
    document = run(samples, simulated(60))
    assert document["alignment"]["measuredSampleCount"] == 5
    assert document["alignment"]["excluded"]["invalidQuality"] == 2
    assert document["alignment"]["coverageRatio"] == pytest.approx(0.6)


def test_measured_instants_outside_the_simulated_range_are_discarded_and_counted():
    """La preparación dura más que la simulación: el exceso no se extrapola."""
    samples = measured([93.0] * 10)          # hasta 4500 ms
    document = run(samples, simulated(20))    # solo hasta 2000 ms
    assert document["alignment"]["excluded"]["outsideSimulatedRange"] == 5
    assert document["alignment"]["comparedSampleCount"] == 5
    assert document["alignment"]["initialConditions"] == INICIO


def test_coverage_below_the_minimum_withholds_any_judgement_on_the_model():
    """Sin cobertura suficiente el sistema se abstiene en lugar de juzgar."""
    samples = measured([93.0, None, None, None, 93.0])   # cobertura 0,4
    document = run(samples, simulated(60), minimum_coverage=0.8)
    assert document["verdict"] == INSUFFICIENT
    assert channel(document, "temperatureC")["verdict"] == INSUFFICIENT


def test_identical_series_produce_exactly_zero_difference():
    """Si el modelo reprodujera la medición, toda diferencia sería nula."""
    document = run(measured([93.0] * 5), simulated(60))
    metrics = channel(document, "temperatureC")
    assert metrics["meanSignedDifference"] == 0.0
    assert metrics["meanAbsoluteDifference"] == 0.0
    assert metrics["rootMeanSquareDifference"] == 0.0
    assert metrics["maxAbsoluteDifference"] == 0.0
    assert metrics["verdict"] == WITHIN


def test_constant_offset_appears_as_signed_bias_with_the_modelica_convention():
    """La diferencia es simulado − medido: un modelo que predice de más da signo positivo."""
    document = run(
        measured([90.0] * 5),
        simulated(60, temperature=lambda t: 91.5),
        initial_conditions={"temperatureC": 91.5, "pressureBar": 9.0},
    )
    metrics = channel(document, "temperatureC")
    assert metrics["meanSignedDifference"] == pytest.approx(1.5)
    assert metrics["meanAbsoluteDifference"] == pytest.approx(1.5)
    assert metrics["rootMeanSquareDifference"] == pytest.approx(1.5)
    assert metrics["verdict"] == WITHIN          # 1,5 °C por debajo de la tolerancia de 2,5

    peor = run(
        measured([90.0] * 5),
        simulated(60, temperature=lambda t: 86.0),
        initial_conditions={"temperatureC": 86.0, "pressureBar": 9.0},
    )
    metricas = channel(peor, "temperatureC")
    assert metricas["meanSignedDifference"] == pytest.approx(-4.0)
    assert metricas["verdict"] == OUTSIDE
    assert peor["verdict"] == OUTSIDE


def test_pressure_channel_is_measured_but_kept_out_of_the_verdict():
    """La presión se registra con su limitación declarada y no decide el veredicto."""
    samples = measured([93.0] * 5)
    for sample in samples:
        sample["pressureBar"] = 6.0            # 3 bar por debajo de lo simulado
    document = run(samples, simulated(60))
    pressure = channel(document, "pressureBar")
    assert pressure["modelLimitation"] == "static-hydraulic-model"
    assert pressure["contributesToVerdict"] is False
    assert pressure["meanSignedDifference"] == pytest.approx(3.0)
    # Tiene tolerancia (0,89 bar) y queda fuera, pero el modelo no vota en presión.
    assert pressure["verdict"] == OUTSIDE
    # la temperatura coincide, así que el veredicto global no se ve arrastrado
    assert channel(document, "temperatureC")["verdict"] == WITHIN
    assert document["verdict"] == WITHIN


def test_pressure_votes_only_when_the_registry_calls_it_stable_and_it_has_tolerance():
    """La presión entra al veredicto con el modelo hidráulico ajustado, no antes."""
    samples = measured([93.0] * 5)
    for sample in samples:
        sample["pressureBar"] = 6.0            # 3 bar por debajo de lo simulado

    experimental = run(samples, simulated(60),
                       channel_maturity={"temperatureC": "stable", "pressureBar": "experimental"})
    assert channel(experimental, "pressureBar")["modelLimitation"] == "experimental-model-channel"
    assert experimental["verdict"] == WITHIN

    # Estable pero sin tolerancia: no vota, y la temperatura conserva su veredicto.
    pending = run(samples, simulated(60),
                  channel_maturity={"temperatureC": "stable", "pressureBar": "stable"},
                  model_tolerances={"pressureBar": None})
    assert channel(pending, "pressureBar")["modelLimitation"] == "tolerance-pending"
    assert pending["verdict"] == WITHIN

    voting = run(samples, simulated(60),
                 channel_maturity={"temperatureC": "stable", "pressureBar": "stable"},
                 model_tolerances={"pressureBar": 0.5})
    pressure = channel(voting, "pressureBar")
    assert pressure["contributesToVerdict"] is True
    assert "modelLimitation" not in pressure
    assert pressure["verdict"] == OUTSIDE
    assert voting["verdict"] == OUTSIDE


def test_maximum_difference_reports_the_instant_where_it_occurs():
    """La desviación mayor se conserva con su instante, para poder localizarla."""
    samples = measured([93.0, 93.0, 88.0, 93.0])
    document = run(samples, simulated(60))
    metrics = channel(document, "temperatureC")
    assert metrics["maxAbsoluteDifference"] == pytest.approx(5.0)
    assert metrics["elapsedMsAtMaxDifference"] == 1000


def test_rejects_empty_series_and_invalid_coverage():
    with pytest.raises(ValueError, match="muestras medidas"):
        run([], simulated(10))
    with pytest.raises(ValueError, match="no tiene muestras"):
        run(measured([93.0]), [])
    with pytest.raises(ValueError, match="minimum_coverage"):
        run(measured([93.0]), simulated(10), minimum_coverage=1.5)


def test_the_minute_before_pressing_does_not_dilute_the_coverage():
    """La simulación empieza al pulsar: el minuto previo no cuenta como no comparado."""
    samples = measured([93.0] * 5)
    before = [{**samples[0], "elapsedMs": -60_000 + i * 500} for i in range(120)]
    document = run(before + samples, simulated(60))
    assert document["alignment"]["measuredSampleCount"] == 5
    assert document["verdict"] == WITHIN
