"""La caldera se juzga contra su referencia y el agua estimada no se juzga (EKO 3.4.0).

Comprueban propiedades: que una caldera en la referencia que fija la placa es
conforme aunque esté muy por encima del agua pedida, que la subida del primer
uso mueve esa referencia, que la especificación del agua no queda como
faltante y que una estimación del agua nunca produce una desviación.
"""

from __future__ import annotations

from espresso_profiles.water_range import boiler_reference_c, judged_specifications
from espresso_semantics.extraction import extraction_graph, stable_iri
from espresso_semantics.reasoning import reason, technical_conformance

WATER = {"property": "water-temperature", "unit": "°C", "strategy": "target",
         "target": 92.0, "tolerance": 1.0}
EXTRACTION = {
    "id": "prep-caldera", "source": "measured", "barista": "barista-1", "coffeeId": "cafe-1",
    "startedAt": "2026-10-22T09:00:00Z", "finishedAt": "2026-10-22T09:00:20Z",
    "termination": "maximum-extraction-time-reached", "sampleCount": 40,
}


def _obs(key, prop, value, kind="calculated", **extra):
    return {"key": key, "property": prop, "unit": "°C", "value": value, "kind": kind,
            "resultTime": "2026-10-22T09:00:20Z", "fromSeries": "temperature",
            "method": f"urn:method:prueba:{key}", **extra}


def _assess(observations, offset=None):
    profile = {"id": "perfil-c", "status": "accepted",
               "specifications": judged_specifications([WATER], offset)}
    extraction = {**EXTRACTION, "boilerOffsetAppliedC": offset}
    graph = reason(extraction_graph(extraction, profile, observations))
    return technical_conformance(graph, stable_iri("extraction", "prep-caldera"))


def _boiler(minimum, maximum):
    return [_obs("temperature-minimum", "boiler-temperature", minimum),
            _obs("temperature-maximum", "boiler-temperature", maximum)]


def test_la_caldera_en_su_referencia_es_conforme_aunque_supere_el_agua():
    reference = boiler_reference_c(92.0)
    result = _assess(_boiler(reference - 0.5, reference + 0.5))
    assert reference > 100
    assert result["status"] == "conformant"


def test_la_caldera_lejos_de_su_referencia_no_es_conforme():
    reference = boiler_reference_c(92.0)
    result = _assess(_boiler(reference - 3.0, reference))
    assert result["status"] == "non-conformant"


def test_la_subida_del_primer_uso_mueve_la_referencia():
    reference = boiler_reference_c(92.0) + 3.7
    assert _assess(_boiler(reference - 0.5, reference + 0.5), 3.7)["status"] == "conformant"
    assert _assess(_boiler(reference - 0.5, reference + 0.5))["status"] == "non-conformant"


def test_el_agua_estimada_no_produce_desviacion_ni_falta():
    reference = boiler_reference_c(92.0)
    frio = _obs("water-temperature-estimate", "water-temperature", 89.0, kind="estimated")
    result = _assess([*_boiler(reference - 0.5, reference + 0.5), frio])
    assert result["status"] == "conformant"
    assert result["deviations"] == []
