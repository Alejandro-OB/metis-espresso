"""La tolerancia que dan las repeticiones de la receta.

Comprueban propiedades: que todos los cafés quedan dentro de la banda, que una
banda nula no existe, que lo que el perfil no declara toma la media, que un
intervalo no se toca y que un valor que falta no se rellena.
"""

from espresso_profiles.dispersion import tolerance_from_repetitions

DOSE = {"property": "coffee-dose", "unit": "g", "strategy": "target", "target": 18.0,
        "tolerance": 0.5}
MASS = {"property": "beverage-mass", "unit": "g", "strategy": "target", "target": 30.0,
        "tolerance": 2.0}
TIME = {"property": "extraction-time", "unit": "s", "strategy": "range",
        "minimum": 18.0, "maximum": 30.0}
WATER = {"property": "water-temperature", "unit": "°C", "strategy": "target",
         "target": 92.0, "tolerance": 1.0}


def _spec(result, prop):
    (item,) = [s for s in result["specifications"] if s["property"] == prop]
    return item


def _detail(result, prop):
    (item,) = [d for d in result["details"] if d["property"] == prop]
    return item


def _values(**overrides):
    values = {
        "coffee-dose": [18.0, 18.1, 17.9],
        "beverage-mass": [29.2, 30.5, 31.1],
        "extraction-time": [24.31, 25.02, 24.8],
        "tds": [9.12, 9.4, 9.25],
    }
    values.update(overrides)
    return values


def test_every_repetition_falls_inside_its_band():
    values = _values()
    result = tolerance_from_repetitions([DOSE, MASS, TIME, WATER], values)
    for prop in ("coffee-dose", "beverage-mass", "tds"):
        spec = _spec(result, prop)
        assert all(abs(v - spec["target"]) <= spec["tolerance"] + 1e-9 for v in values[prop])
    assert _spec(result, "beverage-mass")["tolerance"] == 1.1


def test_declared_objective_is_kept():
    result = tolerance_from_repetitions([DOSE, MASS], _values())
    assert _spec(result, "coffee-dose")["target"] == 18.0
    assert _detail(result, "coffee-dose")["status"] == "updated"


def test_identical_repetitions_get_the_resolution_not_zero():
    result = tolerance_from_repetitions([DOSE], _values(**{"coffee-dose": [18.0, 18.0, 18.0]}))
    assert _spec(result, "coffee-dose")["tolerance"] > 0


def test_undeclared_magnitude_takes_the_mean():
    result = tolerance_from_repetitions([DOSE, MASS], _values())
    tds = _spec(result, "tds")
    assert tds["strategy"] == "target"
    assert min(9.12, 9.4, 9.25) <= tds["target"] <= max(9.12, 9.4, 9.25)
    assert _detail(result, "tds")["status"] == "added"


def test_range_is_left_untouched_and_its_spread_reported():
    result = tolerance_from_repetitions([TIME], _values())
    assert _spec(result, "extraction-time") == TIME
    detail = _detail(result, "extraction-time")
    assert detail["status"] == "range"
    assert all(abs(v - detail["mean"]) <= detail["spread"] + 1e-9
               for v in [24.31, 25.02, 24.8])


def test_missing_value_is_not_filled():
    result = tolerance_from_repetitions([DOSE, MASS], _values(tds=[9.1, None, 9.3]))
    assert all(s["property"] != "tds" for s in result["specifications"])
    assert _detail(result, "tds")["status"] == "missing"


def test_other_specifications_and_input_are_unchanged():
    specs = [DOSE, MASS, TIME, WATER]
    before = [dict(s) for s in specs]
    result = tolerance_from_repetitions(specs, _values())
    assert specs == before
    assert _spec(result, "water-temperature") == WATER
