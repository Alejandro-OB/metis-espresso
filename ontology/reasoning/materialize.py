#!/usr/bin/env python3
"""Materializa conocimiento mediante comparación general de evidencias.

No contiene heurísticas diseñadas para casos concretos. Detecta desviaciones,
evalúa la conformidad técnica completa frente al perfil, contrasta extracciones
y secuencias, formula diagnósticos probables y propone una nueva prueba que
permita confirmar o descartar cada diagnóstico. La aceptación sensorial nunca
se infiere: pertenece al juicio explícito del barista.
"""

from argparse import ArgumentParser
from datetime import datetime
from decimal import Decimal
from pathlib import Path
import re

from rdflib import Graph, Literal, Namespace, RDF, RDFS

EDT = Namespace("https://w3id.org/espresso-dt#")
REASONER = EDT.EvidenceBasedReasoner

# Contrato textual de ExplanationAssertion. Una explicación debe interpretar
# la evidencia y declarar el alcance de la conclusión; las acciones que se
# deben ejecutar pertenecen exclusivamente a RecommendedAction.
RECOMMENDATION_IN_EXPLANATION = re.compile(
    r"\b(repetir|probar|ajustar|modificar|mantener|realizar|volver|investigar|"
    r"iniciar|evaluar|conservar|continuar|usar)\b",
    re.IGNORECASE,
)
INTERPRETATION_MARKER = re.compile(
    r"por ello|por tanto|por eso|indica|respalda|muestra|explica|concentra|permite",
    re.IGNORECASE,
)
LIMITATION_MARKER = re.compile(
    r"no (?:constituye|demuestra|convierte|puede|establece|confirma|determina|"
    r"excluye|predice|localiza)|limitad[ao]",
    re.IGNORECASE,
)


def one(g, s, p):
    return next(g.objects(s, p), None)


def decimal_value(g, s, p=EDT.numericValue):
    value = one(g, s, p)
    return Decimal(str(value)) if value is not None else None


def local_name(node):
    value = str(node).rsplit("#", 1)[-1].rsplit("/", 1)[-1]
    return re.sub(r"[^A-Za-z0-9_-]", "-", value)


def inferred_iri(kind, *nodes):
    return EDT[f"Inferred{kind}-" + "-".join(local_name(n) for n in nodes)]


def set_description(g, node, text):
    g.set((node, EDT.description, Literal(text, lang="es")))


def label(g, node):
    # Las explicaciones se redactan en español, y desde que EKO publica también
    # sus etiquetas en inglés un término tiene dos: tomar la primera que
    # devuelva el grafo mezclaría los idiomas según el orden de carga.
    values = list(g.objects(node, RDFS.label))
    value = next((v for v in values if getattr(v, "language", None) == "es"), None)
    if value is None:
        value = next((v for v in values if getattr(v, "language", None) is None), None)
    return str(value) if value is not None else local_name(node)


def number(value):
    return format(value.normalize(), "f") if value is not None else ""


def unit_text(g, unit):
    symbols = {
        EDT.DegreeCelsius: "°C", EDT.Second: "s", EDT.Gram: "g",
        EDT.Percent: "%", EDT.DegreeBrix: "°Brix", EDT.Bar: "bar",
        EDT.Point: "puntos", EDT.Dimensionless: "",
    }
    return symbols.get(unit, label(g, unit) if unit else "")


def observations(g, extraction, property_node=None):
    return [o for o in g.objects(extraction, EDT.hasObservation)
            if property_node is None or one(g, o, EDT.observedProperty) == property_node]


def observation(g, extraction, property_node):
    values = observations(g, extraction, property_node)
    return values[0] if values else None


def observation_map(g, extraction):
    return {one(g, o, EDT.observedProperty): o for o in observations(g, extraction)
            if one(g, o, EDT.observedProperty) is not None}


def observation_value(g, node):
    numeric = decimal_value(g, node)
    if numeric is not None:
        return numeric
    text = one(g, node, EDT.textValue)
    return str(text).strip().lower() if text is not None else None


def specification(g, extraction, property_node, unit=None):
    profile = one(g, extraction, EDT.usesProfile)
    for spec in g.objects(profile, EDT.hasParameterSpecification) if profile else ():
        if one(g, spec, EDT.forProperty) == property_node and (unit is None or one(g, spec, EDT.hasUnit) == unit):
            return spec
    return None


def limits(g, spec):
    minimum = decimal_value(g, spec, EDT.minimumValue)
    maximum = decimal_value(g, spec, EDT.maximumValue)
    target = decimal_value(g, spec, EDT.targetValue)
    tolerance = decimal_value(g, spec, EDT.toleranceValue)
    if target is not None:
        # Un objetivo sin margen no dice cuánto apartarse es demasiado. Tomarlo
        # como exacto declaraba fuera casi cualquier lectura con un margen que
        # nadie decidió; sin límites, la especificación queda sin cubrir y la
        # conformidad, inconclusa. Es la misma regla que aplicaba la plataforma
        # (`espresso_stability.conformity`, decisión del 2026-10-10), que ahora
        # vive solo aquí.
        if tolerance is None:
            return None, None
        return target - tolerance, target + tolerance
    return minimum, maximum


def is_within_profile(g, extraction, property_node):
    obs = observation(g, extraction, property_node)
    spec = specification(g, extraction, property_node, one(g, obs, EDT.hasUnit)) if obs else None
    value = decimal_value(g, obs) if obs else None
    if spec is None or value is None:
        return False
    minimum, maximum = limits(g, spec)
    return (minimum is None or value >= minimum) and (maximum is None or value <= maximum)


def evaluations(g, extraction):
    return list(g.objects(extraction, EDT.hasSensoryEvaluation))


def defects(g, extraction):
    result = []
    for evaluation in evaluations(g, extraction):
        result.extend(g.objects(evaluation, EDT.hasPerceivedDefect))
    return list(dict.fromkeys(result))


def evaluation_with_defect(g, extraction, defect):
    return next((e for e in evaluations(g, extraction) if (e, EDT.hasPerceivedDefect, defect) in g), None)


def overall_score(g, extraction):
    for evaluation in evaluations(g, extraction):
        for score in g.objects(evaluation, EDT.hasSensoryScore):
            if one(g, score, EDT.forSensoryAttribute) == EDT.OverallLiking and one(g, score, EDT.measuresDimension) == EDT.HedonicLiking:
                return score, decimal_value(g, score, EDT.scoreValue)
    return None, None


def sensory_score_map(g, extraction):
    """Indexa puntuaciones por atributo y dimensión para compararlas entre pruebas."""
    result = {}
    for evaluation in evaluations(g, extraction):
        for score in g.objects(evaluation, EDT.hasSensoryScore):
            attribute = one(g, score, EDT.forSensoryAttribute)
            dimension = one(g, score, EDT.measuresDimension)
            value = decimal_value(g, score, EDT.scoreValue)
            if attribute is not None and dimension is not None and value is not None:
                result[(attribute, dimension)] = (score, value)
    return result


def sensory_score_comparison(g, initial, posterior):
    """Resume cambios comparables sin convertir puntuaciones en defectos o aceptación."""
    before = sensory_score_map(g, initial)
    after = sensory_score_map(g, posterior)
    common = set(before) & set(after)
    evidence = [node for key in sorted(common, key=lambda item: (str(item[0]), str(item[1])))
                for node in (before[key][0], after[key][0])]
    details = []
    attributes = sorted({attribute for attribute, _ in common}, key=str)
    dimensions = (
        (EDT.PerceivedIntensity, "intensidad", 10),
        (EDT.HedonicLiking, "agrado", 9),
    )
    for attribute in attributes:
        changes = []
        for dimension, dimension_name, maximum in dimensions:
            key = (attribute, dimension)
            if key not in common:
                continue
            before_value, after_value = before[key][1], after[key][1]
            if before_value != after_value:
                changes.append(
                    f"{dimension_name} {number(before_value)}/{maximum}→{number(after_value)}/{maximum}"
                )
        if changes:
            details.append(f"{label(g, attribute)}: {human_list(changes)}")
    return human_list(details), evidence


def same_day(g, a, b):
    def date(node):
        literal = one(g, node, EDT.startTime)
        value = literal.toPython() if literal is not None else None
        return value.date() if isinstance(value, datetime) else None
    return date(a) is not None and date(a) == date(b)


def shared_coffee(g, a, b):
    shared = set(g.objects(a, EDT.usesMaterial)) & set(g.objects(b, EDT.usesMaterial))
    return any((item, RDF.type, EDT.CoffeeMaterial) in g for item in shared)


def comparable(g, a, b):
    return a != b and one(g, a, EDT.usesProfile) == one(g, b, EDT.usesProfile) and shared_coffee(g, a, b) and same_day(g, a, b)


def add_difference(g, key, extractions, evidence, text):
    ordered = sorted(set(extractions), key=str)
    node = inferred_iri("Difference", key, *ordered)
    g.add((node, RDF.type, EDT.ObservedDifference)); g.add((node, EDT.generatedBy, REASONER))
    g.set((node, EDT.statusCode, Literal("observed-difference")))
    for extraction in ordered: g.add((node, EDT.comparesExtraction, extraction))
    for item in set(evidence):
        if item is not None: g.add((node, EDT.hasEvidence, item))
    set_description(g, node, text)
    return node


def add_diagnosis(g, context, target, evidence, text):
    node = inferred_iri("Diagnosis", context, target)
    g.add((node, RDF.type, EDT.Diagnosis)); g.add((node, EDT.isDiagnosisOf, target)); g.add((node, EDT.generatedBy, REASONER))
    g.set((node, EDT.statusCode, Literal("probable-diagnosis")))
    for item in set(evidence):
        if item is not None: g.add((node, EDT.derivedFrom, item))
    set_description(g, node, text)
    return node


def mark_outcome_assessment(g, diagnosis, adjustment, category):
    """Especializa un diagnóstico como evaluación contextual de un ajuste."""
    g.add((diagnosis, RDF.type, EDT.AdjustmentOutcomeAssessment))
    g.set((diagnosis, EDT.assessesAdjustment, adjustment))
    g.set((diagnosis, EDT.hasOutcomeCategory, category))
    g.set((diagnosis, EDT.statusCode, Literal("adjustment-outcome-assessment")))
    return diagnosis


def add_positive_outcome_assessment(g, evaluation, adjustment, targets, difference, evidence, conclusion, explanation):
    """Materializa una evaluación positiva respaldada por valores observados."""
    ordered_targets = sorted(set(targets), key=str)
    key = ordered_targets[0] if ordered_targets else difference
    node = inferred_iri("OutcomeAssessment", adjustment, evaluation, key)
    g.add((node, RDF.type, EDT.Diagnosis))
    g.add((node, RDF.type, EDT.AdjustmentOutcomeAssessment))
    g.add((node, EDT.generatedBy, REASONER))
    g.set((node, EDT.assessesAdjustment, adjustment))
    g.set((node, EDT.hasOutcomeCategory, EDT.ExpectedOutcomeObserved))
    g.set((node, EDT.statusCode, Literal("adjustment-outcome-assessment")))
    for target in ordered_targets or [difference]:
        g.add((node, EDT.isDiagnosisOf, target))
    for item in {difference, *evidence}:
        if item is not None:
            g.add((node, EDT.derivedFrom, item))
    set_description(g, node, conclusion)
    add_explanation(g, node, [difference, *evidence], explanation)
    return node


def add_explanation(g, diagnosis, evidence, text):
    if len(text) < 260 or not INTERPRETATION_MARKER.search(text) or not LIMITATION_MARKER.search(text):
        raise ValueError(
            "ExplanationAssertion debe comparar la evidencia, interpretarla y declarar sus límites"
        )
    if RECOMMENDATION_IN_EXPLANATION.search(text):
        raise ValueError(
            "ExplanationAssertion contiene una acción; las recomendaciones pertenecen a RecommendedAction"
        )
    node = inferred_iri("Explanation", diagnosis)
    g.add((node, RDF.type, EDT.ExplanationAssertion)); g.add((node, EDT.explains, diagnosis)); g.add((node, EDT.generatedBy, REASONER))
    g.set((node, EDT.statusCode, Literal("probable-explanation")))
    for item in set(evidence):
        if item is not None: g.add((node, EDT.hasEvidence, item))
    set_description(g, node, text)
    return node


def add_recommendation(
    g,
    extraction,
    diagnosis,
    property_node,
    text,
    procedure=EDT.IterativeCalibrationProcedure,
    heuristic=None,
):
    node = inferred_iri("Recommendation", extraction, diagnosis, property_node)
    g.add((node, RDF.type, EDT.RecommendedAction)); g.add((node, EDT.respondsTo, diagnosis))
    g.add((node, EDT.adjustsProperty, property_node)); g.add((node, EDT.generatedBy, REASONER))
    g.add((node, EDT.derivedFrom, diagnosis))
    if procedure:
        g.add((node, EDT.followsProcedure, procedure))
    if heuristic:
        g.add((node, EDT.supportedByHeuristic, heuristic))
    g.add((node, EDT.governedByPolicy, EDT.HumanRecommendationValidationPolicy))
    g.add((extraction, EDT.hasRecommendedAction, node)); set_description(g, node, text)
    return node


def materialize_profile_deviations(g):
    result = {}
    for extraction in list(g.subjects(RDF.type, EDT.EspressoExtraction)):
        for obs in observations(g, extraction):
            # Una estimación no se juzga como si fuera una medida (EKO 3.4.0):
            # la temperatura del agua sale de la caldera con una recta o con el
            # gemelo, y una desviación sobre ella afirmaría lo que nadie midió.
            if (obs, RDF.type, EDT.EstimatedObservation) in g:
                continue
            value, prop, unit = decimal_value(g, obs), one(g, obs, EDT.observedProperty), one(g, obs, EDT.hasUnit)
            spec = specification(g, extraction, prop, unit) if value is not None and prop and unit else None
            if spec is None: continue
            minimum, maximum = limits(g, spec); direction = bound = None
            if minimum is not None and value < minimum: direction, bound = "below", minimum
            elif maximum is not None and value > maximum: direction, bound = "above", maximum
            if direction is None: continue
            node = inferred_iri("Deviation", extraction, prop, direction)
            g.add((node, RDF.type, EDT.TechnicalDeviation)); g.add((node, EDT.derivedFrom, obs)); g.add((node, EDT.derivedFrom, spec))
            g.add((node, EDT.basedOnCriterion, EDT.ProfileConformanceCriterion)); g.add((node, EDT.generatedBy, REASONER))
            g.set((node, EDT.statusCode, Literal(f"{direction}-profile")))
            relation = "por debajo" if direction == "below" else "por encima"
            suffix = f" {unit_text(g, unit)}" if unit_text(g, unit) else ""
            set_description(g, node, f"{label(g, prop)}={number(value)}{suffix} quedó {relation} del límite {number(bound)}{suffix} del perfil.")
            result[(extraction, prop, direction)] = node
    return result


def deviations_for(index, extraction):
    return {prop: node for (candidate, prop, _), node in index.items() if candidate == extraction}


def observation_for_specification(g, extraction, spec):
    """Devuelve una observación numérica con propiedad y unidad compatibles."""
    prop, unit = one(g, spec, EDT.forProperty), one(g, spec, EDT.hasUnit)
    minimum, maximum = limits(g, spec)
    if prop is None or unit is None or (minimum is None and maximum is None):
        return None
    return next(
        (
            obs for obs in observations(g, extraction, prop)
            if one(g, obs, EDT.hasUnit) == unit and decimal_value(g, obs) is not None
            and (obs, RDF.type, EDT.EstimatedObservation) not in g
        ),
        None,
    )


def latest_assessment_time(g, extraction, matched_observations):
    """Usa el instante de la evidencia más reciente, de forma determinista."""
    times = [one(g, obs, EDT.resultTime) for obs in matched_observations]
    times = [value for value in times if value is not None]
    if times:
        return max(times, key=lambda value: value.toPython())
    return one(g, extraction, EDT.endTime)


def materialize_technical_conformance(g, deviation_index):
    """Clasifica cada extracción sin confundir ausencia de datos con conformidad."""
    for extraction in list(g.subjects(RDF.type, EDT.EspressoExtraction)):
        profile = one(g, extraction, EDT.usesProfile)
        if profile is None:
            continue
        specs = set(g.objects(profile, EDT.hasParameterSpecification))
        # La especificación de la que se deriva otra se juzga a través de la
        # derivada (EKO 3.4.0): la del agua, por la de caldera que se saca de
        # ella con la recta del acta. Exigirle una medida propia dejaría
        # siempre inconclusa una propiedad que ningún sensor mide.
        judged_through = {one(g, spec, EDT.derivedFromSpecification) for spec in specs}
        specs = sorted(specs - judged_through, key=str)
        matched = {
            spec: observation_for_specification(g, extraction, spec)
            for spec in specs
        }
        matched = {spec: obs for spec, obs in matched.items() if obs is not None}
        missing = [spec for spec in specs if spec not in matched]
        deviations = list(deviations_for(deviation_index, extraction).values())

        if deviations:
            category = EDT.TechnicallyNonConformant
            detail = human_list(str(one(g, node, EDT.description)).rstrip(".") for node in deviations)
            conclusion = (
                f"La extracción {local_name(extraction)} no fue técnicamente conforme con {label(g, profile)}: "
                f"{detail}. Se cubrieron {len(matched)} de {len(specs)} especificaciones; una desviación confirmada "
                "basta para establecer la no conformidad aunque falten otras mediciones."
            )
        elif specs and not missing:
            category = EDT.TechnicallyConformant
            properties = [one(g, spec, EDT.forProperty) for spec in specs]
            conclusion = (
                f"La extracción {local_name(extraction)} fue técnicamente conforme con {label(g, profile)}: "
                f"las {len(specs)} especificaciones ({property_names(g, properties)}) tuvieron una observación "
                "numérica con unidad compatible y ninguna quedó fuera de sus límites."
            )
        else:
            category = EDT.TechnicalConformanceInconclusive
            missing_properties = [one(g, spec, EDT.forProperty) for spec in missing]
            conclusion = (
                f"La conformidad técnica de {local_name(extraction)} frente a {label(g, profile)} quedó inconclusa: "
                f"se cubrieron {len(matched)} de {len(specs)} especificaciones y faltaron observaciones numéricas "
                f"con unidad compatible para {property_names(g, missing_properties) if missing_properties else 'las especificaciones del perfil'}. "
                "La ausencia de una desviación no se interpreta como conformidad."
            )

        node = inferred_iri("TechnicalConformanceAssessment", extraction, profile)
        g.add((node, RDF.type, EDT.TechnicalConformanceAssessment))
        g.set((node, EDT.assessesExtraction, extraction))
        g.set((node, EDT.againstProfile, profile))
        g.set((node, EDT.hasTechnicalConformance, category))
        g.add((node, EDT.generatedBy, REASONER))
        g.set((node, EDT.statusCode, Literal("technical-conformance-assessment")))
        assessment_time = latest_assessment_time(g, extraction, matched.values())
        if assessment_time is not None:
            g.set((node, EDT.assessmentTime, assessment_time))
        for evidence in [*specs, *matched.values(), *deviations]:
            g.add((node, EDT.derivedFrom, evidence))
        set_description(g, node, conclusion)
    return None


def phenomenon(g, properties):
    props = set(properties); thermal = {EDT.WaterTemperature, EDT.BoilerBodyTemperature, EDT.ThermalStability}
    if props and props <= thermal: return "el comportamiento térmico"
    names = ", ".join(label(g, p) for p in sorted(props, key=str))
    return f"la especificación de {names}" if len(props) == 1 else f"las especificaciones de {names}"


def deviation_details(g, deviations):
    return "; ".join(str(one(g, node, EDT.description)).rstrip(".") for node in deviations.values())


# Lo que la máquina mide o gobierna, más lo que se calcula con medidas
# (dosis, TDS y rendimiento). Solo sobre esto se diagnostica y se recomienda:
# la molienda, la condición de la cama y el contexto ambiental quedan como
# contexto registrado (decisión del 3-nov-2026).
VERIFIABLE_PROPERTIES = frozenset({
    EDT.WaterTemperature, EDT.BoilerBodyTemperature, EDT.ThermalStability,
    EDT.ExtractionTime, EDT.ExtractionPressure, EDT.PreinfusionDuration,
    EDT.BeverageMass, EDT.CoffeeDoseMass, EDT.BrewRatio, EDT.TDS, EDT.ExtractionYield,
})


def verifiable(properties):
    return {prop for prop in properties if prop in VERIFIABLE_PROPERTIES}


def preferred_property(properties):
    """La propiedad comprobable de mayor prioridad, o None si no hay ninguna."""
    props = verifiable(properties)
    if EDT.ThermalStability in props: props.add(EDT.WaterTemperature)
    priority = (EDT.WaterTemperature, EDT.ExtractionTime, EDT.BeverageMass,
                EDT.ExtractionPressure, EDT.CoffeeDoseMass, EDT.TDS, EDT.ExtractionYield)
    return next((p for p in priority if p in props), next(iter(sorted(props, key=str)), None))


def withhold_recommendation(g, extraction, diagnosis):
    """Deja constancia de que el razonador analizó y no recomienda: ninguna
    variable que la máquina comprueba aísla una causa."""
    g.add((extraction, EDT.hasWithheldRecommendation, diagnosis))
    return None


def constraint_text(g, extraction, prop):
    obs = observation(g, extraction, prop); unit = one(g, obs, EDT.hasUnit) if obs else None
    spec = specification(g, extraction, prop, unit) if obs else None
    if spec is None: return label(g, prop)
    minimum, maximum = limits(g, spec); unit_label = unit_text(g, unit)
    target = decimal_value(g, spec, EDT.targetValue)
    tolerance = decimal_value(g, spec, EDT.toleranceValue)
    if target is not None and tolerance is None:
        return f"{label(g, prop)} con objetivo {number(target)} {unit_label} y sin tolerancia declarada".rstrip()
    if minimum is not None and maximum is not None: return f"{label(g, prop)} entre {number(minimum)} y {number(maximum)} {unit_label}".rstrip()
    if maximum is not None: return f"{label(g, prop)} como máximo en {number(maximum)} {unit_label}".rstrip()
    if minimum is not None: return f"{label(g, prop)} como mínimo en {number(minimum)} {unit_label}".rstrip()
    return label(g, prop)


def observed_value_text(g, extraction, prop):
    obs = observation(g, extraction, prop)
    value = decimal_value(g, obs) if obs else None
    suffix = unit_text(g, one(g, obs, EDT.hasUnit)) if obs else ""
    return f"{label(g, prop)}={number(value)}{(' ' + suffix) if suffix else ''}"


def recommend_verification(g, extraction, diagnosis, properties, effect):
    props = sorted(verifiable(properties), key=str); primary = preferred_property(props)
    if primary is None:
        return withhold_recommendation(g, extraction, diagnosis)
    constraints = ", y ".join(constraint_text(g, extraction, p) for p in props) or label(g, primary)
    effect_text = label(g, effect) if (effect, RDF.type, EDT.SensoryDefect) in g else "el resultado no deseado"
    text = (f"Repetir la extracción manteniendo {constraints}, sin modificar los demás parámetros. Evaluar sensorialmente la nueva bebida: "
            f"si desaparece {effect_text}, el diagnóstico gana respaldo; si continúa, debe investigarse otra variable no registrada.")
    heuristic = EDT.AcidityAdjustmentHeuristic if effect == EDT.ExcessiveAcidity else None
    return add_recommendation(g, extraction, diagnosis, primary, text, heuristic=heuristic)


def compare_observations(g, a, b):
    ma, mb = observation_map(g, a), observation_map(g, b); changed, equal, evidence = [], [], []
    for prop in sorted(set(ma) & set(mb), key=str):
        evidence += [ma[prop], mb[prop]]
        (equal if observation_value(g, ma[prop]) == observation_value(g, mb[prop]) else changed).append(prop)
    return changed, equal, evidence


def human_list(items):
    values = [str(item) for item in items if item]
    if not values:
        return "ninguno"
    if len(values) == 1:
        return values[0]
    return ", ".join(values[:-1]) + " y " + values[-1]


def property_names(g, properties):
    return human_list(label(g, prop) for prop in sorted(set(properties), key=str))


def comparison_values(g, reference, affected, properties):
    details = []
    for prop in sorted(set(properties), key=str):
        before = observation(g, reference, prop); after = observation(g, affected, prop)
        if before is None or after is None:
            continue
        before_value = observation_value(g, before); after_value = observation_value(g, after)
        unit = unit_text(g, one(g, after, EDT.hasUnit))
        suffix = f" {unit}" if unit else ""
        details.append(f"{label(g, prop)} {number(before_value) if isinstance(before_value, Decimal) else before_value}{suffix}→"
                       f"{number(after_value) if isinstance(after_value, Decimal) else after_value}{suffix}")
    return human_list(details)


def profile_checked(g, extraction):
    checked, conforming = [], []
    for obs in observations(g, extraction):
        prop = one(g, obs, EDT.observedProperty)
        if prop is None or specification(g, extraction, prop, one(g, obs, EDT.hasUnit)) is None:
            continue
        checked.append(prop)
        if is_within_profile(g, extraction, prop):
            conforming.append(prop)
    return list(dict.fromkeys(checked)), list(dict.fromkeys(conforming))


def context_values(g, context):
    details = []
    if context is None:
        return "contexto no registrado"
    for obs in sorted(g.subjects(EDT.featureOfInterest, context), key=str):
        prop = one(g, obs, EDT.observedProperty); value = decimal_value(g, obs)
        if prop is None or value is None:
            continue
        unit = unit_text(g, one(g, obs, EDT.hasUnit)); suffix = f" {unit}" if unit else ""
        details.append(f"{label(g, prop)}={number(value)}{suffix}")
    return human_list(details) if details else local_name(context)


def context_observation(g, context, property_node):
    """Recupera una observación cuantitativa vinculada a una condición contextual."""
    return next(
        (
            candidate for candidate in g.subjects(EDT.featureOfInterest, context)
            if one(g, candidate, EDT.observedProperty) == property_node
            and decimal_value(g, candidate) is not None
        ),
        None,
    )


def defect_names(g, extraction):
    return property_names(g, defects(g, extraction)) if defects(g, extraction) else "sin defecto sensorial registrado"


def choose_affected(g, a, b):
    da, db = defects(g, a), defects(g, b)
    if bool(da) != bool(db): return (a, b) if da else (b, a)
    _, sa = overall_score(g, a); _, sb = overall_score(g, b)
    if sa is not None and sb is not None and sa != sb: return (a, b) if sa < sb else (b, a)
    if set(da) != set(db): return b, a
    return None, None


def is_adjustment(g, node):
    return ((node, RDF.type, EDT.ParameterAdjustment) in g
            or (node, RDF.type, EDT.ManualParameterAdjustment) in g)


def adjustment_targets(g, adjustment):
    """Objetivos declarados del ajuste, sin inferirlos desde el parámetro cambiado."""
    return list(dict.fromkeys(g.objects(adjustment, EDT.targetsOutcome)))


def targeted_properties(g, adjustment):
    return {
        target for target in adjustment_targets(g, adjustment)
        if (target, RDF.type, EDT.ObservableProperty) in g
    }


def targeted_defects(g, adjustment):
    return {
        target for target in adjustment_targets(g, adjustment)
        if (target, RDF.type, EDT.SensoryDefect) in g
    }


def has_explicit_sensory_acceptance(g, extraction):
    return any(
        (evaluation, EDT.hasSensoryAcceptanceStatus, EDT.SensoryAccepted) in g
        for evaluation in evaluations(g, extraction)
    )


def materialize_sequences(g, deviation_index):
    handled = set()
    # Secuencias reversibles A-B-A: cualquier observación cualitativa puede ser el factor aislado.
    controls = (EDT.CoffeeDoseMass, EDT.BeverageMass, EDT.ExtractionTime, EDT.WaterTemperature,
                EDT.ExtractionPressure, EDT.ThermalStability, EDT.TDS, EDT.ExtractionYield)
    for first, _, middle in list(g.triples((None, EDT.precedes, None))):
        for last in g.objects(middle, EDT.precedes):
            if not all((x, RDF.type, EDT.EspressoExtraction) in g for x in (first, middle, last)): continue
            common = set(observation_map(g, first)) & set(observation_map(g, middle)) & set(observation_map(g, last))
            for prop in [p for p in common if decimal_value(g, observation(g, first, p)) is None
                         and p in VERIFIABLE_PROPERTIES]:
                vals = [observation_value(g, observation(g, x, prop)) for x in (first, middle, last)]
                if vals[0] != vals[2] or vals[1] == vals[0]: continue
                control_obs = [[observation(g, x, p) for x in (first, middle, last)] for p in controls]
                if any(any(o is None for o in row) or len({observation_value(g, o) for o in row}) != 1 for row in control_obs): continue
                scores = [overall_score(g, x) for x in (first, middle, last)]; score_values = [s[1] for s in scores]
                if None in score_values or score_values[0] != score_values[2] or score_values[1] >= score_values[0]: continue
                evidence = [observation(g, x, prop) for x in (first, middle, last)] + [s[0] for s in scores] + sum(control_obs, [])
                diff = add_difference(g, EDT.ReversibleContrast, [first, middle, last], evidence,
                    f"Los controles permanecieron iguales; {label(g, prop)} cambió {vals[0]}-{vals[1]}-{vals[2]} y el agrado {number(score_values[0])}-{number(score_values[1])}-{number(score_values[2])}.")
                target = defects(g, middle)[0] if defects(g, middle) else diff
                diagnosis = add_diagnosis(g, middle, target, [diff, *evidence],
                    f"El origen más probable del resultado de {local_name(middle)} es {label(g, prop)}: fue el único cambio reproducible mientras los controles permanecieron iguales.")
                add_explanation(g, diagnosis, [diff, *evidence],
                    f"En la secuencia {local_name(first)}→{local_name(middle)}→{local_name(last)}, {label(g, prop)} siguió el patrón "
                    f"«{vals[0]}→{vals[1]}→{vals[2]}» y el agrado cambió «{number(score_values[0])}→{number(score_values[1])}→{number(score_values[2])}», "
                    f"mientras {property_names(g, controls)} permanecieron iguales. La degradación del resultado cuando cambió {label(g, prop)} y su recuperación "
                    "cuando volvió al estado inicial forman un contraste reversible; por eso esa condición tiene mayor poder explicativo que una simple coincidencia temporal. "
                    "La conclusión queda limitada a este café, perfil y contexto, y no constituye una relación causal universal.")
                add_recommendation(g, middle, diagnosis, prop,
                    f"Repetir la preparación manteniendo {label(g, prop)} en «{vals[0]}» y los controles sin cambios. Si el agrado se recupera, el diagnóstico gana respaldo; si no, investigar otra variable.",
                    EDT.IterativeCalibrationProcedure)
                handled.add(middle)

    # Confirmaciones progresivas: extracción-ajuste-extracción-ajuste-extracción.
    # El patrón es general y solo concluye que el primer cambio fue insuficiente
    # cuando un cambio posterior de mayor magnitud resuelve las desviaciones bajo
    # controles observados constantes.
    for first, _, first_adjustment in list(g.triples((None, EDT.precedes, None))):
        if (first, RDF.type, EDT.EspressoExtraction) not in g or not is_adjustment(g, first_adjustment):
            continue
        changed_prop = one(g, first_adjustment, EDT.changesProperty)
        first_targets = targeted_properties(g, first_adjustment)
        first_old = decimal_value(g, first_adjustment, EDT.previousNumericValue)
        first_new = decimal_value(g, first_adjustment, EDT.newNumericValue)
        if (changed_prop not in VERIFIABLE_PROPERTIES or not first_targets
                or first_old is None or first_new is None):
            continue
        for middle in g.objects(first_adjustment, EDT.precedes):
            if (middle, RDF.type, EDT.EspressoExtraction) not in g:
                continue
            for second_adjustment in g.objects(middle, EDT.precedes):
                if (not is_adjustment(g, second_adjustment)
                        or one(g, second_adjustment, EDT.changesProperty) != changed_prop
                        or targeted_properties(g, second_adjustment) != first_targets):
                    continue
                second_old = decimal_value(g, second_adjustment, EDT.previousNumericValue)
                second_new = decimal_value(g, second_adjustment, EDT.newNumericValue)
                if (second_old is None or second_new is None or first_new != second_old
                        or abs(second_new - second_old) <= abs(first_new - first_old)):
                    continue
                for last in g.objects(second_adjustment, EDT.precedes):
                    if (last, RDF.type, EDT.EspressoExtraction) not in g:
                        continue
                    first_dev = deviations_for(deviation_index, first)
                    middle_dev = deviations_for(deviation_index, middle)
                    last_dev = deviations_for(deviation_index, last)
                    persistent = first_targets & set(first_dev) & set(middle_dev)
                    resolved = {prop for prop in persistent if prop not in last_dev and is_within_profile(g, last, prop)}
                    if not resolved:
                        continue

                    maps = [observation_map(g, extraction) for extraction in (first, middle, last)]
                    common = set(maps[0]) & set(maps[1]) & set(maps[2])
                    control_props = common - {changed_prop} - resolved
                    if any(len({observation_value(g, mapping[prop]) for mapping in maps}) != 1
                           for prop in control_props):
                        continue

                    ordered_resolved = sorted(resolved, key=str)
                    resolved_names = ", ".join(label(g, prop) for prop in ordered_resolved)
                    first_results = ", ".join(observed_value_text(g, first, prop) for prop in ordered_resolved)
                    middle_results = ", ".join(observed_value_text(g, middle, prop) for prop in ordered_resolved)
                    confirmed_results = ", ".join(observed_value_text(g, last, prop) for prop in ordered_resolved)
                    profile_constraints = ", ".join(constraint_text(g, last, prop) for prop in ordered_resolved)
                    control_names = ", ".join(label(g, prop) for prop in sorted(control_props, key=str)) or "los demás parámetros registrados"
                    evidence = [first_adjustment, second_adjustment, *first_targets]
                    evidence += [mapping[prop] for mapping in maps for prop in common]
                    evidence += [first_dev[prop] for prop in resolved] + [middle_dev[prop] for prop in resolved]
                    difference = add_difference(
                        g, EDT.ProgressiveAdjustmentContrast, [first, middle, last], evidence,
                        f"El cambio en el parámetro «{label(g, changed_prop)}», {number(first_old)}→{number(first_new)}, dejó {first_results} y {middle_results}; "
                        f"al ampliar el ajuste {number(second_old)}→{number(second_new)}, se obtuvo {confirmed_results}, dentro de {profile_constraints}. "
                        f"Permanecieron iguales {control_names}.",
                    )
                    diagnosis = add_diagnosis(
                        g, middle, difference, [difference, *evidence],
                        f"El primer cambio en el parámetro «{label(g, changed_prop)}», de {number(first_old)} a {number(first_new)}, fue insuficiente "
                        f"para este café y contexto: {first_results} permanecieron como {middle_results}, fuera del perfil. "
                        f"Al ampliar el ajuste hasta {number(second_new)}, {confirmed_results} entraron en el perfil "
                        f"mientras {control_names} permanecieron iguales.",
                    )
                    if (middle, EDT.evaluatesOutcomeOf, first_adjustment) in g:
                        mark_outcome_assessment(
                            g, diagnosis, first_adjustment, EDT.InsufficientOutcomeObserved,
                        )
                    add_explanation(
                        g, diagnosis, [difference, *evidence],
                        f"La primera modificación, {number(first_old)}→{number(first_new)}, no alteró {resolved_names}: se conservaron {middle_results}, todavía fuera del perfil. "
                        f"La segunda modificación, {number(second_old)}→{number(second_new)}, fue de mayor magnitud y coincidió con {confirmed_results}, ya conformes con {profile_constraints}; "
                        f"al mismo tiempo, {control_names} permanecieron iguales. El contraste entre una respuesta nula y una recuperación bajo controles estables indica que la magnitud "
                        "del primer cambio fue insuficiente en estas condiciones. No establece una equivalencia universal entre la magnitud del ajuste y el resultado, ni confirma por sí solo la aceptación sensorial.",
                    )
                    add_recommendation(
                        g, middle, diagnosis, changed_prop,
                        f"Repetir una extracción con «{label(g, changed_prop)}»={number(second_new)}, conservar {control_names} y realizar "
                        "la evaluación sensorial. Si el resultado técnico y sensorial se mantiene, usar ese valor como referencia contextual; "
                        "si no, continuar la calibración modificando un único parámetro.",
                    )
                    if (last, EDT.evaluatesOutcomeOf, second_adjustment) in g:
                        resolved_deviations = [middle_dev[prop] for prop in ordered_resolved]
                        add_positive_outcome_assessment(
                            g, last, second_adjustment, resolved_deviations, difference, evidence,
                            f"La extracción {local_name(last)} evaluó la modificación de {label(g, changed_prop)} "
                            f"{number(second_old)}→{number(second_new)} y registró {confirmed_results} dentro de {profile_constraints}.",
                            f"La extracción {local_name(last)} fue declarada como evaluación de la segunda modificación de {label(g, changed_prop)}. "
                            f"Después del cambio {number(second_old)}→{number(second_new)}, se observaron {confirmed_results}, conformes con {profile_constraints}, "
                            f"mientras {control_names} permanecieron iguales. Esta evidencia positiva respalda que el resultado técnico esperado fue observado en esa prueba. "
                            "La conclusión se limita a este café, perfil y contexto: no demuestra causalidad universal ni confirma por sí sola la aceptación sensorial.",
                        )
                    handled.update((first, middle, last))

    # Secuencias extracción-ajuste-extracción.
    for initial, _, adjustment in list(g.triples((None, EDT.precedes, None))):
        if not is_adjustment(g, adjustment): continue
        changed_prop = one(g, adjustment, EDT.changesProperty)
        if changed_prop not in VERIFIABLE_PROPERTIES: continue
        for posterior in g.objects(adjustment, EDT.precedes):
            if (posterior, RDF.type, EDT.EspressoExtraction) not in g: continue
            if posterior in handled: continue
            changed, equal, evidence = compare_observations(g, initial, posterior)
            sensory_changes, sensory_evidence = sensory_score_comparison(g, initial, posterior)
            evidence += sensory_evidence
            before, after = deviations_for(deviation_index, initial), deviations_for(deviation_index, posterior)
            goals = adjustment_targets(g, adjustment)
            property_goals = targeted_properties(g, adjustment)
            defect_goals = targeted_defects(g, adjustment)
            initial_defects = set(defects(g, initial))
            posterior_defects = set(defects(g, posterior))
            persistent_properties = property_goals & set(before) & set(after)
            resolved_properties = {
                prop for prop in property_goals & set(before)
                if prop not in after and is_within_profile(g, posterior, prop)
            }
            persistent_defects = defect_goals & initial_defects & posterior_defects
            accepted = has_explicit_sensory_acceptance(g, posterior)
            resolved_defects = {
                defect for defect in defect_goals & initial_defects
                if defect not in posterior_defects and accepted
            }
            new_defects = sorted(posterior_defects - initial_defects, key=str)
            designated_evaluation = (posterior, EDT.evaluatesOutcomeOf, adjustment) in g
            if not new_defects and not persistent_properties and not persistent_defects and not designated_evaluation:
                continue
            evidence += [
                adjustment, *goals, *before.values(), *after.values(),
                *evaluations(g, initial), *evaluations(g, posterior),
                *initial_defects, *posterior_defects,
            ]
            sensory_difference_clause = (
                f" Sensorialmente cambiaron {sensory_changes}." if sensory_changes != "ninguno" else ""
            )
            diff = add_difference(g, EDT.SequentialContrast, [initial, posterior], evidence,
                f"Se modificó {label(g, changed_prop)}; cambiaron {', '.join(label(g,p) for p in changed) or 'los resultados'} y permanecieron iguales {', '.join(label(g,p) for p in equal) or 'los controles'}.{sensory_difference_clause}")
            if new_defects:
                target = new_defects[0]; previous = decimal_value(g, adjustment, EDT.previousNumericValue); new = decimal_value(g, adjustment, EDT.newNumericValue)
                values = f" de {number(previous)} a {number(new)}" if previous is not None and new is not None else ""
                diagnosis_sensory_clause = (
                    f" La comparación sensorial registró {sensory_changes}."
                    if sensory_changes != "ninguno" else ""
                )
                diagnosis = add_diagnosis(g, posterior, target, [diff, adjustment, *after.values(), *sensory_evidence],
                    f"El origen más probable de {label(g,target)} en {local_name(posterior)} es el cambio de {label(g,changed_prop)}{values}, porque fue el ajuste registrado entre ambas pruebas.{diagnosis_sensory_clause}")
                if designated_evaluation:
                    mark_outcome_assessment(g, diagnosis, adjustment, EDT.AdverseOutcomeObserved)
                changed_values = comparison_values(g, initial, posterior, changed)
                equal_names = property_names(g, equal)
                previous_defects = defect_names(g, initial)
                deviation_clause = (f" En la segunda extracción también se registró: {deviation_details(g, after)}."
                                    if after else " Ninguna variable observada quedó fuera del perfil en la segunda extracción.")
                crossed_profile = (changed_prop in after and is_within_profile(g, initial, changed_prop))
                adjustment_unit = one(g, adjustment, EDT.hasUnit)
                if adjustment_unit is None:
                    posterior_observation = observation(g, posterior, changed_prop)
                    adjustment_unit = one(g, posterior_observation, EDT.hasUnit) if posterior_observation else None
                transition_unit = unit_text(g, adjustment_unit)
                transition_suffix = f" {transition_unit}" if transition_unit else ""
                transition_clause = (f" El parámetro pasó de un valor conforme a uno fuera del perfil; por tanto, la transición del resultado quedó acotada "
                                     f"entre {number(previous)}{transition_suffix} y {number(new)}{transition_suffix}, aunque esta comparación no localiza el umbral exacto."
                                     if crossed_profile and previous is not None and new is not None else "")
                sensory_explanation_clause = (
                    f" Las puntuaciones comparables mostraron {sensory_changes}."
                    if sensory_changes != "ninguno" else ""
                )
                add_explanation(g, diagnosis, [diff, adjustment, target, *after.values(), *sensory_evidence],
                    f"Entre {local_name(initial)} y {local_name(posterior)} cambió {changed_values}; {equal_names} conservaron el mismo valor. "
                    f"La primera evaluación registró {previous_defects} y la evaluación posterior registró {label(g,target)} después del cambio de {label(g,changed_prop)}{values}."
                    f"{sensory_explanation_clause}{deviation_clause}{transition_clause} Al ser el único ajuste registrado y aparecer el nuevo defecto mientras los controles observados permanecían estables, "
                    f"{label(g,changed_prop)} adquiere prioridad como explicación probable para este café y contexto. La secuencia no demuestra causalidad universal ni excluye variables no registradas.")
                add_recommendation(g, posterior, diagnosis, changed_prop,
                    f"Volver al valor anterior de {label(g,changed_prop)} y repetir; después probar un cambio menor manteniendo los demás parámetros constantes. Si desaparece el defecto, el diagnóstico gana respaldo.")
            elif persistent_properties or persistent_defects:
                target = next(iter(sorted(persistent_defects, key=str)), diff)
                goal_names = property_names(g, [*persistent_properties, *persistent_defects])
                diagnosis = add_diagnosis(
                    g, posterior, target, [diff, adjustment, *goals, *after.values()],
                    f"El ajuste sobre {label(g, changed_prop)} fue insuficiente para corregir {goal_names}: el resultado objetivo continuó presente después de la intervención.",
                )
                if designated_evaluation:
                    mark_outcome_assessment(g, diagnosis, adjustment, EDT.InsufficientOutcomeObserved)
                technical_before = human_list(observed_value_text(g, initial, prop) for prop in sorted(persistent_properties, key=str))
                technical_after = human_list(observed_value_text(g, posterior, prop) for prop in sorted(persistent_properties, key=str))
                sensory_text = property_names(g, persistent_defects) if persistent_defects else "ningún defecto sensorial objetivo persistente"
                add_explanation(
                    g, diagnosis, [diff, adjustment, *goals, *after.values()],
                    f"El ajuste declaró como objetivo corregir {property_names(g, goals)}. Antes se registraron {technical_before} y, después, {technical_after}; "
                    f"en la dimensión sensorial permaneció {sensory_text}. {property_names(g, equal)} conservaron el mismo valor como controles observados. "
                    "Por tanto, la intervención fue ejecutada, pero la evidencia posterior no muestra que desplazara el resultado objetivo hacia el estado esperado. "
                    "La conclusión queda limitada a esta secuencia: no determina si la insuficiencia se debió a la magnitud, a la dirección o a una variable no registrada.",
                )
                add_recommendation(
                    g, posterior, diagnosis, changed_prop,
                    f"Realizar otra prueba modificando solo {label(g, changed_prop)} y volver a evaluar {goal_names}; mantener los demás parámetros constantes.",
                )
            elif (goals and property_goals == resolved_properties
                  and defect_goals == resolved_defects):
                resolved_nodes = [before[prop] for prop in sorted(resolved_properties, key=str)]
                resolved_nodes.extend(sorted(resolved_defects, key=str))
                posterior_values = comparison_values(g, initial, posterior, resolved_properties)
                add_positive_outcome_assessment(
                    g, posterior, adjustment, resolved_nodes, diff, evidence,
                    f"La extracción {local_name(posterior)} aportó evidencia positiva de que el ajuste alcanzó su objetivo: {property_names(g, goals)}.",
                    f"El ajuste declaró como objetivo corregir {property_names(g, goals)}. La comparación posterior registró {posterior_values}; "
                    f"las propiedades técnicas objetivo quedaron dentro del perfil y los objetivos sensoriales fueron respaldados por una aceptación explícita del barista cuando correspondía. "
                    "Por ello, existe evidencia positiva del resultado esperado en esta secuencia. La conclusión se limita al café, perfil y contexto registrados: no demuestra causalidad universal ni predice otras extracciones.",
                )
            else:
                diagnosis = add_diagnosis(
                    g, posterior, diff, [diff, adjustment, *goals, *evidence],
                    f"El resultado del ajuste sobre {label(g, changed_prop)} quedó inconcluso: no existe evidencia positiva suficiente para decidir si se corrigieron {property_names(g, goals) if goals else 'los objetivos no declarados'}.",
                )
                if designated_evaluation:
                    mark_outcome_assessment(g, diagnosis, adjustment, EDT.InconclusiveOutcome)
                add_explanation(
                    g, diagnosis, [diff, adjustment, *goals, *evidence],
                    f"La secuencia registra una modificación de {label(g, changed_prop)} y declara como objetivo {property_names(g, goals) if goals else 'ningún resultado explícito'}, pero la evidencia posterior no confirma todos los objetivos. "
                    "La desaparición de un defecto por ausencia de registro no equivale a una aceptación sensorial, y una propiedad sin observaciones comparables tampoco demuestra corrección. "
                    "Por tanto, el resultado queda inconcluso con los datos disponibles: no demuestra eficacia, ineficacia ni causalidad y no permite atribuir al ajuste los cambios no medidos.",
                )
            handled.add(posterior)
    return handled


def materialize_calibration_conflicts(g):
    handled = set()
    for calibration in g.subjects(RDF.type, EDT.EspressoCalibration):
        if (calibration, EDT.hasValidationStatus, EDT.Rejected) not in g: continue
        for extraction in g.objects(calibration, EDT.hasTrialExtraction):
            ds = defects(g, extraction); evidence = [calibration, extraction, *evaluations(g, extraction), *ds]
            checked = []
            for obs in observations(g, extraction):
                prop = one(g, obs, EDT.observedProperty); spec = specification(g, extraction, prop, one(g, obs, EDT.hasUnit))
                if spec: checked.append(prop); evidence += [obs, spec]
            score, _ = overall_score(g, extraction); evidence.append(score)
            if not ds or not checked or not all(is_within_profile(g, extraction, p) for p in checked): continue
            target = ds[0]; diagnosis = add_diagnosis(g, calibration, target, evidence,
                f"La calibración fue rechazada por {label(g,target)}, no por incumplimiento técnico: los parámetros registrados permanecieron dentro del perfil. "
                "Con las variables que mide la máquina no se aísla una causa, de modo que el razonador no formula una recomendación.")
            conforming_values = human_list(observed_value_text(g, extraction, prop) for prop in sorted(set(checked), key=str))
            score_text = f" y el agrado global fue {number(overall_score(g, extraction)[1])}/9" if overall_score(g, extraction)[1] is not None else ""
            add_explanation(g, diagnosis, evidence,
                f"La extracción registró {conforming_values}, todos dentro de las especificaciones disponibles, pero la evaluación sensorial identificó {label(g,target)}{score_text}. "
                "Esto separa dos dimensiones que no son equivalentes: cumplir los límites técnicos describe conformidad con el perfil, mientras que aceptar la bebida depende del resultado sensorial. "
                "Por tanto, el rechazo no se explica mediante una desviación numérica registrada; muestra que el perfil actual no predice completamente la aceptabilidad de este café y contexto. "
                "Con esta evidencia no puede aislarse una causa técnica única.")
            withhold_recommendation(g, extraction, diagnosis)
            handled.add(extraction)
    return handled


def deviation_direction(g, node):
    code = str(one(g, node, EDT.statusCode) or "")
    return "above" if code.startswith("above") else "below" if code.startswith("below") else None


def intensity(g, extraction, attribute):
    item = sensory_score_map(g, extraction).get((attribute, EDT.PerceivedIntensity))
    return (item[0], item[1]) if item else (None, None)


def estimated_water(g, extraction):
    return next((o for o in observations(g, extraction, EDT.WaterTemperature)
                 if (o, RDF.type, EDT.EstimatedObservation) in g), None)


def toward_recipe(direction):
    """Hacia la receta: lo que quedó por encima se baja y lo que quedó por debajo se sube."""
    return EDT.Decrease if direction == "above" else EDT.Increase if direction == "below" else EDT.Maintain


def guidance_conclusion(g, extraction, target, heuristic, evidence, conclusion, explanation,
                        prop, recommendation, direction, procedure=EDT.IterativeCalibrationProcedure):
    """Diagnóstico, explicación y recomendación de una regla con fuente (EKO 3.4.0)."""
    diagnosis = add_diagnosis(g, extraction, target, evidence, conclusion)
    g.add((diagnosis, EDT.supportedByHeuristic, heuristic))
    add_explanation(g, diagnosis, evidence, explanation)
    action = add_recommendation(g, extraction, diagnosis, prop, recommendation, procedure, heuristic)
    g.set((action, EDT.recommendedDirection, direction))
    return diagnosis


def materialize_guidance_rules(g, deviation_index, handled):
    """Reglas de la literatura y de los baristas sobre una sola extracción.

    Cada regla cita su heurística y su fuente, y concluye un diagnóstico
    probable, nunca una causa. «Fuera de la receta» es una desviación frente al
    perfil, cuya tolerancia fija el barista con la dispersión de sus
    repeticiones. El orden importa: una regla más específica deja fuera a la
    general (cortado antes frente a la masa primero). No toca las extracciones
    que ya explicó una secuencia o un caso de calibración.
    """
    added = set()
    for extraction in list(g.subjects(RDF.type, EDT.EspressoExtraction)):
        if extraction in handled:
            continue
        dev = deviations_for(deviation_index, extraction)
        direction = {prop: deviation_direction(g, node) for prop, node in dev.items()}
        found = defects(g, extraction)
        name = local_name(extraction)
        matched = False

        # Cortado antes: tiempo y masa por debajo de la receta.
        if direction.get(EDT.ExtractionTime) == "below" and direction.get(EDT.BeverageMass) == "below":
            evidence = [dev[EDT.ExtractionTime], dev[EDT.BeverageMass]]
            guidance_conclusion(
                g, extraction, dev[EDT.ExtractionTime], EDT.ShortShotHeuristic, evidence,
                f"{name} se cortó antes que la receta: {deviation_details(g, {p: dev[p] for p in (EDT.ExtractionTime, EDT.BeverageMass)})}. "
                "La bebida resultante es probablemente más concentrada y con menos rendimiento por el tiempo de contacto.",
                f"En {name} el tiempo de extracción y la masa de bebida quedaron por debajo de la receta. "
                "Según la heurística del café cortado antes, las primeras gotas son las más concentradas y el rendimiento sigue al tiempo de contacto; "
                "por ello la diferencia con la receta se explica por el tiempo de contacto. "
                "La conclusión es probable: no demuestra la concentración ni el rendimiento si no se registraron TDS y dosis.",
                EDT.ExtractionTime,
                "Repetir con el tiempo de extracción de la receta y comparar el TDS y el rendimiento.",
                EDT.Increase,
            )
            matched = True
        elif EDT.BeverageMass in dev:
            # Primero la masa: la proporción explica antes que cualquier otra variable.
            guidance_conclusion(
                g, extraction, dev[EDT.BeverageMass], EDT.BeverageMassFirstHeuristic, [dev[EDT.BeverageMass], *evaluations(g, extraction)],
                f"{name} tuvo la masa de bebida fuera de la receta: {deviation_details(g, {EDT.BeverageMass: dev[EDT.BeverageMass]})}. "
                "La proporción es la primera explicación probable de su TDS y de su sabor.",
                f"La masa de bebida de {name} quedó fuera de la receta. La relación de preparación cambia el rendimiento y la composición de la bebida "
                "más que el caudal, la molienda o la temperatura; por ello cualquier diferencia de TDS o de sabor se atribuye primero a la proporción. "
                "La conclusión está limitada a esta extracción y no excluye que otra variable también cambiara.",
                EDT.BeverageMass,
                "Repetir con la masa de bebida de la receta antes de cambiar otra variable.",
                toward_recipe(direction[EDT.BeverageMass]),
            )
            matched = True

        # Dosis y concentración: dosis y TDS en el mismo sentido, con la masa en la receta.
        dose_direction = direction.get(EDT.CoffeeDoseMass)
        if (dose_direction and direction.get(EDT.TDS) == dose_direction
                and EDT.BeverageMass not in dev and is_within_profile(g, extraction, EDT.BeverageMass)):
            evidence = [dev[EDT.CoffeeDoseMass], dev[EDT.TDS], observation(g, extraction, EDT.BeverageMass)]
            strength = "más débil" if dose_direction == "below" else "más fuerte"
            guidance_conclusion(
                g, extraction, dev[EDT.TDS], EDT.DoseConcentrationHeuristic, evidence,
                f"{name} salió {strength} que la receta por la dosis: {deviation_details(g, {p: dev[p] for p in (EDT.CoffeeDoseMass, EDT.TDS)})}, con la masa de bebida dentro de la receta.",
                f"En {name} la dosis y el TDS se apartaron de la receta en el mismo sentido mientras la masa de bebida se mantuvo dentro de ella. "
                "Con la misma cantidad de agua, más o menos café cambia los sólidos disueltos sin cambiar mucho el rendimiento; por ello la bebida es "
                f"{strength} por la dosis y no por una extracción distinta. La conclusión no demuestra el mecanismo y queda limitada a esta extracción.",
                EDT.CoffeeDoseMass,
                "Repetir con la dosis de la receta y comparar el TDS.",
                toward_recipe(dose_direction),
            )
            matched = True

        # Dosis alta y sabor: amargor mayor que 7,5 sobre 10, acidez excesiva o quemado.
        _, bitterness = intensity(g, extraction, EDT.Bitterness)
        signal = [d for d in found if d in (EDT.ExcessiveAcidity, EDT.Burnt)]
        if dose_direction == "above" and ((bitterness is not None and bitterness > Decimal("7.5")) or signal):
            target = dev[EDT.CoffeeDoseMass]
            taste = (f"amargor {number(bitterness)}/10" if bitterness is not None and bitterness > Decimal("7.5")
                     else label(g, signal[0]))
            guidance_conclusion(
                g, extraction, target, EDT.HighDoseTasteHeuristic, [dev[EDT.CoffeeDoseMass], *evaluations(g, extraction)],
                f"{name} tuvo la dosis por encima de la receta y {taste}: el exceso de café es el diagnóstico probable.",
                f"La dosis de {name} quedó por encima de la receta y la catación registró {taste}. "
                "En espresso, más café por taza aumentó los sólidos y, a partir de un amargor de 7,5 sobre 10, la dosis se descartó; "
                "por ello el sabor registrado indica un exceso de café. La conclusión no demuestra la causa y depende del café utilizado.",
                EDT.CoffeeDoseMass,
                "Bajar la dosis a la de la receta y catar de nuevo.",
                EDT.Decrease,
            )
            matched = True

        # Agua fría y acidez: el agua estimada al menos 2 °C bajo el objetivo.
        water = estimated_water(g, extraction)
        water_spec = specification(g, extraction, EDT.WaterTemperature)
        target_c = decimal_value(g, water_spec, EDT.targetValue) if water_spec is not None else None
        value_c = decimal_value(g, water) if water is not None else None
        if (EDT.ExcessiveAcidity in found and value_c is not None and target_c is not None
                and value_c <= target_c - Decimal("2")):
            guidance_conclusion(
                g, extraction, EDT.ExcessiveAcidity, EDT.ColdWaterAcidityHeuristic, [water, water_spec, *evaluations(g, extraction)],
                f"En {name} el agua estimada sobre el café fue {number(value_c)} °C, {number(target_c - value_c)} °C por debajo del objetivo, "
                "y la catación registró acidez excesiva: la temperatura baja es el diagnóstico probable.",
                f"El agua sobre el café de {name} se estimó en {number(value_c)} °C frente a un objetivo de {number(target_c)} °C, y la catación registró acidez excesiva. "
                "Según el barista, dos grados de diferencia se sienten y el agua más fría aumenta la acidez; por ello la acidez indica una temperatura baja. "
                "La temperatura es una estimación del gemelo y no una medida, de modo que la conclusión no demuestra la causa.",
                EDT.WaterTemperature,
                "Compensar o subir la temperatura del agua y catar de nuevo; en el primer café tras un reposo, comprobar que el sostenimiento no estaba suspendido.",
                EDT.Increase,
            )
            matched = True

        # Café amargo y largo: tiempo por encima de la receta.
        if EDT.ExcessiveBitterness in found and direction.get(EDT.ExtractionTime) == "above":
            guidance_conclusion(
                g, extraction, EDT.ExcessiveBitterness, EDT.LongBitterShotHeuristic, [dev[EDT.ExtractionTime], *evaluations(g, extraction)],
                f"{name} salió amargo y más largo que la receta: {deviation_details(g, {EDT.ExtractionTime: dev[EDT.ExtractionTime]})}.",
                f"La catación de {name} registró amargor excesivo y su tiempo de extracción quedó por encima de la receta. "
                "El criterio del barista da prioridad al tiempo sobre la temperatura; por ello el tiempo concentra la explicación disponible. "
                "La conclusión es probable y no excluye que la temperatura también contribuyera.",
                EDT.ExtractionTime,
                "Acortar primero el tiempo de extracción; si no basta, bajar un grado la temperatura.",
                EDT.Decrease,
            )
            matched = True

        if matched:
            added.add(extraction)
    return added


def within_dispersion(g, reference, affected, properties):
    """Si cada propiedad cambió menos que la tolerancia de la receta (EKO 3.4.0)."""
    for prop in properties:
        a, b = observation(g, reference, prop), observation(g, affected, prop)
        va, vb = (decimal_value(g, a) if a else None), (decimal_value(g, b) if b else None)
        spec = specification(g, affected, prop, one(g, b, EDT.hasUnit)) if b else None
        if va is None or vb is None or spec is None:
            return False
        minimum, maximum = limits(g, spec)
        tolerance = decimal_value(g, spec, EDT.toleranceValue)
        if tolerance is None and minimum is not None and maximum is not None:
            tolerance = (maximum - minimum) / 2
        if tolerance is None or abs(va - vb) >= tolerance:
            return False
    return True


def materialize_contrasts(g, deviation_index, handled):
    extractions = list(g.subjects(RDF.type, EDT.EspressoExtraction)); seen = set(); added = set()
    for a in extractions:
        for b in extractions:
            pair = frozenset((a,b))
            if pair in seen or not comparable(g,a,b): continue
            seen.add(pair); affected, reference = choose_affected(g,a,b)
            if affected is None or affected in handled or affected in added: continue
            changed, equal, evidence = compare_observations(g, reference, affected)
            evidence += [overall_score(g,reference)[0], overall_score(g,affected)[0], *evaluations(g,reference), *evaluations(g,affected), *defects(g,reference), *defects(g,affected)]
            contexts = one(g,reference,EDT.occurredUnder), one(g,affected,EDT.occurredUnder)
            if contexts[0] != contexts[1]:
                evidence += list(contexts)
                for context in contexts:
                    if context: evidence += list(g.subjects(EDT.featureOfInterest, context))
            affected_dev, reference_dev = deviations_for(deviation_index,affected), deviations_for(deviation_index,reference)
            discriminating = {p:d for p,d in affected_dev.items()
                              if p not in reference_dev and p in VERIFIABLE_PROPERTIES}
            diff = add_difference(g, EDT.ContrastiveComparison, [reference,affected], evidence,
                f"Compartieron perfil y café. Cambiaron {', '.join(label(g,p) for p in changed) or 'el resultado sensorial'}; permanecieron iguales {', '.join(label(g,p) for p in equal) or 'los controles'}.")
            target = defects(g,affected)[0] if defects(g,affected) else diff
            if discriminating:
                props=set(discriminating); issue=phenomenon(g,props)
                diagnosis=add_diagnosis(g,affected,target,[diff,*discriminating.values()],
                    f"{local_name(affected)} falló porque no reprodujo {issue} del perfil. La evidencia es: {deviation_details(g, discriminating)}. "
                    f"Esas desviaciones solo aparecieron en la extracción afectada; el diagnóstico más probable es un fallo en {issue}.")
                changed_details = comparison_values(g, reference, affected, changed)
                if (target, RDF.type, EDT.SensoryDefect) in g:
                    effect_text = label(g, target)
                else:
                    _, reference_score = overall_score(g, reference); _, affected_score = overall_score(g, affected)
                    effect_text = (f"un agrado global de {number(affected_score)}/9 frente a {number(reference_score)}/9 en la referencia"
                                   if reference_score is not None and affected_score is not None else "un resultado peor que la referencia")
                concentration_verb = "concentra" if issue.startswith(("el ", "la ")) else "concentran"
                add_explanation(g,diagnosis,[diff,target,*discriminating.values()],
                    f"{local_name(reference)} y {local_name(affected)} compartieron perfil y café. Entre ambas cambiaron {changed_details}, mientras "
                    f"{property_names(g, equal)} permanecieron iguales. La referencia no presentó las desviaciones discriminantes y obtuvo el mejor resultado; en la extracción afectada aparecieron "
                    f"{deviation_details(g, discriminating)} junto con {effect_text}. Como esas no conformidades están ausentes en la referencia comparable y los controles registrados no cambiaron, "
                    f"{issue} {concentration_verb} la evidencia explicativa disponible. El contraste respalda un origen probable para esta ejecución, pero no convierte la asociación en una causa universal.")
                recommend_verification(g,affected,diagnosis,props,target)
            elif changed and contexts[0] == contexts[1] and within_dispersion(g, reference, affected, changed):
                diagnosis=add_diagnosis(g,affected,target,[diff],
                    f"La diferencia entre {local_name(reference)} y {local_name(affected)} cabe en la dispersión de la receta: "
                    f"{comparison_values(g, reference, affected, changed)}. No se atribuye a ninguna variable.")
                g.add((diagnosis, EDT.supportedByHeuristic, EDT.WithinDispersionHeuristic))
                add_explanation(g,diagnosis,[diff,target],
                    f"{local_name(reference)} y {local_name(affected)} compartieron perfil, café y contexto, y cada valor que cambió entre ellas lo hizo menos que la tolerancia de la receta, "
                    "que el barista fija con la dispersión de sus repeticiones. Una diferencia de ese tamaño aparece también entre preparaciones iguales; "
                    "por ello no indica un cambio de ninguna variable. La conclusión no demuestra que no exista una causa: solo que estos datos no permiten distinguirla del ruido.")
                kept = preferred_property(set(changed))
                if kept is None:
                    withhold_recommendation(g, affected, diagnosis)
                else:
                    action=add_recommendation(g,affected,diagnosis,kept,
                        "Repetir la preparación sin cambios antes de ajustar; si la diferencia se repite y sale de la dispersión, analizarla de nuevo.",
                        EDT.IterativeCalibrationProcedure, EDT.WithinDispersionHeuristic)
                    g.set((action, EDT.recommendedDirection, EDT.Maintain))
            else:
                # Solo las variables comprobables pueden ser el origen: la
                # molienda, la cama o el contexto se describen, no se diagnostican.
                props=verifiable(changed); context_changed=contexts[0]!=contexts[1]
                if props:
                    issue=", ".join(label(g,p) for p in sorted(props, key=str))
                    diagnosis=add_diagnosis(g,affected,target,[diff],f"El origen más probable del resultado de {local_name(affected)} es {issue}, porque distingue esa extracción de la referencia comparable.")
                else:
                    issue="ninguna variable que la máquina compruebe"
                    diagnosis=add_diagnosis(g,affected,target,[diff],
                        f"El resultado de {local_name(affected)} difiere de la referencia comparable, pero ninguna variable que la máquina compruebe distingue ambas extracciones; "
                        "el razonador no atribuye un origen ni formula una recomendación.")
                changed_details = comparison_values(g, reference, affected, changed)
                context_clause = (f" El contexto pasó de [{context_values(g, contexts[0])}] a [{context_values(g, contexts[1])}]."
                                  if context_changed else " Ambas extracciones ocurrieron bajo el mismo contexto registrado.")
                add_explanation(g,diagnosis,[diff,target],
                    f"Las extracciones compartieron perfil y café; cambiaron {changed_details}, mientras {property_names(g, equal)} permanecieron iguales."
                    f"{context_clause} Ninguna desviación nueva frente al perfil distingue por sí sola la extracción afectada, de modo que el resultado no puede interpretarse como un simple incumplimiento numérico. "
                    f"La covariación sitúa {issue} como el conjunto de factores con mayor poder explicativo en la comparación, pero si varios factores cambiaron simultáneamente la evidencia no permite aislar uno solo. "
                    "La conclusión es contextual y conserva explícitamente esa incertidumbre.")
                prop=preferred_property(props)
                if prop is None:
                    withhold_recommendation(g, affected, diagnosis)
                else:
                    add_recommendation(g,affected,diagnosis,prop,
                        f"Repetir modificando únicamente {label(g,prop)} y conservar los demás factores. Si mejora el resultado, el diagnóstico gana respaldo; si no, registrar nuevas variables.",
                        EDT.IterativeCalibrationProcedure)
            added.add(affected)
    return added


def materialize_fallbacks(g, deviation_index, handled):
    for extraction in g.subjects(RDF.type, EDT.EspressoExtraction):
        ds=defects(g,extraction); dev=deviations_for(deviation_index,extraction)
        # Toda extracción con un defecto o una desviación dispone de una
        # respuesta diagnóstica, aunque no forme parte de un caso comparativo.
        if extraction in handled or (not ds and not dev): continue
        target=ds[0] if ds else next(iter(dev.values()))
        evaluation=evaluation_with_defect(g,extraction,target) if ds else None
        if dev:
            props=set(dev); issue=phenomenon(g,props)
            checked, conforming = profile_checked(g, extraction)
            conforming_clause = (f" Las demás variables comprobadas —{property_names(g, set(conforming) - props)}— permanecieron dentro del perfil."
                                if set(conforming) - props else " No se registraron otros controles conformes con especificación comparable.")
            anomaly_text = ("la única anomalía técnica registrada" if len(props) == 1
                            else "el grupo coherente de anomalías técnicas registrado")
            diagnosis=add_diagnosis(g,extraction,target,[evaluation,*dev.values()],
                f"{local_name(extraction)} no reprodujo {issue} del perfil. La evidencia es: {deviation_details(g, dev)}."
                + (f" El incumplimiento coincidió con {label(g,target)}." if ds else ""))
            add_explanation(g,diagnosis,[evaluation,target,*dev.values()],
                f"En {local_name(extraction)} se observó {label(g,target) if ds else 'un resultado técnico no conforme'} y la comparación con el perfil localizó "
                f"{deviation_details(g, dev)}.{conforming_clause} Por ello, la comparación identifica {anomaly_text} en {issue}; esa anomalía coincide con el resultado. "
                "Esto explica en qué parte de la ejecución dejó de reproducirse el perfil y prioriza esa evidencia frente a los controles conformes. "
                "Al no existir una extracción de referencia ni una secuencia controlada en estos datos, la explicación es probable: no demuestra el mecanismo físico ni excluye variables no observadas.")
            recommend_verification(g,extraction,diagnosis,props,target)
        else:
            checked, conforming = profile_checked(g, extraction)
            conforming_values = human_list(observed_value_text(g, extraction, prop) for prop in sorted(conforming, key=str))
            coverage = property_names(g, checked)
            diagnosis=add_diagnosis(g,extraction,target,[evaluation,target],
                f"Se confirmó {label(g,target)} en {local_name(extraction)}, pero ninguna observación quedó fuera del perfil; el origen no puede aislarse con las variables que mide la máquina, "
                "de modo que el razonador no formula una recomendación.")
            add_explanation(g,diagnosis,[evaluation,target],
                f"La evaluación sensorial registró {label(g,target)}, mientras el subconjunto técnico comparable con el perfil —{coverage}— permaneció conforme: {conforming_values}. "
                "Esto descarta, con los datos disponibles, una explicación basada únicamente en superar los límites declarados; también demuestra que conformidad técnica y aceptabilidad sensorial no son equivalentes. "
                "Como no existe una referencia comparable ni una variable discriminante fuera del perfil, ninguna causa técnica registrada tiene prioridad sobre las demás. "
                "La explicación queda limitada a reconocer una insuficiencia predictiva del perfil o la posible intervención de variables no registradas, sin afirmar cuál de ellas produjo el defecto.")
            withhold_recommendation(g, extraction, diagnosis)


def materialize(g):
    before=len(g); deviations=materialize_profile_deviations(g); handled=set()
    materialize_technical_conformance(g,deviations)
    handled |= materialize_sequences(g,deviations)
    handled |= materialize_calibration_conflicts(g)
    handled |= materialize_guidance_rules(g,deviations,handled)
    handled |= materialize_contrasts(g,deviations,handled)
    materialize_fallbacks(g,deviations,handled)
    return len(g)-before


def main():
    root=Path(__file__).resolve().parents[1]; parser=ArgumentParser(description=__doc__)
    parser.add_argument("--core",type=Path,default=root/"owl"/"espresso-dt.ttl")
    parser.add_argument("--data",type=Path,default=root/"owl"/"espresso-domain-validation.ttl")
    parser.add_argument("--output",type=Path); args=parser.parse_args(); graph=Graph()
    graph.parse(args.core,format="turtle"); graph.parse(args.data,format="turtle"); added=materialize(graph)
    print(f"Materializados {added} triples nuevos; grafo resultante: {len(graph)} triples.")
    if args.output: graph.serialize(args.output,format="turtle"); print(f"Resultado guardado en {args.output}")


if __name__ == "__main__": main()
