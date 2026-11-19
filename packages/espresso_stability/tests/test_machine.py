"""La estabilidad de la máquina usa sus bandas medidas, no la tolerancia del perfil."""

import pytest

from espresso_stability.machine import machine_stability


def samples(pressures, start_ms=0, step_ms=500):
    return [{"elapsedMs": start_ms + i * step_ms, "temperatureC": None, "pressureBar": p}
            for i, p in enumerate(pressures)]


def test_pressure_is_judged_from_the_end_of_the_ramp_on_one_second_averages():
    # Subida en los dos primeros segundos; después, pulsación de ±0,2 bar entre
    # muestras alrededor de 8,5: cada promedio de 1 s queda en 8,5.
    ramp = [2.0, 4.0, 6.0, 7.5]
    plateau = [8.3, 8.7] * 6
    result = machine_stability(samples(ramp + plateau), None, 8.5)["pressure"]
    assert result["status"] == "measured"
    assert result["startMs"] == 2000
    assert result["fraction"] == pytest.approx(1.0)


def test_a_pressure_that_never_reaches_the_band_is_not_scored():
    result = machine_stability(samples([5.0] * 20), None, 8.5)["pressure"]
    assert result["status"] == "never-reached-band"
    assert result["fraction"] is None


def test_without_wait_readings_thermal_stability_is_not_invented():
    during = [{"elapsedMs": i * 500, "temperatureC": 95.0, "pressureBar": None}
              for i in range(10)]
    thermal = machine_stability(during, 101.9, None)["thermal"]
    assert thermal["status"] == "no-wait-samples"
    assert thermal["fraction"] is None


def test_thermal_stability_covers_the_whole_wait_and_weighs_by_time():
    # Diez minutos de espera con telemetría cada 15 s; los primeros 3 min,
    # 2,5 °C por encima (fuera de banda). El minuto previo de la serie, cada
    # 0,5 s y dentro, no pesa más que el resto de la espera por tener más lecturas.
    wait = [(-600.0 + 15 * i, 101.9 + (2.5 if i < 12 else 0.3)) for i in range(36)]
    pre = [{"elapsedMs": -60_000 + i * 500, "temperatureC": 102.0, "pressureBar": None}
           for i in range(120)]
    thermal = machine_stability(pre, 101.9, None, wait, -600.0)["thermal"]
    assert thermal["status"] == "measured"
    assert thermal["fraction"] == pytest.approx(420 / 600, abs=1e-3)
    assert thermal["coverage"] == pytest.approx(1.0)
    assert thermal["maxDeviationC"] == pytest.approx(2.5)


def test_a_gap_in_the_wait_is_declared_not_filled():
    # Una lectura a -600 s y después nada hasta -300 s, desde donde llegan cada 15 s.
    wait = [(-600.0, 102.0)] + [(-300.0 + 15 * i, 102.0) for i in range(20)]
    thermal = machine_stability([], 101.9, None, wait, -600.0)["thermal"]
    # La primera lectura cubre 60 s; desde -300 s se ve todo: 360 de 600 s.
    assert thermal["coverage"] == pytest.approx(0.6, abs=1e-3)


def test_water_between_preparations_is_estimated_with_each_line():
    from espresso_profiles.water_range import CURRENT_METAL, LINE_V6
    from espresso_stability.machine import water_between_preparations

    result = water_between_preparations(101.9, 101.3, LINE_V6, LINE_V6)
    assert result["status"] == "estimated"
    assert result["method"] == "acta-temperatura-v6"
    assert result["estimatedC"] == pytest.approx(0.8063 * 101.9 + 9.84, abs=0.01)
    assert result["differenceC"] == pytest.approx(0.8063 * 0.6, abs=0.01)
    assert result["within"] is True

    current = water_between_preparations(104.9, 104.6)
    assert current["method"] == CURRENT_METAL.method
    assert current["estimatedC"] == pytest.approx(0.8819 * 104.9 - 0.49, abs=0.01)


def test_waters_from_different_lines_are_not_subtracted():
    """Del café de la v6 al de la v11 la recta cambia unos 2,6 °C: no es el agua."""
    from espresso_profiles.water_range import CURRENT_METAL as CURRENT
    from espresso_profiles.water_range import LINE_V6
    from espresso_stability.machine import water_between_preparations

    result = water_between_preparations(104.9, 101.9, CURRENT, LINE_V6)
    assert result["status"] == "line-changed"
    assert result["estimatedC"] is not None and result["previousEstimatedC"] is not None
    assert result["differenceC"] is None and result["within"] is None
    assert water_between_preparations(104.9, 104.9, CURRENT, None)["status"] == "line-changed"
    unknown = water_between_preparations(104.9, 104.9, None, CURRENT)
    assert unknown["status"] == "no-line" and unknown["estimatedC"] is None


def test_outside_the_line_range_the_water_is_not_estimated():
    from espresso_stability.machine import water_between_preparations

    assert water_between_preparations(70.0, 101.9)["status"] == "outside-line-range"
    assert water_between_preparations(None, 101.9)["status"] == "no-body-at-press"
    assert water_between_preparations(101.9, None)["status"] == "no-previous"
