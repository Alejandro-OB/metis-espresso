"""El agua tras un reposo: propiedades del modelo, no valores de un ajuste.

Que tras un reposo largo el primer uso salga más frío que los siguientes, que el
reposo corto no lo enfríe, que nunca pase del hervor local y que sin metal no
se estime nada.
"""

from espresso_simulation.water_after_rest import PE_V7, Use, estimate_uses


def _metal(hasta_s: float, valor: float = 101.9, paso: float = 15.0):
    return [(i * paso, valor) for i in range(int(hasta_s / paso) + 1)]


def _cafes(inicio: float, n: int, separacion: float = 300.0):
    return [Use(inicio + k * separacion, 20.0, 3.25e-3, f"c{k}") for k in range(n)]


def test_tras_un_reposo_largo_el_primer_uso_sale_mas_frio_y_luego_se_recupera():
    usos = _cafes(4 * 3600.0, 5)
    est = estimate_uses(_metal(4 * 3600.0 + 1500.0), usos, 23.0)
    medias = [e.mean_water_c for e in est]
    assert medias[0] < medias[-1] - 1.0
    assert medias == sorted(medias)
    assert [e.use_index for e in est] == [1, 2, 3, 4, 5]


def test_un_reposo_corto_no_vuelve_a_enfriar():
    primera = _cafes(4 * 3600.0, 5)
    tras_corto = Use(4 * 3600.0 + 4 * 300.0 + 120.0, 20.0, 3.25e-3, "corto")
    est = estimate_uses(_metal(4 * 3600.0 + 1800.0), [*primera, tras_corto], 23.0)
    assert est[-1].use_index == 6
    assert est[-1].mean_water_c >= est[0].mean_water_c


def test_nunca_pasa_del_hervor_local():
    est = estimate_uses(_metal(3600.0, valor=130.0), _cafes(1800.0, 3), 23.0)
    assert all(e.mean_water_c <= PE_V7.boiling_c for e in est)


def test_sin_metal_no_hay_estimacion():
    assert estimate_uses([], _cafes(10.0, 1), 23.0) == []


def test_la_pared_se_lee_tambien_en_el_ultimo_instante():
    """La rejilla de 0,5 s arranca en la primera lectura: el final cae entre pasos."""
    from espresso_simulation.water_after_rest import wall_temperatures

    metal = [(0.3, 101.9), (3600.0, 101.9)]
    (final,) = wall_temperatures(metal, [], 24.0, [3600.0])
    assert final is not None

