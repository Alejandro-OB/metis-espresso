from __future__ import annotations

import pytest
from espresso_semantics.extraction import (
    EDT,
    SHAPES,
    SemanticProjectionError,
    extraction_graph,
    specification_iri,
    stable_iri,
    validated_jsonld,
)
from pyshacl import validate

PROFILE = {
    "id": "perfil-placa",
    "status": "accepted",
    "specifications": [
        {
            "property": "temperature",
            "unit": "°C",
            "strategy": "target",
            "target": 93.0,
            "tolerance": 0.5,
        }
    ],
}


def closed_extraction(**changes) -> dict:
    return {
        "id": "prep-0001",
        "barista": "barista-1",
        "coffeeId": "cafe-1",
        "startedAt": "2026-10-12T09:00:00Z",
        "finishedAt": "2026-10-12T09:00:28Z",
        "termination": "manual-normal-stop",
        "sampleCount": 56,
        **changes,
    }


def power_loss_extraction(**changes) -> dict:
    """Lo que deja el dispositivo al rearrancar tras un corte: ni fin ni muestras."""
    return closed_extraction(
        finishedAt=None,
        termination="unexpected-power-loss",
        sampleCount=0,
        seriesRef="serie-que-no-llego",
        **changes,
    )


def test_a_power_loss_extraction_is_published_as_incomplete_without_inventing_times():
    document = validated_jsonld(power_loss_extraction(startedAt=None), PROFILE)
    assert document["validation"]["conforms"] is True

    graph = extraction_graph(power_loss_extraction(startedAt=None), PROFILE)
    extraction = stable_iri("extraction", "prep-0001")
    assert graph.value(extraction, EDT.hasTerminationCause) == EDT.UnexpectedPowerLoss
    assert graph.value(extraction, EDT.startTime) is None
    assert graph.value(extraction, EDT.endTime) is None
    # Sin muestras no se afirma una serie, aunque el registro traiga su referencia.
    assert graph.value(extraction, EDT.hasObservationSeries) is None


def test_a_power_loss_extraction_keeps_its_start_when_the_device_had_one():
    graph = extraction_graph(power_loss_extraction(), PROFILE)
    extraction = stable_iri("extraction", "prep-0001")
    assert graph.value(extraction, EDT.startTime) is not None
    assert graph.value(extraction, EDT.endTime) is None
    assert validated_jsonld(power_loss_extraction(), PROFILE)["validation"]["conforms"]


@pytest.mark.parametrize("missing", ["startedAt", "finishedAt"])
def test_any_other_extraction_without_start_or_end_fails_the_shapes(missing):
    with pytest.raises(SemanticProjectionError) as error:
        validated_jsonld(closed_extraction(**{missing: None}), PROFILE)
    assert ("startTime" if missing == "startedAt" else "endTime") in error.value.report


def test_an_unknown_termination_is_not_projected():
    graph = extraction_graph(closed_extraction(termination="no-definida"), PROFILE)
    assert graph.value(stable_iri("extraction", "prep-0001"), EDT.hasTerminationCause) is None


def test_point_observations_are_published_with_eko_units_and_pass_the_shapes():
    """El razonador compara unidad con unidad: perfil y observación deben coincidir."""
    observations = [
        {"key": "coffee-dose", "property": "coffee-dose", "unit": "g", "value": 13.0,
         "kind": "measured", "resultTime": "2026-10-12T09:02:00Z",
         "recordedBy": "barista-1", "recordedAt": "2026-10-12T09:02:00Z"},
        {"key": "temperature-maximum", "property": "temperature", "unit": "°C",
         "value": 93.2, "kind": "calculated", "resultTime": "2026-10-12T09:00:28Z",
         "fromSeries": "temperature", "method": "urn:method:series-window-extreme:maximum"},
    ]
    extraction = closed_extraction(seriesRef="serie-1")
    validated_jsonld(extraction, PROFILE, observations)
    graph = extraction_graph(extraction, PROFILE, observations)
    node = stable_iri("observation", "prep-0001-temperature-maximum")
    assert (stable_iri("extraction", "prep-0001"), EDT.hasObservation, node) in graph
    assert (node, EDT.hasUnit, EDT.DegreeCelsius) in graph
    specification = specification_iri("perfil-placa", PROFILE["specifications"][0])
    assert (specification, EDT.hasUnit, EDT.DegreeCelsius) in graph


def test_a_calculation_without_its_operands_fails_the_shapes():
    observation = {"key": "brew-ratio", "property": "brew-ratio", "unit": ":1",
                   "value": 2.0, "kind": "calculated", "resultTime": "2026-10-12T09:02:00Z",
                   "method": "urn:method:brew-ratio:beverage-over-dose"}
    with pytest.raises(SemanticProjectionError):
        validated_jsonld(closed_extraction(), PROFILE, [observation])


def test_the_tasting_is_published_with_the_barista_declared_acceptance():
    evaluation = {
        "id": "cata-1",
        "evaluatedBy": "barista-1",
        "evaluatedAt": "2026-10-12T09:03:00Z",
        "scores": [
            {"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 3},
            {"attribute": "Acidity", "dimension": "perceived-intensity", "value": 8},
        ],
        "defects": ["ExcessiveAcidity"],
        "acceptance": {"status": "rejected", "rationale": "Ácido de más."},
    }
    bed = {"key": "bed-condition", "property": "coffee-bed-condition", "text": "uneven",
           "resultTime": "2026-10-12T09:02:00Z"}
    extraction = closed_extraction(sensoryEvaluation=evaluation)
    validated_jsonld(extraction, PROFILE, [bed])
    graph = extraction_graph(extraction, PROFILE, [bed])
    node = stable_iri("sensory-evaluation", "cata-1")
    assert (node, EDT.hasPerceivedDefect, EDT.ExcessiveAcidity) in graph
    assert (node, EDT.hasSensoryAcceptanceStatus, EDT.SensoryRejected) in graph


def test_the_tasting_carries_what_the_cva_form_marks_apart():
    """Fragancia, intensidad hasta 15, sabores básicos y sensación en boca (EKO 3.5.0)."""
    evaluation = {
        "id": "cata-cva",
        "evaluatedBy": "barista-1",
        "evaluatedAt": "2026-10-23T09:03:00Z",
        "scores": [
            {"attribute": "Fragrance", "dimension": "perceived-intensity", "value": 13},
            {"attribute": "Mouthfeel", "dimension": "hedonic-liking", "value": 7},
        ],
        "mainTastes": ["Bitter", "Sweet"],
        "mouthfeelTextures": ["Smooth"],
        "defects": ["Phenolic"],
        "acceptance": {"status": "accepted"},
    }
    extraction = closed_extraction(sensoryEvaluation=evaluation)
    validated_jsonld(extraction, PROFILE)
    graph = extraction_graph(extraction, PROFILE)
    node = stable_iri("sensory-evaluation", "cata-cva")
    assert (node, EDT.hasMainTaste, EDT.BitterTaste) in graph
    assert (node, EDT.hasMainTaste, EDT.SweetTaste) in graph
    assert (node, EDT.hasMouthfeelTexture, EDT.SmoothMouthfeel) in graph
    assert (node, EDT.hasPerceivedDefect, EDT.Phenolic) in graph


def test_a_tasting_without_scores_is_not_published():
    extraction = closed_extraction(sensoryEvaluation={"id": "c", "evaluatedAt": "x", "scores": []})
    graph = extraction_graph(extraction, PROFILE)
    assert not list(graph.subjects(None, EDT.SensoryEvaluation))


def _competency_question(name: str) -> str:
    """El texto de una pregunta de competencia, tal cual en el repositorio."""
    from pathlib import Path

    queries = (
        Path(__file__).resolve().parents[3]
        / "knowledge/ontology/queries/competency-questions.rq"
    ).read_text(encoding="utf-8")
    start = queries.index(f"# --- {name} ---")
    end = queries.find("# --- PC", start + 1)
    return queries[start:end if end != -1 else None]


def test_pc07_and_pc25_answer_with_a_tasting_from_the_app():
    """Lo que la ontología promete de la cata, con un café que llega por la app.

    Hasta EKO 3.5.0 la plataforma no escribía descriptores, cambios deseados,
    la descripción del tostador ni el sabor buscado: las dos preguntas solo
    respondían con los casos escritos a mano en RDF.
    """
    evaluation = {
        "id": "cata-pc",
        "evaluatedBy": "barista-1",
        "evaluatedAt": "2026-10-23T09:03:00Z",
        "scores": [{"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 6}],
        "descriptors": ["Chocolate", "Citrus"],
        "descriptorRatings": [
            {"descriptor": "Chocolate", "intensity": 9},
            {"descriptor": "Citrus", "intensity": 4},
        ],
        "otherDescriptors": [{"name": "naranja", "intensity": 4}],
        "defects": ["ExcessiveBitterness"],
        "desiredChanges": [{"attribute": "Bitterness", "direction": "Decrease"}],
        "acceptance": {"status": "rejected", "rationale": "Amargo de más."},
    }
    references = {
        "declared": {
            "id": "declarado-1",
            "declaredBy": "Tostador",
            "source": "Etiqueta",
            "originalText": "Chocolate, panela y cítricos",
            "descriptors": ["Chocolate", "panela"],
        },
        "target": {
            "id": "objetivo-1",
            "coffeeId": "cafe-1",
            "definedBy": "barista-1",
            "rationale": "Dulce y achocolatado",
            "descriptors": ["Chocolate"],
            "otherDescriptors": ["azúcar morena"],
        },
    }
    extraction = closed_extraction(sensoryEvaluation=evaluation)
    validated_jsonld(extraction, PROFILE, references=references)
    graph = extraction_graph(extraction, PROFILE, references=references)

    pc07 = list(graph.query(_competency_question("PC07")))
    assert {str(row.descriptor) for row in pc07} == {str(EDT.Chocolate), str(EDT.Citrus)}
    assert {str(row.desiredAttribute) for row in pc07} == {str(EDT.Bitterness)}
    assert {str(row.direction) for row in pc07} == {str(EDT.Decrease)}

    pc25 = list(graph.query(_competency_question("PC25")))
    # La extracción usa el café y su dosis: las filas de la dosis no traen
    # descripción del tostador, y por eso se miran solo los valores presentes.
    def present(column: str) -> set[str]:
        return {str(getattr(row, column)) for row in pc25 if getattr(row, column) is not None}

    assert present("declaredDescriptor") == {str(EDT.Chocolate), str(EDT.Panela)}
    assert present("targetDescriptor") == {str(EDT.Chocolate)}
    assert present("observedDescriptor") == {str(EDT.Chocolate), str(EDT.Citrus)}

    node = stable_iri("sensory-evaluation", "cata-pc")
    written = {str(value) for value in graph.objects(node, EDT.writtenDescriptor)}
    assert written == {"naranja"}
    target = stable_iri("target-sensory-profile", "objetivo-1")
    assert {str(value) for value in graph.objects(target, EDT.writtenDescriptor)} == {"azúcar morena"}


def test_a_sensory_profile_without_vocabulary_is_not_published():
    """Un perfil sensorial necesita un descriptor de EKO; solo texto sería una nota."""
    references = {"target": {"id": "solo-texto", "definedBy": "barista-1",
                             "descriptors": [], "otherDescriptors": ["naranja"]}}
    extraction = closed_extraction()
    validated_jsonld(extraction, PROFILE, references=references)
    graph = extraction_graph(extraction, PROFILE, references=references)
    assert (None, EDT.hasTargetSensoryProfile, None) not in graph


def test_pc01_pc16_pc17_and_pc18_answer_with_a_preparation_from_the_app():
    """Equipos, ficha del café y cuarto, con lo que la plataforma guarda (EKO 3.5.0)."""
    references = {
        "coffee": {
            "id": "cafe-1", "commercialReference": "Kajugui", "variety": "Castillo",
            "postHarvestProcess": "Lavado", "roastLevel": "Medio", "region": "Bolívar, Cauca",
            "altitudeM": 1800, "roastDate": "2026-10-01",
        },
        "equipment": [
            {"id": "maquina-1", "type": "machine", "name": "Kalley K-EX", "capabilities": [
                {"property": "water-temperature", "manualAdjustment": False, "automaticAdjustment": True},
            ]},
            {"id": "molino-1", "type": "grinder", "name": "Havmore", "capabilities": [
                {"property": "grind-setting", "manualAdjustment": True, "automaticAdjustment": False},
            ]},
        ],
    }
    observations = [
        {"key": "ambient-temperature", "property": "ambient-temperature", "unit": "°C",
         "value": 23.4, "kind": "measured", "resultTime": "2026-10-12T09:00:28Z",
         "feature": "environment"},
        {"key": "relative-humidity", "property": "relative-humidity", "unit": "%",
         "value": 61, "kind": "measured", "resultTime": "2026-10-12T09:00:28Z",
         "feature": "environment"},
    ]
    extraction = closed_extraction()
    validated_jsonld(extraction, PROFILE, observations, references)
    graph = extraction_graph(extraction, PROFILE, observations, references)

    pc01 = list(graph.query(_competency_question("PC01")))
    assert {(str(row.machine), str(row.grinder)) for row in pc01} >= {
        (str(stable_iri("equipment", "maquina-1")), str(stable_iri("equipment", "molino-1")))
    }
    pc16 = list(graph.query(_competency_question("PC16")))
    coffee_rows = [row for row in pc16 if row.commercialReference is not None]
    assert {str(row.region) for row in coffee_rows} == {"Bolívar, Cauca"}
    assert {float(row.altitudeValue) for row in coffee_rows} == {1800.0}
    # Una fecha de tueste sin hora no se publica.
    assert all(row.roastDate is None for row in coffee_rows)
    pc17 = list(graph.query(_competency_question("PC17")))
    assert {float(row.value) for row in pc17} == {23.4, 61.0}
    pc18 = list(graph.query(_competency_question("PC18")))
    assert {str(row.automaticProperty) for row in pc18 if row.automaticProperty} == {
        str(EDT.WaterTemperature)
    }
    assert {str(row.manualProperty) for row in pc18 if row.manualProperty} == {
        str(EDT.GrindSetting)
    }


def test_pc21_answers_with_an_adjustment_checked_by_the_next_coffee():
    """La cadena café inicial → ajuste → café de comprobación, como la arma la app."""
    from espresso_semantics.extraction import adjustment_graph
    from espresso_semantics.reasoning import reason

    def observed(extraction: dict, seconds: float) -> list[dict]:
        return [{"key": "extraction-time", "property": "extraction-time", "unit": "s",
                 "value": seconds, "kind": "measured", "resultTime": extraction["finishedAt"]}]

    def tasting(identifier: str, defects: list[str]) -> dict:
        return {"id": identifier, "evaluatedBy": "barista-1", "evaluatedAt": "2026-10-12T09:05:00Z",
                "scores": [{"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 4}],
                "defects": defects, "acceptance": {"status": "inconclusive"}}

    initial = closed_extraction(id="prep-a", sensoryEvaluation=tasting("cata-a", ["ExcessiveBitterness"]))
    follow_up = closed_extraction(
        id="prep-b", startedAt="2026-10-12T09:10:00Z", finishedAt="2026-10-12T09:10:25Z",
        sensoryEvaluation=tasting("cata-b", []),
    )
    episode = {
        "id": "ajuste-1", "initialExtractionId": "prep-a", "followUpExtractionId": "prep-b",
        "adjustment": {"id": "ajuste-1", "property": "extraction-time", "previousValue": 28,
                       "newValue": 25, "unit": "s", "occurredAt": "2026-10-12T09:06:00Z",
                       "performedBy": "barista-1"},
        "targetOutcome": {"kind": "sensory-defect", "code": "ExcessiveBitterness"},
    }
    graph = (
        extraction_graph(initial, PROFILE, observed(initial, 28))
        + extraction_graph(follow_up, PROFILE, observed(follow_up, 25))
        + adjustment_graph(episode)
    )
    # El vocabulario trae sus propios individuos de demostración: se miran
    # solo las filas de este ajuste.
    rows = [
        row for row in reason(graph).query(_competency_question("PC21"))
        if row.adjustment == stable_iri("adjustment", "ajuste-1")
    ]
    assert rows
    assert {str(row.evaluationExtraction) for row in rows} == {str(stable_iri("extraction", "prep-b"))}
    assert {str(row.outcomeTarget) for row in rows} == {str(EDT.ExcessiveBitterness)}


def test_two_lists_of_the_same_profile_do_not_share_a_specification_name():
    """Un café real publica el resumen de la placa más la caldera; otro, el perfil
    completo. En la catación del 2026-11-04 la tercera de cada lista se llamaba
    igual (`perfil-2`) y, al juntar los grafos, quedaba con objetivo y rango a la
    vez: SHACL rechazó las recomendaciones. Juntos tienen que cumplir las formas.
    """
    water = {"property": "water-temperature", "unit": "°C", "strategy": "target",
             "target": 92.0, "tolerance": 1.0}
    pressure = {"property": "extraction-pressure", "unit": "bar", "strategy": "target",
                "target": 8.5, "tolerance": 0.5}
    boiler = {"property": "boiler-temperature", "unit": "°C", "strategy": "target",
              "target": 103.81, "tolerance": 1.1, "derivedFrom": 0, "method": "recta"}
    time = {"property": "extraction-time", "unit": "s", "strategy": "range",
            "minimum": 18.0, "maximum": 20.0}
    summary = {"id": "perfil-v4", "status": "accepted",
               "specifications": [water, pressure, boiler]}
    complete = {"id": "perfil-v4", "status": "accepted",
                "specifications": [water, pressure, time]}
    first = extraction_graph(closed_extraction(id="prep-a"), summary, [])
    second = extraction_graph(closed_extraction(id="prep-b"), complete, [])
    together = first + second
    conforms, _, report = validate(together, shacl_graph=str(SHAPES), advanced=True)
    assert conforms, report
    assert specification_iri("perfil-v4", boiler) != specification_iri("perfil-v4", time)
    shared = specification_iri("perfil-v4", water)
    assert shared in set(first.objects(None, EDT.hasParameterSpecification))
    assert shared in set(second.objects(None, EDT.hasParameterSpecification))
    assert (specification_iri("perfil-v4", boiler), EDT.derivedFromSpecification,
            specification_iri("perfil-v4", water)) in first


def test_the_boiler_specification_of_two_cafes_with_different_raises_differs():
    """La de caldera lleva la subida del primer uso, que cambia de un café a otro."""
    base = {"property": "boiler-temperature", "unit": "°C", "strategy": "target",
            "tolerance": 1.1, "derivedFrom": 0, "method": "recta"}
    assert specification_iri("p", {**base, "target": 103.81}) != specification_iri(
        "p", {**base, "target": 106.81})


def test_the_same_specification_named_with_two_property_texts_shares_its_name():
    """El perfil completo dice «extraction-pressure» y el resumen de un café real,
    «pressure»: son la misma especificación y deben llamarse igual (PC04)."""
    full = {"property": "extraction-pressure", "unit": "bar", "strategy": "target",
            "target": 8.5, "tolerance": 0.5}
    summary = {"property": "pressure", "unit": "bar", "strategy": "target",
               "target": 8.50, "tolerance": 0.5}
    assert specification_iri("p", full) == specification_iri("p", summary)
    assert specification_iri("p", full) != specification_iri("p", {**full, "target": 9.0})
