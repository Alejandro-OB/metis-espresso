"""Pruebas de la desviación de una propiedad frente al perfil.

Comprueban propiedades: que la serie solo se juzga dentro de su ventana, que una
desviación dice cuándo empezó y cuánto duró, que sin tolerancia no se juzga y
que lo no apto no cuenta.
"""

from espresso_stability.conformity import (
    Status,
    bounds,
    not_evaluable,
    series_deviation,
    value_deviation,
)

PRESSURE = bounds({"strategy": "target", "target": 9.0, "tolerance": 0.5})


def series(*pressures):
    return [{"elapsedMs": index * 1000, "pressureBar": value}
            for index, value in enumerate(pressures)]


def test_the_rise_before_the_window_does_not_make_it_outside():
    result = series_deviation(series(0.0, 4.0, 9.0, 9.2), "pressureBar", PRESSURE, 2000)
    assert result["deviation"]["status"] == Status.WITHIN.value
    assert result["observed"]["minimum"] == 9.0


def test_a_deviation_states_when_it_started_and_how_long_it_lasted():
    samples = series(9.0, 9.0, 7.0, 7.5, 9.0, 9.0)
    deviation = series_deviation(samples, "pressureBar", PRESSURE, 0)["deviation"]
    assert deviation["status"] == Status.OUTSIDE.value
    assert deviation["direction"] == "below"
    assert deviation["amount"] == 1.5
    assert deviation["firstOutsideMs"] == 2000
    assert deviation["outsideDurationMs"] == 2000


def test_a_single_last_sample_outside_does_not_last_zero():
    deviation = series_deviation(series(9.0, 9.0, 10.0), "pressureBar", PRESSURE, 0)["deviation"]
    assert deviation["firstOutsideMs"] == 2000
    assert deviation["outsideDurationMs"] == 1000


def test_without_tolerance_the_value_is_kept_but_not_judged():
    limits = bounds({"strategy": "target", "target": 93.0})
    result = value_deviation(95.0, limits)
    assert result["observed"]["value"] == 95.0
    assert result["deviation"]["status"] == Status.NO_TOLERANCE.value
    assert limits.objective["tolerance"] is None


def test_a_range_is_its_own_tolerance():
    limits = bounds({"strategy": "range", "minimum": 91.0, "maximum": 95.0})
    assert value_deviation(94.0, limits)["deviation"]["status"] == Status.WITHIN.value


def test_unfit_samples_do_not_count():
    result = series_deviation(series(9.0, None, 9.1), "pressureBar", PRESSURE, 0)
    assert result["deviation"]["status"] == Status.WITHIN.value


def test_without_a_window_nothing_is_judged():
    result = series_deviation(series(9.0, 9.0), "pressureBar", PRESSURE, None)
    assert result["observed"] is None
    assert result["deviation"]["status"] == Status.UNAVAILABLE.value
    assert not_evaluable("x")["status"] == Status.NOT_EVALUABLE.value


def test_the_zero_of_the_status_is_the_conservative_one():
    assert list(Status)[0] is Status.UNASSESSED
