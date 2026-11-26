#!/usr/bin/env python3
"""Verificación reproducible del núcleo y el razonamiento basado en evidencia."""

from decimal import Decimal
from pathlib import Path
import re
import sys
from tempfile import TemporaryDirectory

from rdflib import Graph, Literal, Namespace, OWL, RDF, RDFS, URIRef, XSD
from pyshacl import validate as shacl_validate


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from reasoning.materialize import label as term_label, materialize, materialize_profile_deviations  # noqa: E402


ONTOLOGY = ROOT / "ontology" / "espresso-dt.ttl"
ACQUISITION = ROOT / "ontology" / "espresso-acquisition.ttl"
SHAPES = ROOT / "ontology" / "espresso-dt-shapes.ttl"
ACQUISITION_SHAPES = ROOT / "ontology" / "espresso-acquisition-shapes.ttl"
DOMAIN_VALIDATION = ROOT / "ontology" / "espresso-domain-validation.ttl"
QUERIES = ROOT / "queries" / "competency-questions.rq"
DOMAIN_QUERIES = ROOT / "queries" / "domain-cases.rq"
DIAGNOSTIC_QUERY = ROOT / "queries" / "diagnostic-response.rq"
MIGRATION = ROOT / "migrations" / "2.7.1-to-3.0.0.ru"
EDT = Namespace("https://w3id.org/espresso-dt#")
DCTERMS = Namespace("http://purl.org/dc/terms/")
PROV = Namespace("http://www.w3.org/ns/prov#")


def fail(message):
    print(f"FAIL: {message}")
    return 1


def values(graph, node, predicate):
    return list(graph.objects(node, predicate))


def exactly(graph, node, predicate, count):
    return len(values(graph, node, predicate)) == count


def one(graph, node, predicate):
    found = values(graph, node, predicate)
    return found[0] if found else None


def decimal_value(graph, node, predicate):
    literal = one(graph, node, predicate)
    return Decimal(str(literal)) if literal is not None else None


def typed_subjects(graph, *classes):
    result = set()
    for class_node in classes:
        result |= set(graph.subjects(RDF.type, class_node))
    return result


def clone_graph(graph):
    cloned = Graph()
    for prefix, namespace in graph.namespaces():
        cloned.bind(prefix, namespace)
    for triple in graph:
        cloned.add(triple)
    return cloned


def migration_271_to_300_is_correct():
    fixture = Graph().parse(
        data="""
            @prefix edt: <https://w3id.org/espresso-dt#> .
            @prefix dcterms: <http://purl.org/dc/terms/> .

            edt:LegacyDeviation a edt:TechnicalDeviation ;
                edt:generatedByRule edt:RuleProfileDeviation .
            edt:LegacyAction a edt:RecommendedAction ;
                edt:recommendedByRule edt:RuleBaristaAcidityAdjustment,
                    edt:RuleBaristaCoffeeBedReview,
                    edt:RuleBaristaIterativeCalibration,
                    edt:RuleBaristaContextReview,
                    edt:RuleBaristaHumanSupervision .
            edt:LegacyDiagnosis a edt:Diagnosis ;
                edt:generatedByRule edt:RuleHumidityCoffeeBedEffect,
                    edt:RuleUnderExtractionAcidity .
            edt:LegacyProfile a edt:GlobalKnowledgeProfile ;
                edt:assertsKnowledge edt:RuleBaristaIterativeCalibration .

            edt:RuleProfileDeviation a edt:Rule ; dcterms:source "formalización" .
            edt:RuleBaristaAcidityAdjustment a edt:Rule ; dcterms:source "barista" .
            edt:RuleBaristaCoffeeBedReview a edt:Rule ; dcterms:source "barista" .
            edt:RuleBaristaIterativeCalibration a edt:Rule ; dcterms:source "barista" .
            edt:RuleBaristaContextReview a edt:Rule ; dcterms:source "barista" .
            edt:RuleHumidityCoffeeBedEffect a edt:Rule ; dcterms:source "literatura" .
            edt:RuleUnderExtractionAcidity a edt:Rule ; dcterms:source "literatura" .
            edt:RuleBaristaHumanSupervision a edt:Rule ; dcterms:source "barista" .
        """,
        format="turtle",
    )
    update = MIGRATION.read_text(encoding="utf-8")
    fixture.update(update)
    expected = {
        (EDT.LegacyDeviation, EDT.basedOnCriterion, EDT.ProfileConformanceCriterion),
        (EDT.LegacyAction, EDT.supportedByHeuristic, EDT.AcidityAdjustmentHeuristic),
        (EDT.LegacyAction, EDT.followsProcedure, EDT.CoffeeBedInspectionProcedure),
        (EDT.LegacyAction, EDT.followsProcedure, EDT.IterativeCalibrationProcedure),
        (EDT.LegacyAction, EDT.followsProcedure, EDT.EnvironmentalContextReviewProcedure),
        (EDT.LegacyAction, EDT.governedByPolicy, EDT.HumanRecommendationValidationPolicy),
        (EDT.LegacyDiagnosis, EDT.supportedByEvidencePattern, EDT.HumidityCoffeeBedEvidencePattern),
        (EDT.LegacyDiagnosis, EDT.supportedByHeuristic, EDT.UnderExtractionAcidityHeuristic),
        (EDT.LegacyProfile, EDT.assertsKnowledge, EDT.IterativeCalibrationProcedure),
    }
    if not expected <= set(fixture):
        return False
    if any(next(fixture.subjects(predicate), None) is not None for predicate in (EDT.generatedByRule, EDT.recommendedByRule)):
        return False
    if list(fixture.subjects(RDF.type, EDT.Rule)):
        return False
    first_result = set(fixture)
    fixture.update(update)
    return set(fixture) == first_result


def published_diagrams_use_current_vocabulary():
    diagram_root = ROOT / "diagrams"
    current_files = [
        *(path for directory in diagram_root.glob("caso-*") for path in directory.glob("*.*") if path.suffix in {".html", ".svg"}),
        diagram_root / "ontologia" / "ontologia-diagrama.html",
        diagram_root / "ontologia" / "ontologia-diagrama.svg",
        diagram_root / "ontologia" / "ontologia-diagrama-integral.svg",
        diagram_root / "ontologia" / "ontologia-diagrama-integral.drawio",
        diagram_root / "ontologia" / "recorrido-semantico-principal.svg",
        diagram_root / "ontologia" / "recorrido-semantico-principal.drawio",
    ]
    obsolete = (
        "recommendedByRule", "generatedByRule", "RuleProfileDeviation",
        "RuleBarista", "RuleHumidityCoffeeBedEffect", "RuleUnderExtractionAcidity",
    )
    for path in current_files:
        text = path.read_text(encoding="utf-8")
        # El mapa integral cataloga también los términos obsoletos y los marca
        # como tales; los diagramas operativos no deben utilizarlos.
        is_integral_catalog = path.name.startswith("ontologia-diagrama-integral")
        if "3.5.0" not in text or (
            not is_integral_catalog and any(term in text for term in obsolete)
        ):
            return False
    return True


def validate_integrity(graph):
    errors = []
    coffee_types = (EDT.CoffeeMaterial, EDT.CoffeeDose)
    for coffee in typed_subjects(graph, *coffee_types):
        for predicate in (
            EDT.lotIdentifier, EDT.bagIdentifier, EDT.commercialReference,
            EDT.roastDate, EDT.hasTerroir,
        ):
            if len(values(graph, coffee, predicate)) > 1:
                errors.append(f"{coffee} admite como máximo un valor de {predicate}")

    for terroir in graph.subjects(RDF.type, EDT.Terroir):
        for predicate in (
            EDT.regionName, EDT.hasAltitude, EDT.soilCharacteristics,
            EDT.mineralComposition,
        ):
            if len(values(graph, terroir, predicate)) > 1:
                errors.append(f"{terroir} admite como máximo un valor de {predicate}")
        if values(graph, terroir, EDT.mineralComposition) and not values(graph, terroir, DCTERMS.source):
            errors.append(f"{terroir} requiere una fuente para la composición mineral")

    for altitude in graph.subjects(RDF.type, EDT.AltitudeSpecification):
        if not exactly(graph, altitude, EDT.quantityValue, 1):
            errors.append(f"{altitude} requiere exactamente un valor de cantidad")
        if not exactly(graph, altitude, EDT.hasUnit, 1):
            errors.append(f"{altitude} requiere exactamente una unidad")

    for extraction in graph.subjects(RDF.type, EDT.EspressoExtraction):
        for predicate, count in (
            (EDT.usesProfile, 1), (EDT.performedBy, 1), (EDT.producesBeverage, 1),
        ):
            if not exactly(graph, extraction, predicate, count):
                errors.append(f"{extraction} requiere exactamente un valor de {predicate}")
        # Desde 3.2.0, una extracción cortada por pérdida de energía puede carecer
        # de fin, y de inicio si no había hora: nunca de más de uno de cada uno.
        power_loss = EDT.UnexpectedPowerLoss in values(graph, extraction, EDT.hasTerminationCause)
        for predicate in (EDT.startTime, EDT.endTime):
            found = len(values(graph, extraction, predicate))
            if found > 1 or (found == 0 and not power_loss):
                errors.append(f"{extraction} requiere exactamente un valor de {predicate}")
        if not values(graph, extraction, EDT.usesMaterial):
            errors.append(f"{extraction} requiere material de café")
        if len(values(graph, extraction, EDT.hasTerminationCause)) > 1:
            errors.append(f"{extraction} admite como máximo una causa de finalización")

    observation_types = (
        EDT.Observation, EDT.QualitativeObservation, EDT.QuantitativeObservation,
        EDT.MeasuredObservation, EDT.CalculatedObservation, EDT.QualityIndicatorObservation,
        EDT.TDSObservation, EDT.BrixObservation, EDT.ExtractionYieldObservation,
    )
    observations = typed_subjects(graph, *observation_types)
    quantitative_types = observation_types[2:]
    for observation in observations:
        for predicate in (EDT.observedProperty, EDT.featureOfInterest, EDT.resultTime):
            if not exactly(graph, observation, predicate, 1):
                errors.append(f"{observation} requiere exactamente un valor de {predicate}")
        is_quantitative = any((observation, RDF.type, kind) in graph for kind in quantitative_types)
        if is_quantitative:
            if not exactly(graph, observation, EDT.numericValue, 1):
                errors.append(f"{observation} requiere exactamente un valor numérico")
            if not exactly(graph, observation, EDT.hasUnit, 1):
                errors.append(f"{observation} requiere exactamente una unidad")
        elif not exactly(graph, observation, EDT.textValue, 1):
            errors.append(f"{observation} cualitativa requiere exactamente un texto")
        if values(graph, observation, EDT.recordedBy):
            if not exactly(graph, observation, EDT.recordedBy, 1):
                errors.append(f"{observation} admite exactamente un agente de registro manual")
            if not exactly(graph, observation, EDT.recordingTime, 1):
                errors.append(f"{observation} registrada manualmente requiere tiempo de registro")

    for observation in graph.subjects(RDF.type, EDT.CalculatedObservation):
        if not exactly(graph, observation, EDT.calculationMethodReference, 1):
            errors.append(f"{observation} calculada requiere un método")
        if not values(graph, observation, EDT.computedFrom) and not values(graph, observation, EDT.derivedFromSeries):
            errors.append(f"{observation} calculada requiere observaciones o serie de origen")

    for series in graph.subjects(RDF.type, EDT.ObservationSeries):
        for predicate in (EDT.seriesForProperty, EDT.hasUnit, EDT.dataReference):
            if not exactly(graph, series, predicate, 1):
                errors.append(f"{series} requiere exactamente un valor de {predicate}")

    for condition in graph.subjects(RDF.type, EDT.EnvironmentalCondition):
        for predicate in (EDT.validFrom, EDT.validUntil):
            if not exactly(graph, condition, predicate, 1):
                errors.append(f"{condition} requiere exactamente un valor de {predicate}")

    for profile in graph.subjects(RDF.type, EDT.ExtractionProfile):
        if not values(graph, profile, EDT.definedBy):
            errors.append(f"{profile} requiere autor")
        if not exactly(graph, profile, EDT.hasProfileStatus, 1):
            errors.append(f"{profile} requiere un estado")
        if not values(graph, profile, EDT.hasParameterSpecification):
            errors.append(f"{profile} requiere especificaciones")

    for specification in graph.subjects(RDF.type, EDT.ProfileParameterSpecification):
        targets = values(graph, specification, EDT.targetValue)
        tolerances = values(graph, specification, EDT.toleranceValue)
        minimums = values(graph, specification, EDT.minimumValue)
        maximums = values(graph, specification, EDT.maximumValue)
        exact_strategy = len(targets) == 1 and not minimums and not maximums
        interval_strategy = (
            len(minimums) == 1 and len(maximums) == 1
            and not targets and not tolerances
        )
        if not (exact_strategy ^ interval_strategy):
            errors.append(f"{specification} debe usar valor objetivo o intervalo, sin mezclarlos")
        if len(tolerances) > 1 or (tolerances and not targets):
            errors.append(f"{specification} tiene una tolerancia sin valor objetivo válido")
        if tolerances and Decimal(str(tolerances[0])) < 0:
            errors.append(f"{specification} tiene una tolerancia negativa")
        if interval_strategy and Decimal(str(minimums[0])) > Decimal(str(maximums[0])):
            errors.append(f"{specification} tiene un mínimo mayor que el máximo")

    evaluation_nodes = set(graph.subjects(RDF.type, EDT.SensoryEvaluation))
    allowed_sensory_acceptance = {
        EDT.SensoryAccepted, EDT.SensoryRejected, EDT.SensoryAcceptanceInconclusive,
    }
    for evaluation in evaluation_nodes:
        for predicate in (EDT.evaluatedBy, EDT.evaluatesBeverage, EDT.evaluationTime):
            if not exactly(graph, evaluation, predicate, 1):
                errors.append(f"{evaluation} requiere exactamente un valor de {predicate}")
        if not values(graph, evaluation, EDT.hasSensoryScore):
            errors.append(f"{evaluation} requiere al menos una puntuación")
        combinations = set()
        for score in values(graph, evaluation, EDT.hasSensoryScore):
            combination = (one(graph, score, EDT.forSensoryAttribute), one(graph, score, EDT.measuresDimension))
            if None in combination:
                errors.append(f"{score} requiere atributo y dimensión")
            elif combination in combinations:
                errors.append(f"{evaluation} repite atributo y dimensión")
            combinations.add(combination)
        acceptance_statuses = values(graph, evaluation, EDT.hasSensoryAcceptanceStatus)
        rationales = values(graph, evaluation, EDT.acceptanceRationale)
        if len(acceptance_statuses) > 1 or any(status not in allowed_sensory_acceptance for status in acceptance_statuses):
            errors.append(f"{evaluation} tiene un estado de aceptación sensorial no admitido")
        if len(rationales) > 1:
            errors.append(f"{evaluation} admite como máximo una justificación sensorial")
        if EDT.SensoryRejected in acceptance_statuses and not rationales:
            errors.append(f"{evaluation} rechazada sensorialmente requiere justificación")
        if rationales and not acceptance_statuses:
            errors.append(f"{evaluation} con justificación requiere un estado de aceptación sensorial")

    score_types = (EDT.PerceivedIntensityScore, EDT.HedonicLikingScore)
    for score in typed_subjects(graph, *score_types):
        attribute = one(graph, score, EDT.forSensoryAttribute)
        dimension = one(graph, score, EDT.measuresDimension)
        number = decimal_value(graph, score, EDT.scoreValue)
        if attribute is None or dimension is None or number is None:
            errors.append(f"{score} requiere atributo, dimensión y valor")
            continue
        if (score, RDF.type, EDT.PerceivedIntensityScore) in graph:
            if dimension != EDT.PerceivedIntensity or not Decimal(0) <= number <= Decimal(10):
                errors.append(f"{score} no cumple intensidad 0–10")
        if (score, RDF.type, EDT.HedonicLikingScore) in graph:
            if dimension != EDT.HedonicLiking or not Decimal(1) <= number <= Decimal(9):
                errors.append(f"{score} no cumple agrado 1–9")
        if attribute in (EDT.Balance, EDT.OverallLiking) and dimension != EDT.HedonicLiking:
            errors.append(f"{score}: Balance y OverallLiking solo admiten agrado")

    for deviation in graph.subjects(RDF.type, EDT.TechnicalDeviation):
        sources = values(graph, deviation, EDT.derivedFrom)
        if not any(source in observations for source in sources):
            errors.append(f"{deviation} requiere una observación de origen")
        if not any((source, RDF.type, EDT.ProfileParameterSpecification) in graph for source in sources):
            errors.append(f"{deviation} requiere una especificación de perfil")
        if not exactly(graph, deviation, EDT.basedOnCriterion, 1):
            errors.append(f"{deviation} requiere un criterio de evaluación")
        elif (one(graph, deviation, EDT.basedOnCriterion), RDF.type, EDT.EvaluationCriterion) not in graph:
            errors.append(f"{deviation} utiliza un criterio con tipo incorrecto")
        if not exactly(graph, deviation, EDT.statusCode, 1):
            errors.append(f"{deviation} requiere un código de estado")

    allowed_technical_conformance = {
        EDT.TechnicallyConformant, EDT.TechnicallyNonConformant,
        EDT.TechnicalConformanceInconclusive,
    }
    for assessment in graph.subjects(RDF.type, EDT.TechnicalConformanceAssessment):
        for predicate in (
            EDT.assessesExtraction, EDT.againstProfile, EDT.hasTechnicalConformance,
            EDT.assessmentTime, EDT.statusCode,
        ):
            if not exactly(graph, assessment, predicate, 1):
                errors.append(f"{assessment} requiere exactamente un valor de {predicate}")
        if not values(graph, assessment, EDT.derivedFrom):
            errors.append(f"{assessment} requiere evidencia derivada")
        if not values(graph, assessment, EDT.generatedBy):
            errors.append(f"{assessment} requiere agente generador")
        if not values(graph, assessment, EDT.description):
            errors.append(f"{assessment} requiere descripción")
        extraction = one(graph, assessment, EDT.assessesExtraction)
        profile = one(graph, assessment, EDT.againstProfile)
        category = one(graph, assessment, EDT.hasTechnicalConformance)
        if category not in allowed_technical_conformance:
            errors.append(f"{assessment} tiene una categoría técnica no admitida")
        if extraction is not None and profile is not None and (extraction, EDT.usesProfile, profile) not in graph:
            errors.append(f"{assessment} no utiliza el perfil de la extracción evaluada")
        evidence = set(values(graph, assessment, EDT.derivedFrom))
        specifications = set(graph.objects(profile, EDT.hasParameterSpecification)) if profile is not None else set()
        extraction_observations = list(graph.objects(extraction, EDT.hasObservation)) if extraction is not None else []
        missing = []
        # La especificación de la que se deriva otra se juzga a través de la
        # derivada (EKO 3.4.0) y no exige una observación propia.
        judged_through = {one(graph, spec, EDT.derivedFromSpecification) for spec in specifications}
        for specification_node in specifications - judged_through:
            prop = one(graph, specification_node, EDT.forProperty)
            unit = one(graph, specification_node, EDT.hasUnit)
            compatible = any(
                observation_node in evidence
                and one(graph, observation_node, EDT.observedProperty) == prop
                and one(graph, observation_node, EDT.hasUnit) == unit
                and one(graph, observation_node, EDT.numericValue) is not None
                for observation_node in extraction_observations
            )
            if specification_node not in evidence or not compatible:
                missing.append(specification_node)
        deviations = [
            item for item in evidence
            if (item, RDF.type, EDT.TechnicalDeviation) in graph
        ]
        for item in evidence:
            if item in observations and item not in extraction_observations:
                errors.append(f"{assessment} utiliza una observación ajena a la extracción evaluada")
            if ((item, RDF.type, EDT.ProfileParameterSpecification) in graph
                    and item not in specifications):
                errors.append(f"{assessment} utiliza una especificación ajena al perfil evaluado")
            if (item, RDF.type, EDT.TechnicalDeviation) in graph:
                sources = set(values(graph, item, EDT.derivedFrom))
                matching_observation = any(source in extraction_observations for source in sources)
                matching_specification = any(source in specifications for source in sources)
                if not matching_observation or not matching_specification:
                    errors.append(f"{assessment} utiliza una desviación ajena a la extracción o al perfil evaluados")
        if category == EDT.TechnicallyConformant and (missing or deviations):
            errors.append(f"{assessment} declara conformidad sin cobertura completa o con desviaciones")
        if category == EDT.TechnicallyNonConformant and not deviations:
            errors.append(f"{assessment} declara no conformidad sin desviación")
        if category == EDT.TechnicalConformanceInconclusive and not missing:
            errors.append(f"{assessment} declara resultado inconcluso sin evidencia faltante")
        assessment_time = one(graph, assessment, EDT.assessmentTime)
        if assessment_time is not None:
            for item in evidence:
                result_time = one(graph, item, EDT.resultTime)
                if result_time is not None and assessment_time.toPython() < result_time.toPython():
                    errors.append(f"{assessment} ocurre antes de una evidencia utilizada")

    allowed_diagnosis_targets = typed_subjects(
        graph, EDT.SensoryDefect, EDT.TechnicalDeviation, EDT.ObservedDifference,
    )
    for diagnosis in graph.subjects(RDF.type, EDT.Diagnosis):
        if not values(graph, diagnosis, EDT.derivedFrom):
            errors.append(f"{diagnosis} requiere evidencia derivada")
        targets = values(graph, diagnosis, EDT.isDiagnosisOf)
        if not targets:
            errors.append(f"{diagnosis} requiere un objeto diagnosticado")
        elif any(target not in allowed_diagnosis_targets for target in targets):
            errors.append(f"{diagnosis} apunta a un tipo de objeto no admitido")
        if not values(graph, diagnosis, EDT.generatedBy):
            errors.append(f"{diagnosis} requiere identificar el agente generador")
        if not values(graph, diagnosis, EDT.description):
            errors.append(f"{diagnosis} requiere una conclusión legible")
        if not exactly(graph, diagnosis, EDT.statusCode, 1):
            errors.append(f"{diagnosis} requiere un código de estado")

    allowed_explanation_targets = typed_subjects(graph, EDT.Diagnosis, EDT.ObservedDifference)
    for explanation in graph.subjects(RDF.type, EDT.ExplanationAssertion):
        if not values(graph, explanation, EDT.hasEvidence):
            errors.append(f"{explanation} requiere evidencia")
        if not exactly(graph, explanation, EDT.explains, 1):
            errors.append(f"{explanation} debe explicar exactamente un resultado")
        elif one(graph, explanation, EDT.explains) not in allowed_explanation_targets:
            errors.append(f"{explanation} explica un tipo de resultado no admitido")
        if not values(graph, explanation, EDT.generatedBy):
            errors.append(f"{explanation} requiere identificar el agente generador")
        if not values(graph, explanation, EDT.description):
            errors.append(f"{explanation} requiere un texto explicativo")
        if not exactly(graph, explanation, EDT.statusCode, 1):
            errors.append(f"{explanation} requiere un código de estado")

    for difference in graph.subjects(RDF.type, EDT.ObservedDifference):
        if len(values(graph, difference, EDT.comparesExtraction)) < 2:
            errors.append(f"{difference} requiere al menos dos extracciones")
        if len(values(graph, difference, EDT.hasEvidence)) < 2:
            errors.append(f"{difference} requiere al menos dos evidencias")

    knowledge_basis_types = {
        EDT.basedOnCriterion: (EDT.EvaluationCriterion,),
        EDT.supportedByHeuristic: (EDT.ExpertHeuristic, EDT.DiagnosticHeuristic),
        EDT.followsProcedure: (EDT.Procedure, EDT.DiagnosticProcedure, EDT.CalibrationProcedure),
        EDT.supportedByEvidencePattern: (EDT.ContextualEvidencePattern,),
        EDT.governedByPolicy: (EDT.ValidationPolicy,),
    }
    for action in graph.subjects(RDF.type, EDT.RecommendedAction):
        for predicate in (EDT.respondsTo, EDT.adjustsProperty):
            if not exactly(graph, action, predicate, 1):
                errors.append(f"{action} requiere exactamente un valor de {predicate}")
        if not values(graph, action, EDT.derivedFrom):
            errors.append(f"{action} requiere evidencia de origen")
        bases = [
            (predicate, basis)
            for predicate in knowledge_basis_types
            for basis in values(graph, action, predicate)
        ]
        if not bases:
            errors.append(f"{action} requiere una base de conocimiento explícita")
        for predicate, basis in bases:
            expected_types = knowledge_basis_types[predicate]
            if not any((basis, RDF.type, expected_type) in graph for expected_type in expected_types):
                errors.append(f"{action} usa {predicate} con un tipo de conocimiento incorrecto")
        target = one(graph, action, EDT.respondsTo)
        allowed_action_targets = allowed_diagnosis_targets | set(graph.subjects(RDF.type, EDT.Diagnosis))
        if target is not None and target not in allowed_action_targets:
            errors.append(f"{action} responde a un tipo de hallazgo no admitido")
        statuses = values(graph, action, EDT.hasValidationStatus)
        validators = values(graph, action, EDT.validatedBy)
        validation_times = values(graph, action, EDT.validationTime)
        if statuses and (len(validators) != 1 or len(validation_times) != 1):
            errors.append(f"{action} con decisión requiere un barista y un tiempo de validación")
        if validators and not exactly(graph, action, EDT.hasValidationStatus, 1):
            errors.append(f"{action} con barista validador requiere un estado de validación")
        if (action, EDT.hasValidationStatus, EDT.Modified) in graph and not values(graph, action, EDT.validationRationale):
            errors.append(f"{action} modificada requiere una justificación")
        for predicate in (EDT.hasValidationStatus, EDT.validatedBy, EDT.validationTime, EDT.validationRationale):
            if len(values(graph, action, predicate)) > 1:
                errors.append(f"{action} admite como máximo un valor de {predicate}")
        if not values(graph, action, EDT.generatedBy):
            errors.append(f"{action} requiere identificar el agente generador")
        if not values(graph, action, EDT.description):
            errors.append(f"{action} requiere una instrucción legible")

    for calibration in graph.subjects(RDF.type, EDT.EspressoCalibration):
        if not exactly(graph, calibration, EDT.performedBy, 1):
            errors.append(f"{calibration} requiere exactamente un barista")
        if not values(graph, calibration, EDT.hasTrialExtraction):
            errors.append(f"{calibration} requiere al menos una prueba")
        for predicate in (EDT.startTime, EDT.endTime):
            if not exactly(graph, calibration, predicate, 1):
                errors.append(f"{calibration} requiere exactamente un valor de {predicate}")
        if (calibration, EDT.hasValidationStatus, EDT.Accepted) in graph:
            if not values(graph, calibration, EDT.validatesProfile) and not values(graph, calibration, EDT.producesProfile):
                errors.append(f"{calibration} aceptada debe validar o producir un perfil")

    adjustments = typed_subjects(
        graph, EDT.ParameterAdjustment, EDT.ManualParameterAdjustment, EDT.AutomaticParameterAdjustment,
    )
    for adjustment in adjustments:
        for predicate in (EDT.performedBy, EDT.changesProperty, EDT.startTime, EDT.endTime):
            if not exactly(graph, adjustment, predicate, 1):
                errors.append(f"{adjustment} requiere exactamente un valor de {predicate}")
        if len(values(graph, adjustment, EDT.partOfCalibration)) > 1:
            errors.append(f"{adjustment} no puede pertenecer a más de una calibración")
        for target in values(graph, adjustment, EDT.targetsOutcome):
            if ((target, RDF.type, EDT.ObservableProperty) not in graph
                    and (target, RDF.type, EDT.SensoryDefect) not in graph):
                errors.append(f"{adjustment} se dirige a un resultado de tipo no admitido")
        recommendations = values(graph, adjustment, EDT.basedOnRecommendation)
        if len(recommendations) > 1:
            errors.append(f"{adjustment} no puede basarse en más de una recomendación")
        for recommendation in recommendations:
            status = one(graph, recommendation, EDT.hasValidationStatus)
            is_manual = (adjustment, RDF.type, EDT.ManualParameterAdjustment) in graph
            is_automatic = (adjustment, RDF.type, EDT.AutomaticParameterAdjustment) in graph
            if is_manual and status not in (EDT.Accepted, EDT.Modified):
                errors.append(f"{adjustment} se basa en una recomendación no aceptada ni modificada")
            if is_automatic and status == EDT.Rejected:
                errors.append(f"{adjustment} se basa en una recomendación rechazada")
            if one(graph, recommendation, EDT.adjustsProperty) != one(graph, adjustment, EDT.changesProperty):
                errors.append(f"{adjustment} y su recomendación no se refieren a la misma propiedad")

    for extraction in graph.subjects(EDT.evaluatesOutcomeOf, None):
        evaluated_adjustments = values(graph, extraction, EDT.evaluatesOutcomeOf)
        if len(evaluated_adjustments) != 1:
            errors.append(f"{extraction} debe evaluar exactamente un ajuste")
            continue
        adjustment = evaluated_adjustments[0]
        if adjustment not in adjustments:
            errors.append(f"{extraction} evalúa una entidad que no es un ajuste")
        if not values(graph, adjustment, EDT.targetsOutcome):
            errors.append(f"{extraction} evalúa un ajuste sin objetivo declarado")
        if (adjustment, EDT.precedes, extraction) not in graph:
            errors.append(f"{extraction} no está precedida por el ajuste que evalúa")
        adjustment_end = one(graph, adjustment, EDT.endTime)
        extraction_start = one(graph, extraction, EDT.startTime)
        if adjustment_end is not None and extraction_start is not None:
            if extraction_start.toPython() < adjustment_end.toPython():
                errors.append(f"{extraction} comienza antes de terminar el ajuste evaluado")

    allowed_outcomes = {
        EDT.ExpectedOutcomeObserved, EDT.InsufficientOutcomeObserved,
        EDT.AdverseOutcomeObserved, EDT.InconclusiveOutcome,
    }
    evaluations_by_adjustment = {}
    for extraction in graph.subjects(EDT.evaluatesOutcomeOf, None):
        for adjustment in graph.objects(extraction, EDT.evaluatesOutcomeOf):
            evaluations_by_adjustment.setdefault(adjustment, set()).add(extraction)
    for assessment in graph.subjects(RDF.type, EDT.AdjustmentOutcomeAssessment):
        if not exactly(graph, assessment, EDT.assessesAdjustment, 1):
            errors.append(f"{assessment} debe evaluar exactamente un ajuste")
            continue
        categories = values(graph, assessment, EDT.hasOutcomeCategory)
        if len(categories) != 1 or categories[0] not in allowed_outcomes:
            errors.append(f"{assessment} requiere exactamente una categoría de resultado admitida")
        differences = [
            item for item in values(graph, assessment, EDT.derivedFrom)
            if (item, RDF.type, EDT.ObservedDifference) in graph
        ]
        if not differences:
            errors.append(f"{assessment} requiere una diferencia observada como evidencia")
            continue
        adjustment = one(graph, assessment, EDT.assessesAdjustment)
        evaluations = evaluations_by_adjustment.get(adjustment, set())
        if not any(
            (difference, EDT.hasEvidence, adjustment) in graph
            and any((difference, EDT.comparesExtraction, evaluation) in graph for evaluation in evaluations)
            for difference in differences
        ):
            errors.append(f"{assessment} no conecta ajuste, diferencia y extracción evaluadora")
    return errors


def query_blocks(text, marker_pattern):
    markers = list(re.finditer(marker_pattern, text))
    for index, marker in enumerate(markers):
        start = marker.end()
        end = markers[index + 1].start() if index + 1 < len(markers) else len(text)
        query_text = text[start:end]
        query_start = query_text.find("PREFIX")
        yield marker.group(1), query_text[query_start:].strip()


def exact_target_semantics_are_enforced():
    """Comprueba que un objetivo sin tolerancia no se juzga y que la tolerancia es cerrada.

    Desde 3.3.0 un objetivo sin margen no define límites: ni 9.0 ni 9.1 frente a
    un objetivo de 9.0 producen desviación, porque un margen de cero sería una
    tolerancia que nadie decidió.
    """
    graph = Graph()
    scenarios = (
        ("ExactEqual", Decimal("9.0"), None, Decimal("9.0"), False),
        ("ExactDifferent", Decimal("9.0"), None, Decimal("9.1"), False),
        ("ToleranceBoundary", Decimal("10.0"), Decimal("0.5"), Decimal("10.5"), False),
        ("ToleranceOutside", Decimal("10.0"), Decimal("0.5"), Decimal("10.51"), True),
    )
    for name, target, tolerance, observed, expected_deviation in scenarios:
        extraction = EDT[f"Test{name}Extraction"]
        profile = EDT[f"Test{name}Profile"]
        specification = EDT[f"Test{name}Specification"]
        observation = EDT[f"Test{name}Observation"]
        graph.add((extraction, RDF.type, EDT.EspressoExtraction))
        graph.add((extraction, EDT.usesProfile, profile))
        graph.add((extraction, EDT.hasObservation, observation))
        graph.add((profile, EDT.hasParameterSpecification, specification))
        graph.add((specification, EDT.forProperty, EDT.ExtractionPressure))
        graph.add((specification, EDT.hasUnit, EDT.Bar))
        graph.add((specification, EDT.targetValue, Literal(target, datatype=XSD.decimal)))
        if tolerance is not None:
            graph.add((specification, EDT.toleranceValue, Literal(tolerance, datatype=XSD.decimal)))
        graph.add((observation, EDT.observedProperty, EDT.ExtractionPressure))
        graph.add((observation, EDT.hasUnit, EDT.Bar))
        graph.add((observation, EDT.numericValue, Literal(observed, datatype=XSD.decimal)))
    deviations = materialize_profile_deviations(graph)
    for name, _, _, _, expected_deviation in scenarios:
        extraction = EDT[f"Test{name}Extraction"]
        observed_deviation = any(candidate == extraction for candidate, _, _ in deviations)
        if observed_deviation != expected_deviation:
            return False
    return True


def main():
    core_graph = Graph().parse(ONTOLOGY, format="turtle")
    acquisition_graph = Graph().parse(ACQUISITION, format="turtle")
    domain_graph = Graph().parse(DOMAIN_VALIDATION, format="turtle")
    shape_graph = Graph().parse(SHAPES, format="turtle")
    shape_graph.parse(ACQUISITION_SHAPES, format="turtle")
    raw_graph = core_graph + acquisition_graph + domain_graph
    print(
        f"OK: sintaxis RDF/Turtle ({len(core_graph)} triples núcleo; "
        f"{len(acquisition_graph)} adquisición; {len(domain_graph)} casos; "
        f"{len(shape_graph)} SHACL)"
    )
    ontology_node = next(core_graph.subjects(RDF.type, OWL.Ontology), None)
    if (
        ontology_node is None
        or one(core_graph, ontology_node, OWL.versionInfo) != Literal("3.5.0")
        or one(core_graph, ontology_node, OWL.versionIRI) != URIRef("https://w3id.org/espresso-dt/3.5.0")
        or one(core_graph, ontology_node, OWL.priorVersion) != URIRef("https://w3id.org/espresso-dt/3.4.0")
    ):
        return fail("la cabecera de EKO no declara la versión 3.5.0 y su antecedente 3.4.0")

    if not migration_271_to_300_is_correct():
        return fail("la migración 2.7.1→3.0.0 no conserva el significado o no es idempotente")
    print("OK: migración 2.7.1→3.0.0 explícita e idempotente")

    if not published_diagrams_use_current_vocabulary():
        return fail("un diagrama vigente conserva una versión o vocabulario incorrectos")
    print("OK: diagramas de EKO 3.5.0 mantienen su vocabulario")

    required_terms = (
        EDT.EnvironmentalCondition, EDT.occurredUnder, EDT.validFrom, EDT.validUntil,
        EDT.CoffeeVariety, EDT.PostHarvestProcessType, EDT.RoastLevel,
        EDT.Terroir, EDT.AltitudeSpecification, EDT.hasTerroir, EDT.hasAltitude,
        EDT.lotIdentifier, EDT.bagIdentifier,
        EDT.commercialReference, EDT.roastDate, EDT.regionName,
        EDT.quantityValue, EDT.soilCharacteristics, EDT.mineralComposition,
        EDT.ObservationSeries, EDT.OverallLiking, EDT.Astringency,
        EDT.SensoryProfile, EDT.DeclaredSensoryProfile, EDT.TargetSensoryProfile,
        EDT.hasDeclaredSensoryProfile, EDT.hasTargetSensoryProfile,
        EDT.describesCoffee, EDT.declaredBy, EDT.sensoryTargetDefinedBy,
        EDT.comparesWithSensoryProfile, EDT.sensoryDescriptionText,
        EDT.hasValidationStatus, EDT.basedOnRecommendation,
        EDT.TechnicalDeviation, EDT.isDiagnosisOf, EDT.explains,
        EDT.AdjustmentOutcomeCategory, EDT.AdjustmentOutcomeAssessment,
        EDT.evaluatesOutcomeOf, EDT.targetsOutcome, EDT.assessesAdjustment, EDT.hasOutcomeCategory,
        EDT.ExpectedOutcomeObserved, EDT.InsufficientOutcomeObserved,
        EDT.AdverseOutcomeObserved, EDT.InconclusiveOutcome,
        EDT.validationTime, EDT.validationRationale,
        EDT.TechnicalConformanceAssessment, EDT.TechnicalConformanceCategory,
        EDT.assessesExtraction, EDT.againstProfile, EDT.hasTechnicalConformance,
        EDT.TechnicallyConformant, EDT.TechnicallyNonConformant,
        EDT.TechnicalConformanceInconclusive, EDT.assessmentTime,
        EDT.SensoryAcceptanceStatus, EDT.hasSensoryAcceptanceStatus,
        EDT.SensoryAccepted, EDT.SensoryRejected,
        EDT.SensoryAcceptanceInconclusive, EDT.acceptanceRationale,
        EDT.UnderExtractionDiagnosis,
        EDT.GuidanceKnowledge, EDT.EvaluationCriterion,
        EDT.ExpertHeuristic, EDT.DiagnosticHeuristic,
        EDT.Procedure, EDT.DiagnosticProcedure, EDT.CalibrationProcedure,
        EDT.ContextualEvidencePattern, EDT.ValidationPolicy,
        EDT.hasKnowledgeBasis, EDT.basedOnCriterion,
        EDT.supportedByHeuristic, EDT.followsProcedure,
        EDT.supportedByEvidencePattern, EDT.governedByPolicy,
        EDT.ProfileConformanceCriterion, EDT.AcidityAdjustmentHeuristic,
        EDT.IterativeCalibrationProcedure, EDT.UnderExtractionAcidityHeuristic,
        EDT.HumanRecommendationValidationPolicy, EDT.hasWithheldRecommendation,
        EDT.TerminationCause, EDT.hasTerminationCause,
        EDT.MaximumExtractionTimeReached, EDT.ManualNormalStop,
        EDT.ManualEmergencyStop, EDT.SafetyProtectionTrip,
        EDT.recordedBy, EDT.recordingTime,
    )
    missing = [term for term in required_terms if (term, None, None) not in core_graph]
    if missing:
        return fail(f"faltan términos consolidados: {missing}")

    deprecated_terms = (
        EDT.Rule, EDT.generatedByRule, EDT.recommendedByRule,
        EDT.RuleProfileDeviation, EDT.RuleBaristaAcidityAdjustment,
        EDT.RuleBaristaCoffeeBedReview, EDT.RuleBaristaIterativeCalibration,
        EDT.RuleBaristaContextReview, EDT.RuleHumidityCoffeeBedEffect,
        EDT.RuleUnderExtractionAcidity, EDT.RuleBaristaHumanSupervision,
    )
    if any((term, OWL.deprecated, Literal(True)) not in core_graph for term in deprecated_terms):
        return fail("los términos 2.7.1 no quedaron marcados explícitamente como obsoletos")

    excluded_terms = (
        EDT.DigitalTwin, EDT.GroupHead, EDT.Actuator, EDT.Water, EDT.FlowRate,
        EDT.FlowSensor, EDT.ControllableProperty, EDT.CalibrationResult,
        EDT.EnvironmentalContextAssertion, EDT.TemporalContextAssertion,
        EDT.dependsOnContext, EDT.hasContextObservation, EDT.implementsRecommendation,
        EDT.timeOfDay, EDT.weatherCondition, EDT.expertValidationStatus,
        EDT.DeviationDiagnosis, EDT.hasDiagnosis, EDT.hasExplanation,
        EDT.proposesExplanation, EDT.RuleSensoryDefectDiagnosis,
        EDT.RuleAboveProfileMaximum, EDT.RuleOverextractionAstringency,
        EDT.RuleSameProfileBurntThermal, EDT.RuleAdjustmentDidNotResolve,
        EDT.RuleAcidityCorrectionBitterness, EDT.RuleContextualDifference,
        EDT.RuleTechnicalSensoryConflict, EDT.RuleCoffeeBedInfluence,
        EDT.TerminationCriterion, EDT.hasPrimaryTerminationCriterion,
        EDT.hasFallbackTerminationCriterion, EDT.TargetBeverageMassCriterion,
        EDT.MaximumExtractionTimeCriterion, EDT.TargetBeverageMassReached,
        EDT.BeverageMassAtPumpCutoff, EDT.FinalBeverageMass,
        # Retirados el 3-nov-2026: el sistema no diagnostica ni recomienda sobre
        # molienda, cama de café ni contexto ambiental.
        EDT.CoffeeBedInspectionProcedure, EDT.EnvironmentalContextReviewProcedure,
        EDT.HumidityCoffeeBedEvidencePattern, EDT.TooFineGrindHeuristic,
        EDT.ChannellingHeuristic,
    )
    present = [term for term in excluded_terms if (term, None, None) in raw_graph or (None, None, term) in raw_graph]
    if present:
        return fail(f"la ontología o los casos conservan términos descartados: {present}")
    if list(domain_graph.subjects(RDF.type, EDT.Rule)):
        return fail("los hechos de los casos no deben declarar el tipo obsoleto Rule")
    materializer_text = (ROOT / "reasoning" / "materialize.py").read_text(encoding="utf-8")
    if re.search(r"EDT\.C\d+|Rule(?:Overextraction|SameProfile|AdjustmentDidNot|AcidityCorrection|ContextualDifference|TechnicalSensory|CoffeeBedInfluence)", materializer_text):
        return fail("el razonador contiene identificadores o heurísticas causales de casos")
    if any(next(raw_graph.subjects(predicate), None) is not None for predicate in (EDT.generatedByRule, EDT.recommendedByRule)):
        return fail("la población 3.0 utiliza propiedades obsoletas de Rule")
    if any((term, None, None) in raw_graph or (None, None, term) in raw_graph for term in (
            EDT.C3ExtractionA, EDT.C5ExtractionA, EDT.C6ExtractionA1,
            EDT.C8AfternoonExtraction, EDT.C10ExtractionB)):
        return fail("la población conserva casos 3, 5, 6, 8 o 10")
    if any((term, DCTERMS.isReplacedBy, None) in core_graph for term in (
            EDT.RuleBaristaCoffeeBedReview, EDT.RuleBaristaContextReview, EDT.RuleHumidityCoffeeBedEffect)):
        return fail("un término obsoleto sin reemplazo conserva dcterms:isReplacedBy")
    print("OK: fase 0 consolidada; términos aprobados presentes y términos descartados ausentes")

    if not exact_target_semantics_are_enforced():
        return fail("un targetValue sin tolerancia se juzgó o la tolerancia no respetó sus límites")
    print("OK: targetValue es exacto sin tolerancia y usa un intervalo cerrado cuando la tolerancia existe")

    coffee_lot = EDT.CoffeeLotA
    coffee_fields = (
        EDT.lotIdentifier, EDT.bagIdentifier, EDT.commercialReference,
        EDT.roastDate, EDT.hasVariety, EDT.hasPostHarvestProcess,
        EDT.hasRoastLevel, EDT.hasTerroir,
    )
    if any(not exactly(raw_graph, coffee_lot, predicate, 1) for predicate in coffee_fields):
        return fail("CoffeeLotA no cubre la caracterización ampliada del café")
    terroir = one(raw_graph, coffee_lot, EDT.hasTerroir)
    if (
        terroir is None
        or not exactly(raw_graph, terroir, EDT.regionName, 1)
        or not exactly(raw_graph, terroir, EDT.hasAltitude, 1)
        or not exactly(raw_graph, terroir, EDT.soilCharacteristics, 1)
        or not exactly(raw_graph, terroir, EDT.mineralComposition, 1)
        or not values(raw_graph, terroir, DCTERMS.source)
    ):
        return fail("el terroir sintético no conserva región, altitud, suelo, minerales y fuente")
    altitude = one(raw_graph, terroir, EDT.hasAltitude)
    if (
        altitude is None
        or not exactly(raw_graph, altitude, EDT.quantityValue, 1)
        or not exactly(raw_graph, altitude, EDT.hasUnit, 1)
    ):
        return fail("la altitud sintética no conserva valor y unidad explícita")
    if any(values(raw_graph, EDT.C1Dose, predicate) for predicate in (
        EDT.lotIdentifier, EDT.bagIdentifier, EDT.commercialReference,
        EDT.roastDate, EDT.hasTerroir,
    )):
        return fail("C1Dose debe demostrar que la caracterización ampliada sigue siendo opcional")
    print("OK: identificación, fecha de tueste y terroir ampliados sin volverlos obligatorios")

    for equipment in (EDT.MachineDemo, EDT.GrinderDemo, EDT.MachineValidation, EDT.GrinderValidation):
        if values(raw_graph, equipment, EDT.allowsManualAdjustmentOf) or values(raw_graph, equipment, EDT.allowsAutomaticAdjustmentOf):
            return fail(f"{equipment} no debe declarar capacidades actuales")
    print("OK: el equipo de demostración no afirma capacidades actuales")

    expected_c7_scores = {
        EDT.C7Sensory1: {
            (EDT.Acidity, EDT.PerceivedIntensity): Decimal("8"),
            (EDT.Acidity, EDT.HedonicLiking): Decimal("2"),
            (EDT.Bitterness, EDT.PerceivedIntensity): Decimal("2"),
            (EDT.Bitterness, EDT.HedonicLiking): Decimal("7"),
        },
        EDT.C7Sensory2: {
            (EDT.Acidity, EDT.PerceivedIntensity): Decimal("4"),
            (EDT.Acidity, EDT.HedonicLiking): Decimal("7"),
            (EDT.Bitterness, EDT.PerceivedIntensity): Decimal("8"),
            (EDT.Bitterness, EDT.HedonicLiking): Decimal("2"),
        },
    }
    for evaluation, expected in expected_c7_scores.items():
        actual = {}
        for score in raw_graph.objects(evaluation, EDT.hasSensoryScore):
            key = (
                one(raw_graph, score, EDT.forSensoryAttribute),
                one(raw_graph, score, EDT.measuresDimension),
            )
            actual[key] = decimal_value(raw_graph, score, EDT.scoreValue)
        if actual != expected:
            return fail(f"{evaluation} no conserva la comparación sintética completa de acidez y amargor")
    print("OK: el caso 7 puntúa acidez y amargor antes y después sin crear umbrales sensoriales")

    generated_classes = (
        EDT.TechnicalDeviation, EDT.Diagnosis, EDT.ObservedDifference,
        EDT.ExplanationAssertion, EDT.DemonstratedCause, EDT.AdjustmentOutcomeAssessment,
        EDT.TechnicalConformanceAssessment,
    )
    if any(list(domain_graph.subjects(RDF.type, kind)) for kind in generated_classes):
        return fail("los hechos de entrada ya contienen respuestas o causas")
    input_graph = clone_graph(raw_graph)
    graph = raw_graph
    added = materialize(graph)
    if added <= 0:
        return fail("el razonador no materializó conocimiento nuevo")
    if materialize(graph) != 0:
        return fail("la materialización no es idempotente")
    print(f"OK: razonador externo materializó {added} triples nuevos de forma idempotente")

    expected_technical = {
        EDT.TechnicallyConformant: {
            EDT.C2ExtractionA, EDT.C9Extraction,
            # El agua estimada no cuenta: la caldera cubre la del agua (EKO 3.4.0).
            EDT.C11Extraction, EDT.C16AExtraction, EDT.C16BExtraction,
        },
        EDT.TechnicallyNonConformant: {
            EDT.ExtractionDemo01, EDT.C1Extraction, EDT.C2ExtractionB,
            EDT.C7Extraction2,
            # Caso 17: solo la presión queda fuera del perfil.
            EDT.C17Extraction,
            EDT.C12Extraction, EDT.C13Extraction, EDT.C14Extraction, EDT.C15Extraction,
        },
        EDT.TechnicalConformanceInconclusive: {
            EDT.ExtractionDemo02, EDT.ManualEntryExtractionDemo,
            # Caso 4: todo lo registrado está dentro del perfil, pero faltan TDS y rendimiento.
            EDT.C4Extraction1, EDT.C4Extraction2, EDT.C4Extraction3, EDT.C7Extraction1,
        },
    }
    technical_assessments = list(graph.subjects(RDF.type, EDT.TechnicalConformanceAssessment))
    if len(technical_assessments) != sum(map(len, expected_technical.values())):
        return fail("el razonador no produjo exactamente una evaluación técnica por extracción")
    for category, extractions in expected_technical.items():
        actual = {
            one(graph, assessment, EDT.assessesExtraction)
            for assessment in graph.subjects(EDT.hasTechnicalConformance, category)
        }
        if actual != extractions:
            return fail(f"clasificación técnica incorrecta para {category}: {actual ^ extractions}")
    print("OK: conformidad técnica distingue cobertura completa, desviación confirmada y datos incompletos")

    sensory_decisions = set(graph.subject_objects(EDT.hasSensoryAcceptanceStatus))
    expected_sensory_decisions = {
        (EDT.C4Sensory1, EDT.SensoryRejected),
        (EDT.C4Sensory2, EDT.SensoryRejected),
        (EDT.C4Sensory3, EDT.SensoryAccepted),
        (EDT.C9Sensory, EDT.SensoryRejected),
        (EDT.C17Sensory, EDT.SensoryAccepted),
    }
    if sensory_decisions != expected_sensory_decisions:
        return fail("las decisiones sensoriales explícitas de los casos 4, 9 y 17 no se conservaron exactamente")
    print("OK: la aceptación sensorial solo aparece como decisión explícita del barista")

    c1_observations = set(graph.objects(EDT.C1Extraction, EDT.hasObservation))
    c1_deviations = [
        deviation for deviation in graph.subjects(RDF.type, EDT.TechnicalDeviation)
        if any((deviation, EDT.derivedFrom, observation) in graph for observation in c1_observations)
    ]
    if (
        len(c1_deviations) != 1
        or (c1_deviations[0], EDT.derivedFrom, EDT.C1TimeObs) not in graph
        or (c1_deviations[0], EDT.derivedFrom, EDT.SpecTime) not in graph
    ):
        return fail(
            "el caso 1 debe contener una única desviación técnica, derivada de C1TimeObs"
        )
    print("OK: el caso 1 conserva únicamente la desviación de tiempo")

    # Caso 17, inverso del 9: la desviación técnica no se convierte en un
    # defecto que el barista no percibió, ni cambia su aceptación.
    c17_observations = set(graph.objects(EDT.C17Extraction, EDT.hasObservation))
    c17_deviations = [
        deviation for deviation in graph.subjects(RDF.type, EDT.TechnicalDeviation)
        if any((deviation, EDT.derivedFrom, observation) in graph for observation in c17_observations)
    ]
    if len(c17_deviations) != 1 or (c17_deviations[0], EDT.derivedFrom, EDT.C17PressureObs) not in graph:
        return fail("el caso 17 debe contener una única desviación técnica, derivada de C17PressureObs")
    sensory_defects = set(graph.subjects(RDF.type, EDT.SensoryDefect)) | set(graph.objects(None, EDT.hasPerceivedDefect))
    c17_defect_diagnoses = [
        diagnosis for diagnosis in graph.subjects(RDF.type, EDT.Diagnosis)
        if (diagnosis, EDT.derivedFrom, c17_deviations[0]) in graph
        and set(graph.objects(diagnosis, EDT.isDiagnosisOf)) & sensory_defects
    ]
    if c17_defect_diagnoses:
        return fail("el caso 17 atribuye un defecto sensorial a una bebida aceptada sin defectos")
    if list(graph.objects(EDT.C17Sensory, EDT.hasPerceivedDefect)):
        return fail("el caso 17 no debe tener defectos percibidos")
    print("OK: el caso 17 declara la desviación de presión sin atribuir defectos a la bebida aceptada")

    diagnoses = list(graph.subjects(RDF.type, EDT.Diagnosis))
    recommendation_verbs = re.compile(
        r"\b(repetir|probar|ajustar|modificar|mantener|realizar|volver|investigar|"
        r"iniciar|evaluar|conservar|continuar|usar)\b",
        re.IGNORECASE,
    )
    interpretation_markers = re.compile(r"por ello|por tanto|por eso|indica|respalda|muestra|explica|concentra|permite", re.IGNORECASE)
    limitation_markers = re.compile(r"no (?:constituye|demuestra|convierte|puede|establece|confirma|determina|excluye|predice)|limitad[ao]", re.IGNORECASE)
    for diagnosis in diagnoses:
        explanations = list(graph.subjects(EDT.explains, diagnosis))
        if len(explanations) != 1:
            return fail(f"{diagnosis} debía tener exactamente una explicación")
        is_outcome_assessment = (diagnosis, RDF.type, EDT.AdjustmentOutcomeAssessment) in graph
        has_recommendation = any(
            (action, EDT.respondsTo, diagnosis) in graph
            for action in graph.subjects(RDF.type, EDT.RecommendedAction)
        )
        is_withheld = (None, EDT.hasWithheldRecommendation, diagnosis) in graph
        if has_recommendation and is_withheld:
            return fail(f"{diagnosis} recomienda y a la vez declara que no recomienda")
        if not has_recommendation and not is_outcome_assessment and not is_withheld:
            return fail(f"{diagnosis} debía producir una recomendación verificable o declarar que no la formula")
        if any(values(graph, explanation, predicate) for explanation in explanations for predicate in (EDT.generatedByRule, EDT.recommendedByRule)):
            return fail(f"{diagnosis} conserva una explicación dependiente del vocabulario Rule obsoleto")
        explanation_text = str(one(graph, explanations[0], EDT.description))
        if len(explanation_text) < 260 or not interpretation_markers.search(explanation_text) or not limitation_markers.search(explanation_text):
            return fail(f"{diagnosis} no tiene una explicación suficientemente interpretativa y delimitada")
        if recommendation_verbs.search(explanation_text):
            return fail(f"{diagnosis} mezcla una recomendación de acción dentro de su explicación")
    if list(graph.subjects(RDF.type, EDT.DemonstratedCause)):
        return fail("los casos sintéticos no deben materializar causas demostradas")
    print(f"OK: {len(diagnoses)} diagnósticos tienen explicación basada en evidencia; las evaluaciones de resultado no fuerzan una nueva recomendación y la abstención queda declarada")

    # Solo se recomienda sobre lo que la máquina mide o gobierna.
    unverifiable = [
        action for action in graph.subjects(RDF.type, EDT.RecommendedAction)
        if set(values(graph, action, EDT.adjustsProperty)) & {EDT.GrindSetting, EDT.CoffeeBedCondition}
    ]
    if unverifiable:
        return fail(f"hay recomendaciones sobre molienda o cama de café: {sorted(map(str, unverifiable))}")
    if (EDT.C9Extraction, EDT.hasRecommendedAction, None) in graph:
        return fail("el caso 9 (calibración rechazada con todo conforme) no debe recomendar")
    if (EDT.C9Extraction, EDT.hasWithheldRecommendation, None) not in graph:
        return fail("el caso 9 debe declarar con hasWithheldRecommendation que no recomienda")
    print("OK: ninguna recomendación ajusta molienda o cama; el caso 9 declara que no recomienda")

    diagnostic_rows = list(graph.query(DIAGNOSTIC_QUERY.read_text(encoding="utf-8")))
    returned_diagnoses = {row.diagnosis for row in diagnostic_rows}
    if returned_diagnoses != set(diagnoses):
        missing = set(diagnoses) - returned_diagnoses
        return fail(f"la consulta diagnóstica general no recuperó: {sorted(map(str, missing))}")
    if not all(row.conclusion and row.explanation and row.evidence and row.technicalConformance for row in diagnostic_rows):
        return fail("la respuesta diagnóstica general tiene campos vacíos")
    if any(not row.recommendation and not row.outcomeCategory and not row.withheldFor for row in diagnostic_rows):
        return fail("una respuesta sin recomendación no identifica una categoría de resultado ni declara la abstención")
    print(f"OK: consulta diagnóstica general recupera los {len(diagnoses)} diagnósticos completos")
    demo_diagnoses = [
        diagnosis for diagnosis in diagnoses
        if any(
            (diagnosis, EDT.isDiagnosisOf, deviation) in graph
            and (deviation, EDT.derivedFrom, EDT.ObsTimeDemo) in graph
            for deviation in graph.subjects(RDF.type, EDT.TechnicalDeviation)
        )
    ]
    if not demo_diagnoses or not set(demo_diagnoses) <= returned_diagnoses:
        return fail("el método general no respondió la extracción demostrativa ajena a los siete casos")
    print("OK: el mismo método responde una extracción ajena a los siete casos")

    # Simulación independiente: una extracción nueva con valores y defecto debe
    # recibir la misma explicación desarrollada sin depender de un caso.
    simulation_graph = core_graph + domain_graph
    simulated = EDT.SimulatedExtraction
    simulated_dose = EDT.SimulatedDose
    simulated_beverage = EDT.SimulatedBeverage
    simulated_evaluation = EDT.SimulatedSensoryEvaluation
    simulated_score = EDT.SimulatedOverallLiking
    simulated_observations = {
        EDT.SimulatedDoseObservation: (EDT.MeasuredObservation, EDT.CoffeeDoseMass, simulated_dose, 18.0, EDT.Gram),
        EDT.SimulatedMassObservation: (EDT.MeasuredObservation, EDT.BeverageMass, simulated_beverage, 36.0, EDT.Gram),
        EDT.SimulatedTimeObservation: (EDT.MeasuredObservation, EDT.ExtractionTime, simulated, 28.0, EDT.Second),
        EDT.SimulatedTemperatureObservation: (EDT.MeasuredObservation, EDT.WaterTemperature, simulated, 96.0, EDT.DegreeCelsius),
        EDT.SimulatedPressureObservation: (EDT.MeasuredObservation, EDT.ExtractionPressure, simulated, 9.0, EDT.Bar),
        EDT.SimulatedThermalObservation: (EDT.CalculatedObservation, EDT.ThermalStability, simulated, 1.2, EDT.DegreeCelsius),
    }
    simulation_graph.add((simulated_dose, RDF.type, EDT.CoffeeDose))
    simulation_graph.add((simulated_beverage, RDF.type, EDT.EspressoBeverage))
    for triple in (
        (simulated, RDF.type, EDT.EspressoExtraction),
        (simulated, EDT.performedBy, EDT.BaristaA),
        (simulated, EDT.usesProfile, EDT.perfil_001),
        (simulated, EDT.usesMaterial, EDT.CoffeeLotA),
        (simulated, EDT.usesMaterial, simulated_dose),
        (simulated, EDT.usesEquipment, EDT.MachineValidation),
        (simulated, EDT.usesEquipment, EDT.GrinderValidation),
        (simulated, EDT.producesBeverage, simulated_beverage),
        (simulated, EDT.occurredUnder, EDT.EnvC1),
        (simulated, EDT.startTime, Literal("2026-10-01T09:00:00-05:00", datatype=XSD.dateTime)),
        (simulated, EDT.endTime, Literal("2026-10-01T09:00:28-05:00", datatype=XSD.dateTime)),
        (simulated, EDT.hasSensoryEvaluation, simulated_evaluation),
        (simulated_evaluation, RDF.type, EDT.SensoryEvaluation),
        (simulated_evaluation, EDT.evaluatedBy, EDT.BaristaA),
        (simulated_evaluation, EDT.evaluatesBeverage, simulated_beverage),
        (simulated_evaluation, EDT.evaluationTime, Literal("2026-10-01T09:03:00-05:00", datatype=XSD.dateTime)),
        (simulated_evaluation, EDT.hasPerceivedDefect, EDT.Burnt),
        (simulated_evaluation, EDT.hasSensoryScore, simulated_score),
        (simulated_score, RDF.type, EDT.HedonicLikingScore),
        (simulated_score, EDT.forSensoryAttribute, EDT.OverallLiking),
        (simulated_score, EDT.measuresDimension, EDT.HedonicLiking),
        (simulated_score, EDT.scoreValue, Literal(2)),
    ):
        simulation_graph.add(triple)
    result_time = Literal("2026-10-01T09:00:28-05:00", datatype=XSD.dateTime)
    for node, (kind, prop, feature, numeric, unit) in simulated_observations.items():
        simulation_graph.add((node, RDF.type, kind))
        simulation_graph.add((node, EDT.observedProperty, prop))
        simulation_graph.add((node, EDT.featureOfInterest, feature))
        simulation_graph.add((node, EDT.numericValue, Literal(numeric)))
        simulation_graph.add((node, EDT.hasUnit, unit))
        simulation_graph.add((node, EDT.resultTime, result_time))
        simulation_graph.add((simulated, EDT.hasObservation, node))
    simulation_graph.add((EDT.SimulatedThermalObservation, EDT.computedFrom, EDT.SimulatedTemperatureObservation))
    simulation_graph.add((EDT.SimulatedThermalObservation, EDT.calculationMethodReference, Literal("urn:method:simulation:thermal-stability", datatype=XSD.anyURI)))
    materialize(simulation_graph)
    if values(simulation_graph, simulated_evaluation, EDT.hasSensoryAcceptanceStatus):
        return fail("el razonador infirió aceptación sensorial a partir de puntuaciones o defectos")
    simulated_diagnoses = [
        diagnosis for diagnosis in simulation_graph.subjects(EDT.isDiagnosisOf, EDT.Burnt)
        if any(
            (diagnosis, EDT.derivedFrom, deviation) in simulation_graph
            and (deviation, EDT.derivedFrom, EDT.SimulatedTemperatureObservation) in simulation_graph
            for deviation in simulation_graph.subjects(RDF.type, EDT.TechnicalDeviation)
        )
    ]
    if len(simulated_diagnoses) != 1:
        return fail("la extracción simulada no produjo un diagnóstico independiente")
    simulated_explanation = next(simulation_graph.subjects(EDT.explains, simulated_diagnoses[0]))
    simulated_explanation_text = str(one(simulation_graph, simulated_explanation, EDT.description))
    if not all(fragment in simulated_explanation_text for fragment in ("96", "94", "1.2", "0.7", "quemado", "comportamiento térmico")):
        return fail("la explicación simulada no interpretó valores, límites, defecto y fenómeno técnico")
    if recommendation_verbs.search(simulated_explanation_text) or len(simulated_explanation_text) < 350:
        return fail("la explicación simulada es básica o mezcla acciones recomendadas")
    simulated_action = next(a for a in simulation_graph.subjects(EDT.respondsTo, simulated_diagnoses[0]) if (a, RDF.type, EDT.RecommendedAction) in simulation_graph)
    if not recommendation_verbs.search(str(one(simulation_graph, simulated_action, EDT.description))):
        return fail("la acción de la simulación no quedó separada como recomendación")
    print("OK: una extracción simulada ajena a los casos recibe explicación elaborada y recomendación separada")

    errors = validate_integrity(graph)
    if errors:
        for error in errors:
            fail(error)
        return 1
    print("OK: restricciones de integridad equivalentes a las formas SHACL principales")

    conforms, _, shacl_report = shacl_validate(
        graph,
        shacl_graph=shape_graph,
        inference="rdfs",
        advanced=True,
    )
    if not conforms:
        return fail(f"pySHACL rechazó el grafo materializado:\n{shacl_report}")
    print("OK: pySHACL confirma las formas de EKO y adquisición")

    invalid_decision = clone_graph(input_graph)
    invalid_decision.remove((EDT.ActionShorterTimeDemo, EDT.validationTime, None))
    if not any("tiempo de validación" in error for error in validate_integrity(invalid_decision)):
        return fail("la validación no rechazó una decisión humana sin instante")

    invalid_sequence = clone_graph(input_graph)
    invalid_sequence.set((
        EDT.ExtractionDemo02, EDT.startTime,
        Literal("2026-08-05T10:02:15-05:00", datatype=XSD.dateTime),
    ))
    if not any("comienza antes" in error for error in validate_integrity(invalid_sequence)):
        return fail("la validación no rechazó una extracción anterior al fin del ajuste evaluado")

    invalid_assessment = clone_graph(graph)
    c4_assessment = next(invalid_assessment.subjects(EDT.assessesAdjustment, EDT.C4Adjustment))
    c4_difference = next(
        item for item in invalid_assessment.objects(c4_assessment, EDT.derivedFrom)
        if (item, RDF.type, EDT.ObservedDifference) in invalid_assessment
    )
    invalid_assessment.remove((c4_difference, EDT.hasEvidence, EDT.C4Adjustment))
    if not any("no conecta ajuste" in error for error in validate_integrity(invalid_assessment)):
        return fail("la validación no rechazó una evaluación sin el ajuste como evidencia")

    invalid_technical = clone_graph(graph)
    c9_technical = next(invalid_technical.subjects(EDT.assessesExtraction, EDT.C9Extraction))
    invalid_technical.remove((c9_technical, EDT.derivedFrom, EDT.SpecTDS))
    if not any("cobertura completa" in error for error in validate_integrity(invalid_technical)):
        return fail("la validación no rechazó una conformidad técnica con evidencia incompleta")

    foreign_evidence = clone_graph(graph)
    c9_observation = EDT.C9TimeObs
    foreign_evidence.remove((c9_technical, EDT.derivedFrom, c9_observation))
    foreign_evidence.add((c9_technical, EDT.derivedFrom, EDT.C2TimeObsA))
    if not any("observación ajena" in error for error in validate_integrity(foreign_evidence)):
        return fail("la validación aceptó evidencia observacional de otra extracción")

    foreign_deviation = clone_graph(graph)
    c1_deviation = next(
        deviation for deviation in foreign_deviation.subjects(RDF.type, EDT.TechnicalDeviation)
        if (deviation, EDT.derivedFrom, EDT.C1TimeObs) in foreign_deviation
    )
    foreign_deviation.add((c9_technical, EDT.derivedFrom, c1_deviation))
    if not any("desviación ajena" in error for error in validate_integrity(foreign_deviation)):
        return fail("la validación aceptó una desviación de otra extracción")

    missing_goal = clone_graph(input_graph)
    missing_goal.remove((EDT.C7Adjustment, EDT.targetsOutcome, None))
    if not any("sin objetivo declarado" in error for error in validate_integrity(missing_goal)):
        return fail("la validación aceptó una evaluación de ajuste sin objetivo")

    invalid_specifications = clone_graph(input_graph)
    invalid_specifications.add((EDT.InvalidNegativeTolerance, RDF.type, EDT.ProfileParameterSpecification))
    invalid_specifications.add((EDT.InvalidNegativeTolerance, EDT.targetValue, Literal("9.0", datatype=XSD.decimal)))
    invalid_specifications.add((EDT.InvalidNegativeTolerance, EDT.toleranceValue, Literal("-0.1", datatype=XSD.decimal)))
    invalid_specifications.add((EDT.InvalidMixedSpecification, RDF.type, EDT.ProfileParameterSpecification))
    invalid_specifications.add((EDT.InvalidMixedSpecification, EDT.targetValue, Literal("9.0", datatype=XSD.decimal)))
    invalid_specifications.add((EDT.InvalidMixedSpecification, EDT.minimumValue, Literal("8.5", datatype=XSD.decimal)))
    invalid_specifications.add((EDT.InvalidMixedSpecification, EDT.maximumValue, Literal("9.5", datatype=XSD.decimal)))
    invalid_spec_errors = validate_integrity(invalid_specifications)
    if not any("tolerancia negativa" in error for error in invalid_spec_errors):
        return fail("la validación aceptó una tolerancia negativa")
    if not any("sin mezclarlos" in error for error in invalid_spec_errors):
        return fail("la validación aceptó una especificación que mezcla objetivo e intervalo")
    invalid_conforms, _, _ = shacl_validate(
        invalid_specifications,
        shacl_graph=shape_graph,
        inference="rdfs",
        advanced=True,
    )
    if invalid_conforms:
        return fail("pySHACL aceptó especificaciones deliberadamente inválidas")

    invalid_declared_profile = clone_graph(input_graph)
    invalid_declared_profile.remove((EDT.DeclaredProfileCoffeeLotA, DCTERMS.source, None))
    for label, invalid_profile_graph in (
        ("perfil declarado sin fuente", invalid_declared_profile),
    ):
        invalid_profile_conforms, _, _ = shacl_validate(
            invalid_profile_graph,
            shacl_graph=shape_graph,
            inference="rdfs",
            advanced=True,
        )
        if invalid_profile_conforms:
            return fail(f"pySHACL aceptó {label}")
    print("OK: controles sensoriales rechazan declaraciones sin fuente")

    invalid_sensory = clone_graph(input_graph)
    invalid_sensory.remove((EDT.C9Sensory, EDT.acceptanceRationale, None))
    if not any("requiere justificación" in error for error in validate_integrity(invalid_sensory)):
        return fail("la validación no rechazó un rechazo sensorial sin justificación")
    print("OK: controles negativos rechazan objetivos ausentes, especificaciones inválidas y evidencia ajena")

    # La ruta debe conservar el orden de los tres patrones secuenciales.
    route_checks = (
        (EDT.C4Extraction1, EDT.C4Adjustment, EDT.C4Extraction2),
        (EDT.C4Extraction2, EDT.C4Adjustment2, EDT.C4Extraction3),
        (EDT.C7Extraction1, EDT.C7Adjustment, EDT.C7Extraction2),
    )
    for first, middle, last in route_checks:
        if (first, EDT.precedes, middle) not in graph or (middle, EDT.precedes, last) not in graph:
            return fail(f"ruta incompleta: {first}, {middle}, {last}")
    designated_evaluations = (
        (EDT.ExtractionDemo02, EDT.AdjustmentTimeDuringCalibrationDemo),
        (EDT.C4Extraction2, EDT.C4Adjustment),
        (EDT.C4Extraction3, EDT.C4Adjustment2),
        (EDT.C7Extraction2, EDT.C7Adjustment),
    )
    if any((extraction, EDT.evaluatesOutcomeOf, adjustment) not in graph for extraction, adjustment in designated_evaluations):
        return fail("faltan relaciones explícitas entre extracción evaluadora y ajuste")
    print("OK: rutas temporales de los casos 4 y 7 reproducibles")

    expected_outcomes = {
        EDT.AdjustmentTimeDuringCalibrationDemo: EDT.ExpectedOutcomeObserved,
        EDT.C4Adjustment: EDT.InsufficientOutcomeObserved,
        EDT.C4Adjustment2: EDT.ExpectedOutcomeObserved,
        EDT.C7Adjustment: EDT.AdverseOutcomeObserved,
    }
    assessments = list(graph.subjects(RDF.type, EDT.AdjustmentOutcomeAssessment))
    if len(assessments) != len(expected_outcomes):
        return fail(f"se esperaban {len(expected_outcomes)} evaluaciones de resultado y se obtuvieron {len(assessments)}")
    for adjustment, category in expected_outcomes.items():
        matches = [assessment for assessment in graph.subjects(EDT.assessesAdjustment, adjustment)]
        if len(matches) != 1 or (matches[0], EDT.hasOutcomeCategory, category) not in graph:
            return fail(f"clasificación de resultado incorrecta para {adjustment}")
    expected_targets = {
        EDT.AdjustmentTimeDuringCalibrationDemo: {EDT.ExtractionTime},
        EDT.C4Adjustment: {EDT.ExcessiveAcidity},
        EDT.C4Adjustment2: {EDT.ExcessiveAcidity},
        EDT.C7Adjustment: {EDT.ExcessiveAcidity},
    }
    if any(
        set(graph.objects(adjustment, EDT.targetsOutcome)) != targets
        for adjustment, targets in expected_targets.items()
    ):
        return fail("los ajustes evaluados no conservan sus objetivos explícitos")
    print("OK: resultados clasificados respecto a los objetivos explícitos de cada ajuste")

    def c7_outcome_with(posterior_defect=None, accepted=False):
        scenario = clone_graph(input_graph)
        scenario.remove((EDT.C7Sensory2, EDT.hasPerceivedDefect, None))
        if posterior_defect is not None:
            scenario.add((EDT.C7Sensory2, EDT.hasPerceivedDefect, posterior_defect))
        if accepted:
            scenario.add((EDT.C7Sensory2, EDT.hasSensoryAcceptanceStatus, EDT.SensoryAccepted))
        materialize(scenario)
        assessments = list(scenario.subjects(EDT.assessesAdjustment, EDT.C7Adjustment))
        return one(scenario, assessments[0], EDT.hasOutcomeCategory) if len(assessments) == 1 else None

    if c7_outcome_with(EDT.ExcessiveAcidity) != EDT.InsufficientOutcomeObserved:
        return fail("la persistencia del defecto objetivo no produjo resultado insuficiente")
    if c7_outcome_with() != EDT.InconclusiveOutcome:
        return fail("la ausencia del defecto sin aceptación explícita no produjo resultado inconcluso")
    if c7_outcome_with(accepted=True) != EDT.ExpectedOutcomeObserved:
        return fail("la corrección sensorial con aceptación explícita no produjo resultado esperado")
    print("OK: persistencia, ausencia sin confirmación y aceptación explícita se distinguen correctamente")

    c7_diagnoses = [
        diagnosis for diagnosis in graph.subjects(EDT.assessesAdjustment, EDT.C7Adjustment)
        if (diagnosis, EDT.isDiagnosisOf, EDT.ExcessiveBitterness) in graph
    ]
    if len(c7_diagnoses) != 1:
        return fail("el caso 7 no produjo un único diagnóstico del amargor posterior")
    c7_diagnosis = c7_diagnoses[0]
    c7_diagnosis_text = str(one(graph, c7_diagnosis, EDT.description) or "")
    c7_explanation_nodes = list(graph.subjects(EDT.explains, c7_diagnosis))
    c7_explanation_text = (
        str(one(graph, c7_explanation_nodes[0], EDT.description) or "")
        if len(c7_explanation_nodes) == 1 else ""
    )
    sensory_fragments = (
        "acidez: intensidad 8/10→4/10 y agrado 2/9→7/9",
        "amargor: intensidad 2/10→8/10 y agrado 7/9→2/9",
    )
    if any(fragment not in c7_diagnosis_text or fragment not in c7_explanation_text for fragment in sensory_fragments):
        return fail("el diagnóstico del caso 7 no explica el contraste sensorial cuantitativo completo")
    if re.search(r"la acidez (?:desapareció|fue corregida)", c7_diagnosis_text + " " + c7_explanation_text, re.IGNORECASE):
        return fail("el razonador afirmó una desaparición de la acidez que las puntuaciones no demuestran")
    print("OK: el caso 7 explica disminución de acidez y aumento de amargor sin afirmar desaparición")

    domain_query_text = DOMAIN_QUERIES.read_text(encoding="utf-8")
    domain_blocks = list(query_blocks(domain_query_text, r"(?m)^# --- (CASE\d{2}) ---$"))
    if [identifier for identifier, _ in domain_blocks] != [
        "CASE01", "CASE02", "CASE04", "CASE07", "CASE09",
        "CASE11", "CASE12", "CASE13", "CASE14", "CASE15", "CASE16", "CASE17",
    ]:
        return fail("las consultas no corresponden a los doce casos vigentes")
    if re.search(r"edt:C(?:1[0-7]?|2|4|7|8|9)[A-Z]", domain_query_text):
        return fail("las consultas de casos contienen identificadores sintéticos codificados")
    for identifier, query in domain_blocks:
        rows = list(graph.query(query))
        if not rows:
            return fail(f"{identifier} no produjo respuesta")
        print(f"OK: {identifier} ({len(rows)} evidencia(s) recuperada(s))")

    # Casos 11 a 16 (EKO 3.4.0): cada uno activa su regla con fuente, y solo
    # sobre la extracción que la cumple. La receta es un ejemplo declarado.
    expected_rules = {
        EDT.C11Extraction: {EDT.ColdWaterAcidityHeuristic},
        EDT.C12Extraction: {EDT.DoseConcentrationHeuristic},
        EDT.C13Extraction: {EDT.ShortShotHeuristic},
        EDT.C14Extraction: {EDT.DoseConcentrationHeuristic, EDT.HighDoseTasteHeuristic},
        EDT.C15Extraction: {EDT.LongBitterShotHeuristic},
        EDT.C16BExtraction: {EDT.WithinDispersionHeuristic},
        EDT.C16AExtraction: set(),
    }
    guidance = {
        EDT.BeverageMassFirstHeuristic, EDT.DoseConcentrationHeuristic, EDT.HighDoseTasteHeuristic,
        EDT.WithinDispersionHeuristic, EDT.ColdWaterAcidityHeuristic, EDT.LongBitterShotHeuristic,
        EDT.ShortShotHeuristic,
    }
    for extraction, rules in expected_rules.items():
        used = {
            heuristic
            for action in graph.objects(extraction, EDT.hasRecommendedAction)
            for heuristic in graph.objects(action, EDT.supportedByHeuristic)
        } & guidance
        if used != rules:
            return fail(f"{extraction.split('#')[-1]} activó {sorted(str(u).split('#')[-1] for u in used)} en vez de {sorted(str(r).split('#')[-1] for r in rules)}")
    if (EDT.C11WaterObs, None, None) in graph and any(
        (deviation, EDT.derivedFrom, EDT.C11WaterObs) in graph
        for deviation in graph.subjects(RDF.type, EDT.TechnicalDeviation)
    ):
        return fail("el agua estimada del caso 11 produjo una desviación como si fuera medida")
    print("OK: casos 11–16 activan cada regla con fuente y el agua estimada no se juzga como medida")

    competency_blocks = list(query_blocks(
        QUERIES.read_text(encoding="utf-8"), r"(?m)^# --- (PC\d{2}) ---$",
    ))
    if len(competency_blocks) != 25:
        return fail(f"se esperaban 25 consultas de competencia y se encontraron {len(competency_blocks)}")
    for identifier, query in competency_blocks:
        rows = list(graph.query(query))
        if not rows:
            return fail(f"{identifier} no produjo resultados")
    print("OK: PC01–PC25 ejecutables sobre el grafo materializado")

    pc22_query = dict(competency_blocks)["PC22"]
    pc22_rows = list(graph.query(pc22_query))
    if not any(
        row.extraction == EDT.C9Extraction
        and row.technicalStatus == EDT.TechnicallyConformant
        and row.sensoryStatus == EDT.SensoryRejected
        and row.barista == EDT.BaristaA
        and row.sensoryRationale
        for row in pc22_rows
    ):
        return fail("PC22 no recuperó el conflicto técnico-sensorial explícito del caso 9")
    expected_pc22_sensory = {
        EDT.C4Extraction1: EDT.SensoryRejected,
        EDT.C4Extraction2: EDT.SensoryRejected,
        EDT.C4Extraction3: EDT.SensoryAccepted,
        EDT.C9Extraction: EDT.SensoryRejected,
        EDT.C17Extraction: EDT.SensoryAccepted,
    }
    if not any(
        row.extraction == EDT.C17Extraction
        and row.technicalStatus == EDT.TechnicallyNonConformant
        and row.sensoryStatus == EDT.SensoryAccepted
        and row.sensoryRationale
        for row in pc22_rows
    ):
        return fail("PC22 no recuperó el conflicto inverso del caso 17: no conforme y aceptada")
    for row in pc22_rows:
        if row.sensoryStatus and expected_pc22_sensory.get(row.extraction) != row.sensoryStatus:
            return fail("PC22 presentó como decisión sensorial un estado no declarado")
    print("OK: PC22 recupera conformidad técnica y decisiones sensoriales explícitas como dimensiones independientes")

    pc23_rows = list(graph.query(dict(competency_blocks)["PC23"]))
    if not all(row.outcomeTarget and row.category and row.conclusion and row.explanation for row in pc23_rows):
        return fail("PC23 no recuperó objetivo, categoría, conclusión y explicación")
    if {row.adjustment for row in pc23_rows} != set(expected_targets):
        return fail("PC23 no cubre todos los ajustes evaluados")
    print("OK: PC23 responde qué pretendía corregir cada ajuste y qué resultado se observó")

    pc24_rows = list(graph.query(dict(competency_blocks)["PC24"]))
    if not any(
        row.extraction == EDT.ManualEntryExtractionDemo
        and row.terminationCause == EDT.ManualNormalStop
        and Decimal(str(row.maximumTime)) == Decimal("30.0")
        and Decimal(str(row.dose)) == Decimal("18.0")
        and Decimal(str(row.finalMass)) == Decimal("36.0")
        and Decimal(str(row.tds)) == Decimal("9.2")
        and Decimal(str(row["yield"])) == Decimal("18.4")
        and Decimal(str(row.observedTime)) == Decimal("28.0")
        and row.recordedBy == EDT.BaristaDemo
        for row in pc24_rows
    ):
        return fail("PC24 no reconstruyó la finalización ni los datos externos del rendimiento")
    print("OK: PC24 reconstruye la causa de finalización y el ingreso manual usado para calcular el rendimiento")

    pc25_rows = list(graph.query(dict(competency_blocks)["PC25"]))
    if not any(
        row.extraction == EDT.C4Extraction3
        and row.coffee == EDT.CoffeeLotA
        and row.declaredProfile == EDT.DeclaredProfileCoffeeLotA
        and row.targetProfile == EDT.TargetProfileCoffeeLotA
        and row.evaluation == EDT.C4Sensory3
        and row.observedDescriptor in {EDT.Chocolate, EDT.Caramel}
        for row in pc25_rows
    ):
        return fail("PC25 no separó perfil declarado, objetivo y evaluación observada")
    print("OK: PC25 distingue declaración de fuente, objetivo del barista y observación de la taza")

    sensor_observations = list(graph.query("""
        PREFIX edt: <https://w3id.org/espresso-dt#>
        PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>
        SELECT ?observation WHERE {
          ?observation a ?observationType ; edt:madeBySensor ?sensor .
          ?observationType rdfs:subClassOf* edt:Observation .
          ?sensor a ?sensorType . ?sensorType rdfs:subClassOf* edt:Sensor .
        }
    """))
    if not sensor_observations:
        return fail("no hay observaciones para probar SensorObservation")

    try:
        from owlready2 import get_ontology, sync_reasoner
    except ImportError:
        print("SKIP: HermiT no ejecutado porque owlready2 no está instalado")
    else:
        with TemporaryDirectory() as directory:
            rdfxml = Path(directory) / "espresso-dt.rdf"
            reasoner_graph = Graph()
            for triple in graph:
                if triple[1] != OWL.imports:
                    reasoner_graph.add(triple)
            reasoner_graph.serialize(rdfxml, format="xml")
            ontology = get_ontology(rdfxml.as_uri()).load()
            with ontology:
                try:
                    sync_reasoner(debug=0)
                except Exception as exc:
                    return fail(f"HermiT detectó inconsistencia: {type(exc).__name__}")
            inferred_class = ontology.search_one(iri="*SensorObservation")
            if inferred_class is None or not list(inferred_class.instances()):
                return fail("HermiT no infirió SensorObservation")
        print("OK: HermiT confirma consistencia e infiere SensorObservation")

    case2_diagnoses = list(graph.subjects(EDT.isDiagnosisOf, EDT.Burnt))
    if not case2_diagnoses:
        return fail("el caso 2 no produjo diagnóstico del defecto Burnt")
    case2_text = " ".join(str(one(graph, item, EDT.description)) for item in case2_diagnoses)
    if not all(fragment in case2_text for fragment in ("96", "94", "1.4", "0.7", "comportamiento térmico")):
        return fail("el caso 2 no conserva valores, límites y conclusión térmica")
    print("OK: el caso 2 produce una conclusión térmica concreta con valores y límites")

    # Caso 4: dos ajustes de temperatura del agua con todo lo demás dentro del
    # perfil; el resultado se juzga por la evaluación sensorial del barista.
    first_assessments = list(graph.subjects(EDT.assessesAdjustment, EDT.C4Adjustment))
    second_assessments = list(graph.subjects(EDT.assessesAdjustment, EDT.C4Adjustment2))
    if len(first_assessments) != 1 or len(second_assessments) != 1:
        return fail("el caso 4 debía evaluar cada ajuste de temperatura exactamente una vez")
    first_assessment, second_assessment = first_assessments[0], second_assessments[0]
    if (
        (first_assessment, EDT.hasOutcomeCategory, EDT.InsufficientOutcomeObserved) not in graph
        or (first_assessment, EDT.isDiagnosisOf, EDT.ExcessiveAcidity) not in graph
    ):
        return fail("el primer ajuste de temperatura del caso 4 no quedó como insuficiente frente a la acidez persistente")
    first_actions = [
        action for action in graph.subjects(EDT.respondsTo, first_assessment)
        if (action, RDF.type, EDT.RecommendedAction) in graph
    ]
    if (
        len(first_actions) != 1
        or one(graph, first_actions[0], EDT.adjustsProperty) != EDT.WaterTemperature
        or (EDT.C4Extraction2, EDT.hasRecommendedAction, first_actions[0]) not in graph
    ):
        return fail("el caso 4 debía recomendar otra prueba sobre la temperatura del agua tras el primer ajuste")
    if (
        (second_assessment, EDT.hasOutcomeCategory, EDT.ExpectedOutcomeObserved) not in graph
        or (second_assessment, EDT.isDiagnosisOf, EDT.ExcessiveAcidity) not in graph
        or not any(
            (EDT.C4Extraction3, EDT.hasSensoryEvaluation, evaluation) in graph
            and (evaluation, EDT.hasSensoryAcceptanceStatus, EDT.SensoryAccepted) in graph
            for evaluation in graph.objects(EDT.C4Extraction3, EDT.hasSensoryEvaluation)
        )
    ):
        return fail("el segundo ajuste de temperatura del caso 4 no logró el resultado esperado con aceptación explícita")
    if any(
        deviation for deviation in graph.subjects(RDF.type, EDT.TechnicalDeviation)
        for extraction in (EDT.C4Extraction1, EDT.C4Extraction2, EDT.C4Extraction3)
        if any((deviation, EDT.derivedFrom, obs) in graph for obs in graph.objects(extraction, EDT.hasObservation))
    ):
        return fail("las extracciones del caso 4 debían quedar dentro del perfil en todo lo registrado")
    first_text = str(one(graph, first_assessment, EDT.description))
    if not all(fragment in first_text for fragment in ("temperatura del agua", "insuficiente", "acidez excesiva")):
        return fail("la conclusión del primer ajuste del caso 4 no nombra el parámetro, el defecto y la insuficiencia")
    print("OK: el caso 4 muestra que subir el agua 92,0→92,5 °C no corrigió la acidez (insuficiente) y que 92,5→93,5 °C sí, con aceptación explícita del barista")

    # EKO se publica para lectores de fuera: cada término con etiqueta en español
    # la lleva también en inglés, y las explicaciones del razonador siguen en
    # español aunque el término tenga las dos.
    terms = set()
    for kind in (OWL.Class, OWL.ObjectProperty, OWL.DatatypeProperty, OWL.AnnotationProperty):
        terms.update(s for s in graph.subjects(RDF.type, kind) if isinstance(s, URIRef))
    for term in sorted(terms):
        langs = {v.language for v in graph.objects(term, RDFS.label)}
        if "es" in langs and "en" not in langs:
            return fail(f"{term} tiene etiqueta en español y no en inglés")
    if term_label(graph, EDT.targetValue) != "valor objetivo":
        return fail("las explicaciones del razonador no tomaron la etiqueta en español")
    print("OK: los términos de EKO tienen etiqueta en español y en inglés, y el razonador explica en español")

    print("PASS: EKO 3.5.0 y el módulo opcional de adquisición verificados sin mezclar responsabilidades")
    return 0


if __name__ == "__main__":
    sys.exit(main())
