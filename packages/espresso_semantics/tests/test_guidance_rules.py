"""Las reglas con fuente del razonador (EKO 3.4.0).

Comprueban propiedades sobre el mismo grafo que publica la plataforma: que cada
regla se activa con lo que su fuente pide y no sin ello, que cita su
heurística, que concluye un diagnóstico probable con recomendación, que la regla
más específica deja fuera a la general y que una estimación no basta donde la
regla pide un defecto percibido.
"""

from __future__ import annotations

from espresso_semantics.extraction import EDT, extraction_graph, stable_iri
from espresso_semantics.reasoning import reason

PROFILE = {
    "id": "perfil-reglas", "status": "accepted",
    "specifications": [
        {"property": "extraction-time", "unit": "s", "strategy": "range", "minimum": 18.0, "maximum": 22.0},
        {"property": "beverage-mass", "unit": "g", "strategy": "target", "target": 30.0, "tolerance": 2.0},
        {"property": "coffee-dose", "unit": "g", "strategy": "target", "target": 13.0, "tolerance": 0.5},
        {"property": "tds", "unit": "%", "strategy": "target", "target": 9.0, "tolerance": 0.6},
        {"property": "water-temperature", "unit": "°C", "strategy": "target", "target": 92.0, "tolerance": 1.0},
    ],
}
RECIPE = {"time": 20.0, "mass": 30.0, "dose": 13.0, "tds": 9.0}


def _extraction(identifier, minute=0, sensory=None):
    return {
        "id": identifier, "source": "simulation", "barista": "barista-1", "coffeeId": "cafe-1",
        "startedAt": f"2026-10-22T09:{minute:02d}:00Z", "finishedAt": f"2026-10-22T09:{minute:02d}:25Z",
        "termination": "maximum-extraction-time-reached", "sampleCount": 0,
        "sensoryEvaluation": sensory,
    }


def _obs(key, prop, unit, value, kind="measured", **extra):
    return {"key": key, "property": prop, "unit": unit, "value": value, "kind": kind,
            "resultTime": "2026-10-22T09:00:25Z", **extra}


def _observations(time=None, mass=None, dose=None, tds=None, water=None):
    values = {**RECIPE, **{k: v for k, v in
                           {"time": time, "mass": mass, "dose": dose, "tds": tds}.items() if v is not None}}
    result = [_obs("extraction-time", "extraction-time", "s", values["time"]),
              _obs("beverage-mass", "beverage-mass", "g", values["mass"]),
              _obs("coffee-dose", "coffee-dose", "g", values["dose"]),
              _obs("tds", "tds", "%", values["tds"])]
    if water is not None:
        result.append(_obs("water-estimate", "water-temperature", "°C", water, kind="estimated",
                           computedFrom=["extraction-time"], method="urn:method:prueba"))
    return result


def _sensory(defects=(), bitterness=None, identifier="cata"):
    scores = [{"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 4}]
    if bitterness is not None:
        scores.append({"attribute": "Bitterness", "dimension": "perceived-intensity", "value": bitterness})
    return {"id": identifier, "evaluatedBy": "barista-1", "evaluatedAt": "2026-10-22T09:05:00Z",
            "scores": scores, "defects": list(defects)}


def _heuristics(sensory=None, **observed):
    graph = reason(extraction_graph(_extraction("prep", sensory=sensory), PROFILE, _observations(**observed)))
    extraction = stable_iri("extraction", "prep")
    found = set()
    for recommendation in graph.objects(extraction, EDT.hasRecommendedAction):
        found |= set(graph.objects(recommendation, EDT.supportedByHeuristic))
    return found


def test_la_masa_fuera_de_la_receta_se_atribuye_primero_a_la_proporcion():
    assert EDT.BeverageMassFirstHeuristic in _heuristics(mass=36.0)
    assert EDT.BeverageMassFirstHeuristic not in _heuristics()


def test_cortado_antes_deja_fuera_a_la_masa_primero():
    found = _heuristics(time=13.0, mass=22.0)
    assert EDT.ShortShotHeuristic in found
    assert EDT.BeverageMassFirstHeuristic not in found


def test_dosis_y_tds_en_el_mismo_sentido_es_concentracion_no_extraccion():
    assert EDT.DoseConcentrationHeuristic in _heuristics(dose=10.0, tds=7.0)
    # Si el TDS no se movió con la dosis, la regla no se activa.
    assert EDT.DoseConcentrationHeuristic not in _heuristics(dose=10.0)


def test_dosis_alta_con_amargor_sobre_7_5_es_exceso_de_cafe():
    assert EDT.HighDoseTasteHeuristic in _heuristics(_sensory(bitterness=8), dose=14.5)
    assert EDT.HighDoseTasteHeuristic not in _heuristics(_sensory(bitterness=7), dose=14.5)
    assert EDT.HighDoseTasteHeuristic not in _heuristics(_sensory(bitterness=8))


def test_agua_estimada_dos_grados_abajo_con_acidez_es_temperatura_baja():
    acid = _sensory(defects=["ExcessiveAcidity"])
    assert EDT.ColdWaterAcidityHeuristic in _heuristics(acid, water=89.5)
    assert EDT.ColdWaterAcidityHeuristic not in _heuristics(acid, water=91.0)
    # Sin acidez percibida, una estimación fría no basta.
    assert EDT.ColdWaterAcidityHeuristic not in _heuristics(water=89.5)


def test_amargo_y_largo_acorta_primero_el_tiempo():
    bitter = _sensory(defects=["ExcessiveBitterness"])
    assert EDT.LongBitterShotHeuristic in _heuristics(bitter, time=26.0)
    assert EDT.LongBitterShotHeuristic not in _heuristics(bitter)


def test_una_diferencia_dentro_de_la_tolerancia_no_se_atribuye():
    graph = extraction_graph(_extraction("ref", 0, _sensory(identifier="cata-ref")), PROFILE, _observations())
    graph += extraction_graph(_extraction("prep", 10, _sensory(defects=["ExcessiveBitterness"])),
                              PROFILE, _observations(time=21.0, mass=31.0))
    graph = reason(graph)
    extraction = stable_iri("extraction", "prep")
    found = set()
    for recommendation in graph.objects(extraction, EDT.hasRecommendedAction):
        found |= set(graph.objects(recommendation, EDT.supportedByHeuristic))
    assert EDT.WithinDispersionHeuristic in found


def test_una_diferencia_mayor_que_la_tolerancia_si_se_analiza():
    graph = extraction_graph(_extraction("ref", 0, _sensory(identifier="cata-ref")), PROFILE, _observations())
    graph += extraction_graph(_extraction("prep", 10, _sensory(defects=["ExcessiveBitterness"])),
                              PROFILE, _observations(time=21.9, mass=27.5))
    graph = reason(graph)
    used = set(graph.objects(None, EDT.supportedByHeuristic))
    assert EDT.WithinDispersionHeuristic not in used


def test_ninguna_recomendacion_ajusta_lo_que_la_maquina_no_comprueba():
    # La molienda y la cama de café quedan como contexto registrado: el
    # razonador no recomienda sobre ellas.
    graph = reason(extraction_graph(_extraction("prep", sensory=_sensory(defects=["ExcessiveBitterness"])),
                                    PROFILE, _observations(time=26.0, mass=36.0, dose=14.5, tds=10.0)))
    adjusted = {
        prop
        for action in graph.subjects(EDT.adjustsProperty, None)
        for prop in graph.objects(action, EDT.adjustsProperty)
    }
    assert adjusted, "la preparación fuera de la receta debía recibir alguna recomendación"
    assert EDT.GrindSetting not in adjusted
    assert EDT.CoffeeBedCondition not in adjusted


def test_lo_que_concluyen_las_reglas_cumple_las_formas():
    from espresso_semantics.reasoning import validate_conclusions

    cases = [
        (_sensory(defects=["ExcessiveBitterness"], bitterness=8), {"time": 26.0, "dose": 14.5}),
        (_sensory(defects=["ExcessiveAcidity"]), {"water": 89.5}),
        (None, {"time": 13.0, "mass": 22.0}),
        (None, {"dose": 10.0, "tds": 7.0}),
    ]
    for sensory, observed in cases:
        preparations = extraction_graph(_extraction("prep", sensory=sensory), PROFILE,
                                        _observations(**observed))
        conforms, report = validate_conclusions(reason(preparations), preparations)
        assert conforms, report
