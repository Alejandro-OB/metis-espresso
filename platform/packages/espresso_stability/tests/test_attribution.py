"""Un ajuste solo se da por probado si no cambió también el agua o la presión."""

from espresso_stability.attribution import attribution, regime_pressure_bar

LINE = "recta-v12"


def _prep(water=92.0, pressure=8.5, method=LINE, dose=13.0, mass=38.0):
    return {"waterC": water, "waterMethod": method, "pressureBar": pressure,
            "coffeeDoseG": dose, "beverageMassG": mass}


def test_sin_cambios_fuera_de_banda_el_resultado_es_atribuible():
    result = attribution(_prep(), _prep(water=92.3, pressure=8.45), "extraction-time")
    assert result["status"] == "attributable"


def test_el_agua_que_cambia_mas_que_la_banda_lo_vuelve_no_atribuible():
    # Café 4 → 5 de la catación del 4-nov: 90,96 → 92,42 °C con un ajuste de tiempo.
    result = attribution(_prep(water=90.96), _prep(water=92.42), "extraction-time")
    assert result["status"] == "not-attributable"
    water = next(check for check in result["checks"] if check["condition"] == "water")
    assert water["status"] == "changed"
    assert water["difference"] == 1.46


def test_la_presion_que_cambia_mas_que_la_banda_lo_vuelve_no_atribuible():
    assert attribution(_prep(), _prep(pressure=8.8), None)["status"] == "not-attributable"


def test_lo_que_el_barista_ajusto_no_se_juzga():
    result = attribution(_prep(water=90.0), _prep(water=92.0), "water-temperature")
    assert result["status"] == "attributable"
    assert result["checks"][0]["status"] == "adjusted"


def test_sin_un_dato_el_resultado_queda_indeterminado_nunca_atribuible():
    assert attribution(_prep(water=None), _prep(), None)["status"] == "undetermined"


def test_aguas_de_rectas_distintas_no_se_restan():
    result = attribution(_prep(method="recta-v6"), _prep(), None)
    assert result["checks"][0]["status"] == "line-changed"
    assert result["status"] == "undetermined"


def test_masa_y_dosis_se_informan_sin_veredicto():
    result = attribution(_prep(mass=30.0), _prep(mass=40.0), "extraction-time")
    assert result["status"] == "attributable"
    assert {item["condition"] for item in result["reported"]} == {"coffeeDoseG", "beverageMassG"}


def test_la_presion_de_regimen_empieza_al_entrar_en_la_banda():
    samples = [{"elapsedMs": i * 500, "pressureBar": p}
               for i, p in enumerate([2.0, 5.0, 7.0, 8.4, 8.5, 8.6, 8.5, 8.5])]
    # Promedios de 1 s: 3,5, 7,7, 8,55 y 8,5; el tramo empieza en el tercero.
    assert regime_pressure_bar(samples, 8.5) == 8.525
    assert regime_pressure_bar(samples, None) is None
    assert regime_pressure_bar([{"elapsedMs": 0, "pressureBar": 3.0}], 8.5) is None
