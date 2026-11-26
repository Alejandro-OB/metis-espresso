"""El resumen de una serie dice cuánto varía sin rellenar lo que falta."""

import pytest

from espresso_stability.repeatability import spread


def test_el_agua_del_perfil_final_da_el_rango_de_la_monografia():
    # Preparaciones 8, 9 y 10 de la catación (cap. 11): 91,65, 91,04 y 91,63 °C.
    result = spread([91.65, 91.04, 91.63])
    assert result["range"] == pytest.approx(0.61)
    assert result["minimum"] == pytest.approx(91.04)
    assert result["count"] == 3
    assert result["standardDeviation"] is not None


def test_una_preparacion_sin_dato_no_cuenta_y_se_declara():
    result = spread([38.0, None, 38.3])
    assert result["count"] == 2
    assert result["missing"] == 1
    assert result["range"] == pytest.approx(0.3)


def test_con_dos_valores_no_hay_desviacion_estandar():
    assert spread([10.0, 12.0])["standardDeviation"] is None


def test_con_menos_de_dos_valores_no_hay_resumen():
    result = spread([None, 5.0])
    assert result["range"] is None and result["mean"] is None
    assert result["count"] == 1
