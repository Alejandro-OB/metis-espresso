"""La especificación de caldera que se deriva del agua, con la recta de cada café.

Comprueban propiedades: que la referencia es la que fija la placa, que la
subida del primer uso entra en ella, que lo que no consta no se inventa y que
las especificaciones de entrada no cambian.
"""

from espresso_profiles.water_range import (
    CURRENT,
    LINE_V6,
    LINE_V11,
    WATER_GAIN,
    boiler_reference_c,
    judged_specifications,
    line_of,
    metal_line_of,
)

WATER = {"property": "water-temperature", "unit": "°C", "strategy": "target",
         "target": 92.0, "tolerance": 1.0}
TIME = {"property": "extraction-time", "unit": "s", "strategy": "range",
        "minimum": 18.0, "maximum": 20.0}


def _boiler(specs):
    (boiler,) = [item for item in specs if item["property"] == "boiler-temperature"]
    return boiler


def test_la_caldera_se_juzga_contra_la_referencia_de_la_placa():
    boiler = _boiler(judged_specifications([WATER, TIME]))
    assert boiler["target"] == round(boiler_reference_c(92.0), 2)
    assert boiler["tolerance"] == round(1.0 / WATER_GAIN, 2)
    assert boiler["derivedFrom"] == 0


def test_la_subida_del_primer_uso_entra_en_la_referencia():
    sin, con = (_boiler(judged_specifications([WATER], offset)) for offset in (None, 3.7))
    assert round(con["target"] - sin["target"], 2) == 3.7


def test_sin_tolerancia_del_agua_no_hay_tolerancia_de_caldera():
    boiler = _boiler(judged_specifications([{**WATER, "tolerance": None}]))
    assert boiler["tolerance"] is None


def test_un_rango_se_convierte_extremo_por_extremo():
    water = {"property": "water-temperature", "unit": "°C", "strategy": "range",
             "minimum": 91.0, "maximum": 93.0}
    boiler = _boiler(judged_specifications([water]))
    assert boiler["minimum"] == round(boiler_reference_c(91.0), 2)
    assert boiler["maximum"] == round(boiler_reference_c(93.0), 2)


def test_sin_agua_no_se_deriva_nada_y_la_entrada_no_cambia():
    specs = [TIME]
    assert judged_specifications(specs) == [TIME]
    judged_specifications([WATER])
    assert WATER == {"property": "water-temperature", "unit": "°C", "strategy": "target",
                     "target": 92.0, "tolerance": 1.0}


def test_cada_cafe_se_juzga_con_la_recta_con_que_lo_hizo_la_placa():
    """Un café anterior a la v11 no trae recta: se hizo con la v6, y con ella se juzga."""
    assert line_of({"id": "sim-1"}) == LINE_V6
    declared = {"plant": "acta-temperatura-20261028-v11", "gain": 0.8819, "offsetC": -0.49,
                "boilerMinimumC": 97.3, "boilerMaximumC": 105.6}
    assert line_of({"waterReference": declared}) == LINE_V11
    old = _boiler(judged_specifications([WATER], None, line_of({})))
    new = _boiler(judged_specifications([WATER], None, line_of({"waterReference": declared})))
    assert old["target"] == round(LINE_V6.boiler_reference_c(92.0), 2)
    assert new["target"] == round(LINE_V11.boiler_reference_c(92.0), 2)
    assert old["method"] == "acta-temperatura-v6"
    assert new["method"] == "acta-temperatura-20261028-v11"


def test_sin_recta_que_conste_no_se_deriva_la_caldera():
    """A null la recta no consta: no se le atribuye la vigente ni la v6."""
    for declared in (None, {"plant": "x"}, {**dict.fromkeys(
            ("plant", "gain", "offsetC", "boilerMinimumC", "boilerMaximumC"), 1.0)}):
        assert line_of({"waterReference": declared}) is None
    specs = judged_specifications([WATER, TIME], None, None)
    assert [item["property"] for item in specs] == ["water-temperature", "extraction-time"]


def test_desde_la_v12_el_agua_se_estima_con_la_recta_del_metal():
    """La de conversión lleva el repunte tras el café; desde el metal vale la de la v11."""
    v12 = {"plant": CURRENT.method, "gain": CURRENT.gain, "offsetC": CURRENT.offset_c,
           "boilerMinimumC": CURRENT.boiler_minimum_c, "boilerMaximumC": CURRENT.boiler_maximum_c}
    assert line_of({"waterReference": v12}) == CURRENT
    assert metal_line_of({"waterReference": v12}) == LINE_V11
    assert metal_line_of({}) == LINE_V6
    assert metal_line_of({"waterReference": None}) is None
    # 92 °C piden 103,81 de referencia; con el metal en ella el agua estimada sería
    # más baja, porque el metal llega más alto que la referencia.
    assert round(CURRENT.boiler_reference_c(92.0), 2) == 103.81
