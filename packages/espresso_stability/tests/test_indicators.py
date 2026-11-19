"""Pruebas de los indicadores de estabilidad.

Comprueban propiedades: que la subida inicial de presión no cuenta, que una
preparación que nunca llega a su presión no recibe nota, que sin tolerancia
declarada no hay porcentaje pero sí error medio, que las medidas no aptas no
cuentan y que cada resultado declara su regla y dónde se midió la temperatura.

Los casos de la ventana se repiten en `device/tests/host/test_extraction_session.c`
para que el firmware y la plataforma no puedan divergir sin que falle una prueba.
"""

import pytest

from espresso_stability.indicators import (
    TEMPERATURE_SITE_BOILER,
    WINDOW_RULE,
    Requested,
    WindowStatus,
    stability,
)

TEMPERATURE = Requested(target=93.0, tolerance=1.0)
PRESSURE = Requested(target=9.0, tolerance=0.5)


def series(*points):
    return [
        {"elapsedMs": index * 1000, "temperatureC": temperature, "pressureBar": pressure}
        for index, (temperature, pressure) in enumerate(points)
    ]


def test_the_initial_pressure_rise_does_not_count():
    # Tres segundos subiendo, luego estable y exacto.
    samples = series((90.0, 0.0), (90.0, 3.0), (90.0, 6.0), (93.0, 9.0), (93.0, 9.0))
    result = stability(samples, TEMPERATURE, PRESSURE)
    assert result["window"] == {"rule": WINDOW_RULE, "status": "stabilized", "startMs": 3000}
    assert result["temperatureMaeC"] == 0.0
    assert result["pressureMaeBar"] == 0.0
    assert result["temperatureWithinTolerancePercent"] == 100.0
    assert result["pressureWithinTolerancePercent"] == 100.0


def test_once_open_the_window_keeps_later_departures():
    # La ventana no se cierra si la presión vuelve a salir: esa salida es
    # justamente la inestabilidad que se quiere medir.
    samples = series((93.0, 0.0), (93.0, 9.0), (93.0, 7.0), (93.0, 9.0))
    result = stability(samples, TEMPERATURE, PRESSURE)
    assert result["window"]["startMs"] == 1000
    assert result["pressureMaeBar"] == pytest.approx(2.0 / 3.0, abs=1e-3)
    assert result["pressureWithinTolerancePercent"] == pytest.approx(66.7)


def test_a_preparation_that_never_reaches_its_pressure_gets_no_score():
    samples = series((93.0, 2.0), (93.0, 4.0), (93.0, 6.0))
    result = stability(samples, TEMPERATURE, PRESSURE)
    assert result["window"] == {"rule": WINDOW_RULE, "status": "never-stabilized", "startMs": None}
    for name in ("temperatureMaeC", "pressureMaeBar",
                 "temperatureWithinTolerancePercent", "pressureWithinTolerancePercent"):
        assert result[name] is None


def test_without_a_declared_tolerance_there_is_no_percentage_but_there_is_an_error():
    samples = series((90.0, 0.0), (92.0, 9.0), (94.0, 9.0))
    result = stability(samples, Requested(target=93.0), PRESSURE)
    assert result["temperatureWithinTolerancePercent"] is None
    assert result["temperatureMaeC"] == 1.0


def test_without_a_pressure_range_the_window_cannot_open():
    samples = series((93.0, 9.0), (93.0, 9.0))
    result = stability(samples, TEMPERATURE, Requested(target=9.0))
    assert result["window"]["status"] == WindowStatus.NO_PRESSURE_RANGE.value
    assert result["temperatureMaeC"] is None


def test_unfit_measurements_do_not_count_and_do_not_open_the_window():
    samples = series((93.0, None), (None, 9.0), (95.0, 9.0))
    result = stability(samples, TEMPERATURE, PRESSURE)
    assert result["window"]["startMs"] == 1000
    # Solo la muestra de 95 °C tiene temperatura válida dentro de la ventana.
    assert result["temperatureMaeC"] == 2.0
    assert result["temperatureWithinTolerancePercent"] == 0.0


def test_samples_arrive_in_any_order():
    samples = series((90.0, 0.0), (93.0, 9.0), (93.0, 9.0))
    expected = stability(samples, TEMPERATURE, PRESSURE)
    assert stability(list(reversed(samples)), TEMPERATURE, PRESSURE) == expected


def test_the_result_says_where_the_temperature_was_measured():
    result = stability(series((93.0, 9.0)), TEMPERATURE, PRESSURE)
    assert result["temperatureSite"] == TEMPERATURE_SITE_BOILER


def test_the_zero_of_the_status_is_the_conservative_one():
    assert list(WindowStatus)[0] is WindowStatus.UNDETERMINED
