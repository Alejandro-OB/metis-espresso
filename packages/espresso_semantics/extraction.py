from __future__ import annotations

import hashlib
import json
from decimal import Decimal
from pathlib import Path
from typing import Any
from urllib.parse import quote

from pyshacl import validate
from rdflib import RDF, XSD, Graph, Literal, Namespace, URIRef
from rdflib.namespace import DCTERMS, RDFS


class SemanticProjectionError(ValueError):
    """La extracción no pudo proyectarse; `report` lleva el informe SHACL si lo hubo.

    El informe viaja con el error porque es lo único que dice qué forma falló:
    sin él, quien registra el rechazo solo puede decir que hubo uno.
    """

    def __init__(self, message: str, report: str | None = None) -> None:
        super().__init__(message)
        self.report = report


EDT = Namespace("https://w3id.org/espresso-dt#")
RESOURCE = "https://w3id.org/espresso-dt/resource/"
ROOT = Path(__file__).resolve().parents[2]
SHAPES = ROOT / "knowledge/ontology/ontology/espresso-dt-shapes.ttl"


#: Causa de finalización del contrato del dispositivo y su individuo en EKO. Una
#: terminación que no figure aquí no se proyecta: afirmar una causa que la
#: ontología no define sería inventarla.
TERMINATION_CAUSES = {
    "maximum-extraction-time-reached": EDT.MaximumExtractionTimeReached,
    "manual-normal-stop": EDT.ManualNormalStop,
    "safety-protection-trip": EDT.SafetyProtectionTrip,
    "unexpected-power-loss": EDT.UnexpectedPowerLoss,
}


#: Propiedad del perfil y su término en EKO. Una propiedad que no figure aquí
#: se publica con un IRI propio, como antes: el razonador no la reconoce como
#: una de las suyas y no la juzga, pero tampoco se pierde.
PROPERTIES = {
    "water-temperature": EDT.WaterTemperature,
    "temperature": EDT.WaterTemperature,
    # Lo que mide la placa (EKO 3.4.0). Antes se publicaba como agua, y el
    # razonador comparaba el metal con el objetivo del agua.
    "boiler-temperature": EDT.BoilerBodyTemperature,
    "pressure": EDT.ExtractionPressure,
    "extraction-pressure": EDT.ExtractionPressure,
    "coffee-dose": EDT.CoffeeDoseMass,
    "beverage-mass": EDT.BeverageMass,
    "extraction-time": EDT.ExtractionTime,
    "brew-ratio": EDT.BrewRatio,
    "grind-setting": EDT.GrindSetting,
    "preinfusion-time": EDT.PreinfusionDuration,
    "ambient-temperature": EDT.AmbientTemperature,
    "relative-humidity": EDT.RelativeHumidity,
    "tds": EDT.TDS,
    "extraction-yield": EDT.ExtractionYield,
    "thermal-stability": EDT.ThermalStability,
    "coffee-bed-condition": EDT.CoffeeBedCondition,
}

#: Unidad declarada por la plataforma y su individuo en EKO. El razonador solo
#: compara una observación con una especificación de la misma unidad: si cada
#: lado usara su propio IRI, ninguna especificación quedaría cubierta.
UNITS = {
    "°C": EDT.DegreeCelsius,
    "bar": EDT.Bar,
    "g": EDT.Gram,
    "s": EDT.Second,
    "%": EDT.Percent,
    "°Bx": EDT.DegreeBrix,
    ":1": EDT.Dimensionless,
    "m": EDT.Meter,
}


def property_iri(name: str) -> URIRef:
    return PROPERTIES.get(name, stable_iri("property", name))


def unit_iri(unit: str | None) -> URIRef:
    return UNITS.get(str(unit), stable_iri("unit", str(unit or "unknown")))


def has_series(extraction: dict[str, Any]) -> bool:
    """Si la preparación tiene una serie que referenciar.

    Sin muestras no hay serie, aunque el registro traiga una referencia. Con
    muestras sí la hay aunque no traiga `seriesRef`: las que sincroniza la
    placa no lo llevan, y antes quedaban sin serie en el grafo y, con ello, sin
    nada que juzgar de su temperatura y su presión.
    """
    if extraction.get("sampleCount") == 0:
        return False
    return bool(extraction.get("seriesRef") or extraction.get("sampleCount"))


def stable_iri(kind: str, identifier: str) -> URIRef:
    return URIRef(f"{RESOURCE}{quote(kind, safe='')}/{quote(identifier, safe='')}")


def specification_iri(profile_id: str, specification: dict[str, Any]) -> URIRef:
    """El nombre de una especificación sale de su contenido, no de su posición.

    Antes era `perfil-{posición}`. La lista que se publica no es siempre la
    misma para un perfil: un café real guarda solo el resumen que declara la
    placa (temperatura y presión) y la plataforma le añade la de caldera que
    sale del agua, mientras que el perfil completo trae tiempo, dosis y
    relación. En la catación del 2026-11-04 `default-profile-v4-2` era la
    caldera en un café y el tiempo en otro; al juntar los grafos quedó con
    objetivo y rango a la vez, y SHACL rechazó las recomendaciones
    (evidencias/catacion-20261104/preguntas-competencia.md). Tampoco basta la
    propiedad: la de caldera lleva la subida del primer uso, que cambia de un
    café a otro. Con el contenido, dos especificaciones iguales comparten
    nombre y dos distintas no lo comparten nunca.
    """
    # La propiedad y la unidad entran como el término de EKO, no como el texto
    # con que llegan: el perfil completo dice «extraction-pressure» y el resumen
    # que guarda un café real, «pressure», y son la misma especificación. Con el
    # texto quedaban con dos nombres, y las desviaciones de la conformidad
    # apuntaban a una que no estaba publicada (PC04, 2026-11-05). Los valores
    # pasan por Decimal para que 8.5 y 8.50 no den nombres distintos.
    term = property_iri(str(specification.get("property")))
    content = {
        "property": str(term),
        "unit": str(unit_iri(specification.get("unit"))),
        "strategy": specification.get("strategy"),
        "method": specification.get("method"),
        **{
            key: None if specification.get(key) is None
            else str(Decimal(str(specification[key])).normalize())
            for key in ("target", "tolerance", "minimum", "maximum")
        },
    }
    digest = hashlib.sha256(
        json.dumps(content, sort_keys=True, default=str).encode("utf-8")
    ).hexdigest()[:12]
    name = f"{profile_id}-{str(term).rsplit('#', 1)[-1].rsplit('/', 1)[-1]}-{digest}"
    return stable_iri("profile-specification", name)


def extraction_graph_iri(identifier: str) -> str:
    return f"{RESOURCE}graph/extraction/{quote(identifier, safe='')}"


def extraction_graph(
    extraction: dict[str, Any],
    profile: dict[str, Any],
    observations: list[dict[str, Any]] | None = None,
    references: dict[str, Any] | None = None,
) -> Graph:
    """Grafo de una preparación y, si se dan, de sus observaciones puntuales.

    Las observaciones las prepara quien tiene la serie y las medidas a mano
    (`aplicacion_web.application.observations`); aquí solo se traducen. Lo que
    no venga no se escribe: una observación ausente es una falta que el
    razonador debe ver como tal, no un valor que rellenar.
    """
    graph = Graph()
    graph.bind("edt", EDT)
    extraction_id = str(extraction["id"])
    extraction_iri = stable_iri("extraction", extraction_id)
    profile_iri = stable_iri("profile", str(profile["id"]))
    barista_iri = stable_iri("barista", str(extraction.get("barista") or "unknown"))
    coffee_iri = stable_iri("coffee", str(extraction.get("coffeeId") or "unknown"))
    beverage_iri = stable_iri("beverage", extraction_id)
    graph.add((extraction_iri, RDF.type, EDT.EspressoExtraction))
    graph.add((extraction_iri, EDT.usesProfile, profile_iri))
    graph.add((extraction_iri, EDT.performedBy, barista_iri))
    graph.add((extraction_iri, EDT.usesMaterial, coffee_iri))
    graph.add((extraction_iri, EDT.producesBeverage, beverage_iri))
    # Un instante que no consta no se escribe. Antes se escribía igual y una
    # extracción sin final daba el literal «None»; ahora la falta se ve como
    # falta, y las formas deciden si es admisible (solo con pérdida de energía).
    for predicate, field in ((EDT.startTime, "startedAt"), (EDT.endTime, "finishedAt")):
        if extraction.get(field) is not None:
            graph.add(
                (extraction_iri, predicate, Literal(extraction[field], datatype=XSD.dateTime))
            )
    cause = TERMINATION_CAUSES.get(str(extraction.get("termination")))
    if cause is not None:
        graph.add((extraction_iri, EDT.hasTerminationCause, cause))
        graph.add((cause, RDF.type, EDT.TerminationCause))
    graph.add((profile_iri, RDF.type, EDT.ExtractionProfile))
    graph.add((barista_iri, RDF.type, EDT.Barista))
    graph.add((barista_iri, RDF.type, EDT.Agent))
    graph.add((coffee_iri, RDF.type, EDT.CoffeeMaterial))
    graph.add((beverage_iri, RDF.type, EDT.EspressoBeverage))
    graph.add(
        (
            profile_iri,
            EDT.definedBy,
            stable_iri("barista", str((profile.get("provenance") or {}).get("actor") or "unknown")),
        )
    )
    author_iri = stable_iri(
        "barista", str((profile.get("provenance") or {}).get("actor") or "unknown")
    )
    graph.add((author_iri, RDF.type, EDT.Barista))
    graph.add((author_iri, RDF.type, EDT.Agent))
    status_iri = stable_iri("profile-status", str(profile.get("status") or "unknown"))
    graph.add((status_iri, RDF.type, EDT.ProfileStatus))
    graph.add((profile_iri, EDT.hasProfileStatus, status_iri))
    specifications = list(profile.get("specifications") or [])
    for specification in specifications:
        spec_iri = specification_iri(str(profile["id"]), specification)
        spec_property = property_iri(str(specification.get("property")))
        spec_unit = unit_iri(specification.get("unit"))
        graph.add((spec_iri, RDF.type, EDT.ProfileParameterSpecification))
        graph.add((spec_property, RDF.type, EDT.ObservableProperty))
        graph.add((spec_unit, RDF.type, EDT.Unit))
        graph.add((profile_iri, EDT.hasParameterSpecification, spec_iri))
        graph.add((spec_iri, EDT.forProperty, spec_property))
        graph.add((spec_iri, EDT.hasUnit, spec_unit))
        source = specification.get("derivedFrom")
        if isinstance(source, int) and 0 <= source < len(specifications):
            graph.add((
                spec_iri,
                EDT.derivedFromSpecification,
                specification_iri(str(profile["id"]), specifications[source]),
            ))
        if specification.get("strategy") == "target":
            graph.add(
                (
                    spec_iri,
                    EDT.targetValue,
                    Literal(Decimal(str(specification["target"])), datatype=XSD.decimal),
                )
            )
            if specification.get("tolerance") is not None:
                graph.add(
                    (
                        spec_iri,
                        EDT.toleranceValue,
                        Literal(Decimal(str(specification["tolerance"])), datatype=XSD.decimal),
                    )
                )
        else:
            graph.add(
                (
                    spec_iri,
                    EDT.minimumValue,
                    Literal(Decimal(str(specification["minimum"])), datatype=XSD.decimal),
                )
            )
            graph.add(
                (
                    spec_iri,
                    EDT.maximumValue,
                    Literal(Decimal(str(specification["maximum"])), datatype=XSD.decimal),
                )
            )
    if has_series(extraction):
        # La placa mide el metal; la práctica simula el agua (EKO 3.4.0).
        temperature_site = (
            EDT.BoilerBodyTemperature if extraction.get("source") == "measured"
            else EDT.WaterTemperature
        )
        for name, series_property, unit in (
            ("temperature", temperature_site, "°C"),
            ("pressure", EDT.ExtractionPressure, "bar"),
        ):
            series_iri = stable_iri("series", f"{extraction_id}-{name}")
            series_unit = unit_iri(unit)
            graph.add((series_iri, RDF.type, EDT.ObservationSeries))
            graph.add((series_property, RDF.type, EDT.ObservableProperty))
            graph.add((series_unit, RDF.type, EDT.Unit))
            graph.add((extraction_iri, EDT.hasObservationSeries, series_iri))
            graph.add((series_iri, EDT.seriesForProperty, series_property))
            graph.add((series_iri, EDT.hasUnit, series_unit))
            graph.add(
                (
                    series_iri,
                    EDT.dataReference,
                    Literal(
                        f"{RESOURCE}series/{quote(extraction_id, safe='')}/{name}",
                        datatype=XSD.anyURI,
                    ),
                )
            )
    # El cuarto como condición de la preparación (PC17, EKO 3.5.0): sus
    # observaciones cuelgan de ella y no de la extracción, que es donde las
    # buscan la pregunta y la regla de la humedad. Sin inicio y fin no hay
    # vigencia que declarar, y entonces quedan sobre la extracción.
    environment = None
    environmental = [item for item in observations or [] if item.get("feature") == "environment"]
    if environmental and extraction.get("startedAt") and extraction.get("finishedAt"):
        environment = stable_iri("environment", extraction_id)
        graph.add((environment, RDF.type, EDT.EnvironmentalCondition))
        graph.add((extraction_iri, EDT.occurredUnder, environment))
        graph.add((environment, EDT.validFrom,
                   Literal(extraction["startedAt"], datatype=XSD.dateTime)))
        graph.add((environment, EDT.validUntil,
                   Literal(extraction["finishedAt"], datatype=XSD.dateTime)))
    _add_observations(graph, extraction_id, extraction_iri, beverage_iri, observations or [],
                      environment)
    _add_sensory_evaluation(graph, extraction_iri, beverage_iri,
                            extraction.get("sensoryEvaluation"))
    _add_sensory_references(graph, coffee_iri, profile_iri, references or {})
    # La dosis de esta preparación aparte del café del que sale (PC01): el café
    # es el lote, la dosis es lo que entró en el portafiltro.
    dose_iri = stable_iri("dose", extraction_id)
    graph.add((dose_iri, RDF.type, EDT.CoffeeDose))
    # Las formas se validan sin inferencia: la superclase se declara.
    graph.add((dose_iri, RDF.type, EDT.CoffeeMaterial))
    graph.add((extraction_iri, EDT.usesMaterial, dose_iri))
    _add_coffee(graph, coffee_iri, (references or {}).get("coffee"))
    _add_equipment(graph, extraction_iri, (references or {}).get("equipment") or [])
    return graph


#: Nivel de aceptación del barista y su individuo en EKO. Lo declara el
#: barista; nunca se deduce de las puntuaciones ni de los defectos.
ACCEPTANCE = {
    "accepted": EDT.SensoryAccepted,
    "rejected": EDT.SensoryRejected,
    "inconclusive": EDT.SensoryAcceptanceInconclusive,
}
DIMENSIONS = {
    "hedonic-liking": (EDT.HedonicLiking, EDT.HedonicLikingScore),
    "perceived-intensity": (EDT.PerceivedIntensity, EDT.PerceivedIntensityScore),
}


def _add_sensory_evaluation(
    graph: Graph,
    extraction_iri: URIRef,
    beverage_iri: URIRef,
    evaluation: dict[str, Any] | None,
) -> None:
    """La catación registrada. Los nombres de atributo y defecto de la aplicación
    son los de EKO, así que se traducen sin tabla."""
    if not evaluation or not evaluation.get("scores") or not evaluation.get("evaluatedAt"):
        return
    node = stable_iri("sensory-evaluation", str(evaluation.get("id") or extraction_iri))
    evaluator = stable_iri("barista", str(evaluation.get("evaluatedBy") or "unknown"))
    graph.add((evaluator, RDF.type, EDT.Barista))
    graph.add((evaluator, RDF.type, EDT.Agent))
    graph.add((node, RDF.type, EDT.SensoryEvaluation))
    graph.add((extraction_iri, EDT.hasSensoryEvaluation, node))
    graph.add((node, EDT.evaluatedBy, evaluator))
    graph.add((node, EDT.evaluatesBeverage, beverage_iri))
    graph.add((node, EDT.evaluationTime,
               Literal(evaluation["evaluatedAt"], datatype=XSD.dateTime)))
    for score in evaluation["scores"]:
        dimension, kind = DIMENSIONS[score["dimension"]]
        attribute = EDT[score["attribute"]]
        score_iri = stable_iri(
            "sensory-score", f"{evaluation.get('id')}-{score['attribute']}-{score['dimension']}"
        )
        graph.add((score_iri, RDF.type, EDT.SensoryScore))
        graph.add((score_iri, RDF.type, kind))
        graph.add((attribute, RDF.type, EDT.SensoryAttribute))
        graph.add((dimension, RDF.type, EDT.SensoryDimension))
        graph.add((node, EDT.hasSensoryScore, score_iri))
        graph.add((score_iri, EDT.forSensoryAttribute, attribute))
        graph.add((score_iri, EDT.measuresDimension, dimension))
        graph.add((score_iri, EDT.scoreValue, Literal(int(score["value"]), datatype=XSD.integer)))
    for defect in evaluation.get("defects") or []:
        graph.add((EDT[defect], RDF.type, EDT.SensoryDefect))
        graph.add((node, EDT.hasPerceivedDefect, EDT[defect]))
    # Los descriptores de la lista, los escritos a mano y los cambios deseados
    # (EKO 3.5.0). Hasta aquí la cata llegaba al grafo sin ellos, y la PC07 y
    # la PC25 no respondían con un café real aunque la ontología los modela.
    _add_descriptors(graph, node, evaluation.get("descriptors") or [],
                     [item.get("name") for item in evaluation.get("otherDescriptors") or []])
    for change in evaluation.get("desiredChanges") or []:
        change_iri = stable_iri(
            "desired-change", f"{evaluation.get('id')}-{change['attribute']}"
        )
        graph.add((change_iri, RDF.type, EDT.DesiredSensoryChange))
        graph.add((node, EDT.hasDesiredSensoryChange, change_iri))
        graph.add((change_iri, EDT.targetsSensoryAttribute, EDT[change["attribute"]]))
        graph.add((EDT[change["attribute"]], RDF.type, EDT.SensoryAttribute))
        graph.add((change_iri, EDT.desiredDirection, EDT[change["direction"]]))
        graph.add((EDT[change["direction"]], RDF.type, EDT.DesiredChangeDirection))
    # Lo que la ficha CVA marca aparte (EKO 3.5.0). La aplicación los nombra
    # cortos («Salty»); en EKO llevan el sufijo de su clase para no chocar con
    # atributos y descriptores del mismo nombre.
    for taste in evaluation.get("mainTastes") or []:
        graph.add((EDT[f"{taste}Taste"], RDF.type, EDT.BasicTaste))
        graph.add((node, EDT.hasMainTaste, EDT[f"{taste}Taste"]))
    for texture in evaluation.get("mouthfeelTextures") or []:
        graph.add((EDT[f"{texture}Mouthfeel"], RDF.type, EDT.MouthfeelTexture))
        graph.add((node, EDT.hasMouthfeelTexture, EDT[f"{texture}Mouthfeel"]))
    acceptance = evaluation.get("acceptance") or {}
    status = ACCEPTANCE.get(str(acceptance.get("status")))
    if status is not None:
        graph.add((status, RDF.type, EDT.SensoryAcceptanceStatus))
        graph.add((node, EDT.hasSensoryAcceptanceStatus, status))
        if acceptance.get("rationale"):
            graph.add((node, EDT.acceptanceRationale,
                       Literal(acceptance["rationale"], lang="es")))


def _add_observations(
    graph: Graph,
    extraction_id: str,
    extraction_iri: URIRef,
    beverage_iri: URIRef,
    observations: list[dict[str, Any]],
    environment: URIRef | None = None,
) -> None:
    nodes: dict[str, URIRef] = {}
    for item in observations:
        node = stable_iri("observation", f"{extraction_id}-{item['key']}")
        nodes[item["key"]] = node
    for item in observations:
        node = nodes[item["key"]]
        observed = property_iri(str(item["property"]))
        graph.add((node, RDF.type, EDT.Observation))
        graph.add((observed, RDF.type, EDT.ObservableProperty))
        graph.add((extraction_iri, EDT.hasObservation, node))
        graph.add((node, EDT.observedProperty, observed))
        feature = (
            beverage_iri if item.get("feature") == "beverage"
            else environment if item.get("feature") == "environment" and environment is not None
            else extraction_iri
        )
        graph.add((node, EDT.featureOfInterest, feature))
        graph.add((node, EDT.resultTime, Literal(item["resultTime"], datatype=XSD.dateTime)))
        if item.get("text") is not None:
            # Lo que el barista vio, con sus palabras: el razonador solo compara
            # si dos preparaciones dicen lo mismo, no interpreta el texto.
            graph.add((node, RDF.type, EDT.QualitativeObservation))
            graph.add((node, EDT.textValue, Literal(str(item["text"]))))
        else:
            unit = unit_iri(item.get("unit"))
            graph.add((node, RDF.type, EDT.QuantitativeObservation))
            graph.add((unit, RDF.type, EDT.Unit))
            graph.add((node, EDT.numericValue, Literal(Decimal(str(item["value"])),
                                                       datatype=XSD.decimal)))
            graph.add((node, EDT.hasUnit, unit))
        if item.get("kind") in ("calculated", "estimated"):
            graph.add((node, RDF.type, EDT.CalculatedObservation))
            if item["kind"] == "estimated":
                graph.add((node, RDF.type, EDT.EstimatedObservation))
            graph.add((node, EDT.calculationMethodReference,
                       Literal(item["method"], datatype=XSD.anyURI)))
            if item.get("fromSeries"):
                graph.add((node, EDT.derivedFromSeries,
                           stable_iri("series", f"{extraction_id}-{item['fromSeries']}")))
            for key in item.get("computedFrom") or []:
                graph.add((node, EDT.computedFrom, nodes[key]))
        elif item.get("text") is None:
            graph.add((node, RDF.type, EDT.MeasuredObservation))
        if item.get("recordedBy") and item.get("recordedAt"):
            recorder = stable_iri("barista", str(item["recordedBy"]))
            graph.add((recorder, RDF.type, EDT.Agent))
            graph.add((node, EDT.recordedBy, recorder))
            graph.add((node, EDT.recordingTime,
                       Literal(item["recordedAt"], datatype=XSD.dateTime)))
        if item.get("note"):
            graph.add((node, EDT.description, Literal(item["note"], lang="es")))


def _add_coffee(graph: Graph, coffee_iri: URIRef, coffee: dict[str, Any] | None) -> None:
    """Lo que la ficha del café dice de él (PC16, EKO 3.5.0).

    Variedad, proceso y tueste son texto en la plataforma; cada valor es un
    individuo de su clase con ese texto de etiqueta, sin forzarlo a uno de los
    pocos que trae EKO. Una fecha de tueste sin hora no se publica: EKO la pide
    con hora, y ponerle la medianoche afirmaría una hora que nadie anotó.
    """
    if not coffee:
        return
    for predicate, field in (
        (EDT.commercialReference, "commercialReference"),
        (EDT.lotIdentifier, "lotIdentifier"),
        (EDT.bagIdentifier, "bagIdentifier"),
    ):
        if coffee.get(field):
            graph.add((coffee_iri, predicate, Literal(str(coffee[field]), datatype=XSD.string)))
    roast_date = str(coffee.get("roastDate") or "")
    if "T" in roast_date:
        graph.add((coffee_iri, EDT.roastDate, Literal(roast_date, datatype=XSD.dateTime)))
    for predicate, field, kind, cls in (
        (EDT.hasVariety, "variety", "variety", EDT.CoffeeVariety),
        (EDT.hasPostHarvestProcess, "postHarvestProcess", "process", EDT.PostHarvestProcessType),
        (EDT.hasRoastLevel, "roastLevel", "roast-level", EDT.RoastLevel),
    ):
        if coffee.get(field):
            node = stable_iri(kind, str(coffee[field]).casefold())
            graph.add((node, RDF.type, cls))
            graph.add((node, RDFS.label, Literal(str(coffee[field]), lang="es")))
            graph.add((coffee_iri, predicate, node))
    terroir_fields = ("region", "altitudeM", "soilCharacteristics", "mineralComposition")
    if not any(coffee.get(field) is not None for field in terroir_fields):
        return
    terroir = stable_iri("terroir", str(coffee.get("id") or coffee_iri))
    graph.add((terroir, RDF.type, EDT.Terroir))
    graph.add((coffee_iri, EDT.hasTerroir, terroir))
    if coffee.get("region"):
        graph.add((terroir, EDT.regionName, Literal(str(coffee["region"]), datatype=XSD.string)))
    if coffee.get("altitudeM") is not None:
        altitude = stable_iri("altitude", str(coffee.get("id") or coffee_iri))
        graph.add((altitude, RDF.type, EDT.AltitudeSpecification))
        graph.add((terroir, EDT.hasAltitude, altitude))
        graph.add((altitude, EDT.quantityValue,
                   Literal(Decimal(str(coffee["altitudeM"])), datatype=XSD.decimal)))
        graph.add((altitude, EDT.hasUnit, EDT.Meter))
        graph.add((EDT.Meter, RDF.type, EDT.Unit))
    if coffee.get("soilCharacteristics"):
        graph.add((terroir, EDT.soilCharacteristics,
                   Literal(str(coffee["soilCharacteristics"]), datatype=XSD.string)))
    # La composición mineral solo con su fuente: así lo piden las formas.
    if coffee.get("mineralComposition") and coffee.get("mineralSource"):
        graph.add((terroir, EDT.mineralComposition,
                   Literal(str(coffee["mineralComposition"]), datatype=XSD.string)))
        graph.add((terroir, DCTERMS.source, Literal(str(coffee["mineralSource"]), lang="es")))


#: Tipo de equipo de la plataforma y su clase en EKO.
EQUIPMENT_TYPES = {"machine": EDT.EspressoMachine, "grinder": EDT.Grinder}


def _add_equipment(
    graph: Graph, extraction_iri: URIRef, equipment: list[dict[str, Any]]
) -> None:
    """La máquina y el molino de la preparación, con lo que cada uno permite
    ajustar a mano o solo (PC01, PC18, EKO 3.5.0)."""
    for item in equipment:
        cls = EQUIPMENT_TYPES.get(str(item.get("type")))
        if cls is None or not item.get("id"):
            continue
        node = stable_iri("equipment", str(item["id"]))
        graph.add((node, RDF.type, cls))
        if item.get("name"):
            graph.add((node, RDFS.label, Literal(str(item["name"]), lang="es")))
        graph.add((extraction_iri, EDT.usesEquipment, node))
        for capability in item.get("capabilities") or []:
            prop = property_iri(str(capability.get("property")))
            graph.add((prop, RDF.type, EDT.ObservableProperty))
            if capability.get("manualAdjustment"):
                graph.add((node, EDT.allowsManualAdjustmentOf, prop))
            if capability.get("automaticAdjustment"):
                graph.add((node, EDT.allowsAutomaticAdjustmentOf, prop))


#: Los descriptores del vocabulario de EKO, por su nombre en la ontología; los
#: mismos que admite la plataforma (`SensoryDescriptor` en sus esquemas).
DESCRIPTORS = {
    "Citrus", "RedFruit", "TropicalFruit", "RipeFruit", "DriedFruit",
    "Caramel", "Honey", "Panela", "Chocolate", "Vanilla", "BrownSugar",
    "GenericFloral", "Jasmine", "Rose",
    "Nuts", "Spices", "Cereal", "Herbal", "FermentedDescriptor",
    "Cocoa", "Tobacco", "Earthy", "Woody",
}


def _vocabulary(values: list[str]) -> tuple[list[str], list[str]]:
    """Separa lo que es vocabulario de EKO de lo que es texto libre.

    La descripción del tostador puede traer palabras sueltas («panela»): las
    que coinciden con un descriptor, sin distinguir mayúsculas, son ese
    descriptor; las demás se publican como texto, no se fuerzan a uno.
    """
    by_name = {name.casefold(): name for name in DESCRIPTORS}
    known, written = [], []
    for value in values:
        name = by_name.get(str(value).casefold())
        (known if name else written).append(name or str(value))
    return known, written


def _add_descriptors(
    graph: Graph, node: URIRef, descriptors: list[str], written: list[str | None]
) -> None:
    known, loose = _vocabulary(descriptors)
    for name in known:
        graph.add((EDT[name], RDF.type, EDT.SensoryDescriptor))
        graph.add((node, EDT.hasSensoryDescriptor, EDT[name]))
    for text in [*loose, *(item for item in written if item)]:
        graph.add((node, EDT.writtenDescriptor, Literal(text, lang="es")))


def _add_sensory_references(
    graph: Graph, coffee_iri: URIRef, profile_iri: URIRef, references: dict[str, Any]
) -> None:
    """La descripción del tostador y el sabor buscado del perfil (EKO 3.5.0).

    Sin ellos la PC25 no podía comparar lo declarado, lo buscado y lo
    observado. Un perfil sensorial necesita al menos un descriptor del
    vocabulario (formas de EKO); uno que solo traiga texto libre no se publica,
    porque no sería un perfil sensorial sino una nota.
    """
    declared = references.get("declared")
    if declared:
        known, _ = _vocabulary(list(declared.get("descriptors") or []))
        if known and declared.get("originalText") and declared.get("source"):
            node = stable_iri("declared-sensory-profile", str(declared.get("id") or coffee_iri))
            source = stable_iri("source", str(declared.get("declaredBy") or "unknown"))
            graph.add((node, RDF.type, EDT.DeclaredSensoryProfile))
            graph.add((coffee_iri, EDT.hasDeclaredSensoryProfile, node))
            graph.add((node, EDT.describesCoffee, coffee_iri))
            graph.add((source, RDF.type, EDT.Agent))
            graph.add((node, EDT.declaredBy, source))
            graph.add((node, EDT.sensoryDescriptionText,
                       Literal(declared["originalText"], lang="es")))
            graph.add((node, DCTERMS.source, Literal(declared["source"], lang="es")))
            _add_descriptors(graph, node, list(declared.get("descriptors") or []), [])
    target = references.get("target")
    if target:
        known, _ = _vocabulary(list(target.get("descriptors") or []))
        if known:
            node = stable_iri("target-sensory-profile", str(target.get("id") or profile_iri))
            barista = stable_iri("barista", str(target.get("definedBy") or "unknown"))
            graph.add((node, RDF.type, EDT.TargetSensoryProfile))
            graph.add((profile_iri, EDT.hasTargetSensoryProfile, node))
            # El sabor buscado es de un café concreto, que puede no ser el de
            # esta preparación si el perfil sirve a varios.
            target_coffee = (
                stable_iri("coffee", str(target["coffeeId"])) if target.get("coffeeId") else coffee_iri
            )
            graph.add((target_coffee, RDF.type, EDT.CoffeeMaterial))
            graph.add((node, EDT.describesCoffee, target_coffee))
            # La plataforma solo admite un sabor buscado para un café del perfil
            # (`set_sensory_target`), así que el perfil le es aplicable.
            graph.add((profile_iri, EDT.applicableToCoffee, target_coffee))
            graph.add((barista, RDF.type, EDT.Barista))
            graph.add((barista, RDF.type, EDT.Agent))
            graph.add((node, EDT.sensoryTargetDefinedBy, barista))
            graph.add((node, DCTERMS.source, Literal("Sabor buscado del perfil en la Bitácora", lang="es")))
            _add_descriptors(graph, node, list(target.get("descriptors") or []),
                             list(target.get("otherDescriptors") or []))


#: Defectos de EKO por su nombre; un objetivo que no sea uno de ellos ni una
#: propiedad no se publica como objetivo.
DEFECTS = {
    "Burnt", "ExcessiveAstringency", "ExcessiveBitterness", "ExcessiveAcidity",
    "InsufficientAcidity", "FermentedDefect", "Vegetal", "Ashy", "Metallic",
    "PersistentUnpleasantTaste", "Moldy", "Phenolic", "Potato",
}


def adjustment_graph(episode: dict[str, Any], action: str | None = None) -> Graph:
    """Un ajuste del barista entre dos preparaciones (PC21, EKO 3.5.0).

    La preparación inicial precede al ajuste, el ajuste a la de comprobación y
    esta evalúa su resultado: es la cadena con la que el razonador compara las
    dos tazas. `action` es la acción recomendada en que se basó, si la hubo y si
    el barista la aceptó o la modificó; las formas exigen además que cambie la
    misma propiedad, así que quien llama solo la pasa en ese caso.
    """
    graph = Graph()
    graph.bind("edt", EDT)
    adjustment = episode.get("adjustment") or {}
    node = stable_iri("adjustment", str(adjustment.get("id") or episode["id"]))
    graph.add((node, RDF.type, EDT.ParameterAdjustment))
    graph.add((node, RDF.type, EDT.ManualParameterAdjustment))
    barista = stable_iri("barista", str(adjustment.get("performedBy") or "unknown"))
    graph.add((barista, RDF.type, EDT.Barista))
    graph.add((barista, RDF.type, EDT.Agent))
    graph.add((node, EDT.performedBy, barista))
    changed = property_iri(str(adjustment.get("property")))
    graph.add((changed, RDF.type, EDT.ObservableProperty))
    graph.add((node, EDT.changesProperty, changed))
    for predicate, field in ((EDT.previousNumericValue, "previousValue"),
                             (EDT.newNumericValue, "newValue")):
        if adjustment.get(field) is not None:
            graph.add((node, predicate,
                       Literal(Decimal(str(adjustment[field])), datatype=XSD.decimal)))
    unit = unit_iri(adjustment.get("unit"))
    graph.add((unit, RDF.type, EDT.Unit))
    graph.add((node, EDT.hasUnit, unit))
    if adjustment.get("occurredAt"):
        moment = Literal(adjustment["occurredAt"], datatype=XSD.dateTime)
        graph.add((node, EDT.startTime, moment))
        graph.add((node, EDT.endTime, moment))
    target = episode.get("targetOutcome") or {}
    if target.get("kind") == "sensory-defect" and target.get("code") in DEFECTS:
        graph.add((EDT[target["code"]], RDF.type, EDT.SensoryDefect))
        graph.add((node, EDT.targetsOutcome, EDT[target["code"]]))
    elif target.get("kind") == "observable-property" and target.get("code"):
        outcome = property_iri(str(target["code"]))
        graph.add((outcome, RDF.type, EDT.ObservableProperty))
        graph.add((node, EDT.targetsOutcome, outcome))
    initial = stable_iri("extraction", str(episode["initialExtractionId"]))
    graph.add((initial, EDT.precedes, node))
    if episode.get("followUpExtractionId"):
        follow_up = stable_iri("extraction", str(episode["followUpExtractionId"]))
        graph.add((node, EDT.precedes, follow_up))
        graph.add((follow_up, EDT.evaluatesOutcomeOf, node))
    if action:
        graph.add((node, EDT.basedOnRecommendation, URIRef(action)))
    return graph


def validated_jsonld(
    extraction: dict[str, Any],
    profile: dict[str, Any],
    observations: list[dict[str, Any]] | None = None,
    references: dict[str, Any] | None = None,
) -> dict[str, Any]:
    graph = extraction_graph(extraction, profile, observations, references)
    conforms, _, report = validate(graph, shacl_graph=str(SHAPES), advanced=True)
    if not conforms:
        raise SemanticProjectionError(
            "la extracción no cumple las formas SHACL canónicas", report=str(report)
        )
    serialized = graph.serialize(format="json-ld", indent=2)
    return {
        "@context": {"edt": str(EDT)},
        "@id": str(stable_iri("extraction", str(extraction["id"]))),
        "@graph": json.loads(serialized),
        "schemaVersion": "1.0",
        "resourceIri": str(stable_iri("extraction", str(extraction["id"]))),
        "validation": {"conforms": True, "shapes": "espresso-dt-shapes.ttl"},
    }
