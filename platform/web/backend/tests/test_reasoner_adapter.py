"""«Analizar» propone lo que el razonador concluye, o se abstiene diciéndolo."""

from espresso_semantics.extraction import EDT, extraction_graph
from rdflib import Graph

from aplicacion_web.adapters.reasoner import ReasonerRecommendationAdapter

PROFILE = {
    "id": "perfil-a",
    "status": "accepted",
    "specifications": [
        {"property": "pressure", "unit": "bar", "strategy": "target",
         "target": 9.0, "tolerance": 0.5},
    ],
}
EXTRACTION = {
    "id": "prep-a",
    "barista": "barista-1",
    "coffeeId": "cafe-1",
    "startedAt": "2026-10-16T09:00:00Z",
    "finishedAt": "2026-10-16T09:00:28Z",
    "termination": "manual-normal-stop",
}


def graph_with_pressure(value):
    return extraction_graph(EXTRACTION, PROFILE, [
        {"key": "pressure-maximum", "property": "pressure", "unit": "bar", "value": value,
         "kind": "measured", "resultTime": "2026-10-16T09:00:28Z"},
    ])


def test_a_deviation_becomes_a_test_to_verify_it_never_a_command():
    adapter = ReasonerRecommendationAdapter("3.5.0")
    result = adapter.recommend("prep-a", [{"id": "fact:1"}], lambda: graph_with_pressure(10.2))
    assert result["origin"] == "reasoner"
    assert result["status"] == "pending-human-review"
    assert result["requiresHumanReview"] is True
    assert result["actuatorCommand"] is None
    assert result["proposedAdjustment"] == {
        "property": "pressure", "direction": "maintain", "value": 9.0, "unit": "bar"}
    assert result["diagnosis"]["summary"]
    assert result["summary"].startswith("Repetir")
    assert result["conclusionValidation"]["conforms"] is True
    conclusions = Graph().parse(data=result["conclusionGraph"], format="turtle")
    assert list(conclusions.subjects(None, EDT.RecommendedAction))
    # Las conclusiones de las demás preparaciones del vocabulario no viajan.
    assert len({str(s) for s in conclusions.subjects(EDT.assessesExtraction, None)}) == 1


def test_without_a_conclusion_it_abstains_and_says_so():
    adapter = ReasonerRecommendationAdapter("3.5.0")
    result = adapter.recommend("prep-a", [], lambda: graph_with_pressure(9.1))
    assert result["status"] == "abstained"
    assert result["reasonCode"] == "NO_REASONER_CONCLUSION"
    assert result["proposedAdjustment"] is None


def test_a_preparation_that_is_not_measured_is_not_reasoned_on():
    adapter = ReasonerRecommendationAdapter("3.5.0")
    result = adapter.recommend("prep-a", [], lambda: None)
    assert result["reasonCode"] == "PREPARATION_NOT_MEASURED"


def test_a_rule_with_a_source_gives_the_direction_and_goes_first():
    """Amargo y largo: la regla del barista propone acortar el tiempo (EKO 3.4.0)."""
    profile = {"id": "perfil-t", "status": "accepted", "specifications": [
        {"property": "extraction-time", "unit": "s", "strategy": "range",
         "minimum": 18.0, "maximum": 22.0}]}
    extraction = {**EXTRACTION, "id": "prep-t", "sensoryEvaluation": {
        "id": "cata-t", "evaluatedBy": "barista-1", "evaluatedAt": "2026-10-16T09:05:00Z",
        "scores": [{"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 3}],
        "defects": ["ExcessiveBitterness"]}}
    graph = extraction_graph(extraction, profile, [
        {"key": "extraction-time", "property": "extraction-time", "unit": "s", "value": 27.0,
         "kind": "measured", "resultTime": "2026-10-16T09:00:28Z"}])
    result = ReasonerRecommendationAdapter("3.5.0").recommend("prep-t", [], lambda: graph)
    assert result["proposedAdjustment"]["property"] == "extraction-time"
    assert result["proposedAdjustment"]["direction"] == "decrease"
    assert result["summary"].startswith("Acortar")
    # La regla viaja con su fuente para que el barista sepa de dónde sale.
    assert result["heuristic"]["source"]
    assert result["heuristic"]["description"]
    validation = result["conclusionValidation"]
    assert validation["conforms"] is True, validation["report"]


def test_the_rule_that_answers_the_tasting_defect_goes_before_the_mass_first_rule():
    """Largo, amargo y con la masa fuera de la receta, como el café 11 de la
    catación del 2026-11-04: las dos reglas tienen fuente. La principal responde
    al amargor que registró el barista, y la masa queda como alternativa, por
    una regla explícita y no por el orden alfabético de los nombres.
    """
    profile = {"id": "perfil-m", "status": "accepted", "specifications": [
        {"property": "extraction-time", "unit": "s", "strategy": "range",
         "minimum": 18.0, "maximum": 23.0},
        {"property": "beverage-mass", "unit": "g", "strategy": "target",
         "target": 37.6, "tolerance": 0.4}]}
    extraction = {**EXTRACTION, "id": "prep-m", "sensoryEvaluation": {
        "id": "cata-m", "evaluatedBy": "barista-1", "evaluatedAt": "2026-10-16T09:05:00Z",
        "scores": [{"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 4}],
        "defects": ["ExcessiveBitterness"]}}
    graph = extraction_graph(extraction, profile, [
        {"key": "extraction-time", "property": "extraction-time", "unit": "s", "value": 27.0,
         "kind": "measured", "resultTime": "2026-10-16T09:00:28Z"},
        {"key": "beverage-mass", "property": "beverage-mass", "unit": "g", "value": 45.2,
         "kind": "measured", "resultTime": "2026-10-16T09:00:28Z"}])
    result = ReasonerRecommendationAdapter("3.5.0").recommend("prep-m", [], lambda: graph)
    assert result["proposedAdjustment"]["property"] == "extraction-time"
    assert result["summary"].startswith("Acortar")
    assert any("masa de bebida" in item for item in result["alternatives"])
