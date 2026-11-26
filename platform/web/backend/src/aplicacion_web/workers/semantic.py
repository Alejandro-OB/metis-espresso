"""Valida y publica proyecciones de simulación en grafos nombrados de Fuseki."""

from __future__ import annotations

import logging
import time
from collections.abc import Callable
from typing import Any

from espresso_profiles.water_range import judged_specifications, line_of, measures_boiler
from espresso_semantics import (
    FusekiClient,
    FusekiError,
    SemanticManifest,
    graph_iri,
    simulation_graph,
    validate_simulation_graph,
)
from espresso_semantics.extraction import (
    EDT,
    SemanticProjectionError,
    extraction_graph,
    extraction_graph_iri,
    stable_iri,
    validated_jsonld,
)
from espresso_semantics.projection import resource_iri
from rdflib import RDF, XSD, Graph, Literal, URIRef

from aplicacion_web.adapters.database import SqlRepository, build_database
from aplicacion_web.application.observations import (
    conformity_window,
    preparation_observations,
)
from aplicacion_web.application.revisions import with_revisions
from aplicacion_web.application.services import CONFORMITY_KIND
from aplicacion_web.config import Settings, get_settings
from aplicacion_web.domain.models import iso_now
from aplicacion_web.semantic.references import sensory_references

logger = logging.getLogger("aplicacion_web.workers.semantic")


def _report_once(reported: set[tuple[str, str]] | None, extraction_id: str, reason: str) -> bool:
    """Si este motivo de esta extracción no se ha registrado aún en este proceso.

    Una extracción rechazada se reintenta en cada vuelta del bucle; registrarla
    cada vez llenaría el log con la misma línea y escondería las demás.
    """
    if reported is None:
        return True
    key = (extraction_id, reason)
    if key in reported:
        return False
    reported.add(key)
    return True


def publication_document(
    run: dict[str, Any],
    manifest: SemanticManifest,
    conforms: bool,
    status: str,
    published_at: str | None,
) -> dict[str, Any]:
    data = manifest.data
    return {
        "schemaVersion": "1.0",
        "simulationRunId": run["simulationRunId"],
        "status": status,
        "ontology": {key: data["ontology"][key] for key in ("id", "version", "sha256")},
        "simulationModule": {
            key: data["simulationModule"][key] for key in ("id", "version", "sha256")
        },
        "transformation": {"id": "simulation-rdf-mapping", "version": "1.0.0"},
        "validation": {
            "conforms": conforms,
            "shapesVersion": data["simulationShapes"]["version"],
            "validatedAt": iso_now(),
        },
        "namedGraphIri": graph_iri(run["simulationRunId"]),
        "resourceIri": str(resource_iri("simulation-run", run["simulationRunId"])),
        "publishedAt": published_at,
    }


#: Espera creciente entre intentos de publicar el esquema, en segundos.
SCHEMA_RETRY_DELAYS = (1.0, 2.0, 5.0, 10.0, 30.0)


def publish_schema(
    manifest: SemanticManifest,
    fuseki: FusekiClient,
    *,
    attempts: int = len(SCHEMA_RETRY_DELAYS) + 1,
    sleep: Callable[[float], None] = time.sleep,
) -> None:
    """Publica el grafo de esquema, esperando a que el almacén esté disponible.

    Sin reintentos, una indisponibilidad momentánea de Fuseki termina el proceso
    y el trabajador queda a merced de la política de reinicio del contenedor,
    reiniciándose sin pausa y sin recuperarse por sí solo cuando el servicio
    vuelve.
    """
    manifest.verify()
    graph = Graph()
    for key in ("ontology", "simulationModule"):
        graph.parse(manifest.project_root / manifest.data[key]["path"], format="turtle")
    module_version = manifest.data["simulationModule"]["version"]
    ontology_version = manifest.data["ontology"]["version"]
    graph_iri = (
        "https://w3id.org/espresso-dt/resource/graph/schema/"
        f"eko-{ontology_version}-simulation-{module_version}"
    )
    for attempt in range(attempts):
        try:
            fuseki.replace_graph(graph_iri, graph)
            return
        except FusekiError:
            if attempt >= attempts - 1:
                raise
            sleep(SCHEMA_RETRY_DELAYS[min(attempt, len(SCHEMA_RETRY_DELAYS) - 1)])


def process_next(
    repository: SqlRepository,
    manifest: SemanticManifest,
    fuseki: FusekiClient,
) -> bool:
    run = repository.claim_semantic_publication()
    if run is None:
        return False
    manifest.verify()
    graph = simulation_graph(run)
    report = validate_simulation_graph(graph)
    if not report["conforms"]:
        repository.save_semantic_publication(
            run["simulationRunId"],
            publication_document(run, manifest, False, "rejected", None),
        )
        return True
    try:
        fuseki.replace_graph(graph_iri(run["simulationRunId"]), graph)
    except FusekiError:
        repository.save_semantic_publication(
            run["simulationRunId"],
            publication_document(run, manifest, True, "pending", None),
        )
        return True
    published_at = iso_now()
    repository.save_semantic_publication(
        run["simulationRunId"],
        publication_document(run, manifest, True, "published", published_at),
    )
    return True


def report_skipped_extractions(
    repository: SqlRepository, reported: set[tuple[str, str]] | None = None
) -> list[str]:
    """Registra las extracciones que la publicación salta por no tener final.

    Las formas de EKO exigen inicio y fin salvo con pérdida inesperada de
    energía (`ExtractionStartShape`, `ExtractionEndShape`), y el trabajador no
    toma las demás sin final. Antes eso ocurría en silencio. Esto no las
    publica; solo deja dicho que faltan.
    """
    skipped = repository.extractions_without_end()
    for extraction_id in skipped:
        if _report_once(reported, extraction_id, "NO_END_TIME"):
            logger.warning(
                "extracción %s no publicada: NO_END_TIME (sin finishedAt y sin "
                "pérdida de energía declarada; las formas exigen el fin)",
                extraction_id,
            )
    return skipped


#: Versión de lo que la proyección escribe de una preparación. Subirla vuelve a
#: publicar todas: la 2 añade los valores puntuales y la catación que lee el
#: razonador, que las publicadas con la 1 no tienen; la 3 publica la serie del
#: metal como temperatura de la caldera y la especificación de caldera que se
#: deriva del agua (EKO 3.4.0), y las publicadas con la 2 decían «agua»; la 4
#: añade lo que la ontología modelaba y no se escribía (EKO 3.5.0): descriptores
#: y cambios deseados de la cata, la descripción del tostador, el sabor buscado,
#: la ficha del café, los equipos, la dosis y la condición ambiental; la 5
#: nombra cada especificación del perfil por su contenido y no por su posición
#: (2026-11-05): con la 4, `perfil-2` era la caldera en un café real y el tiempo
#: en el perfil completo, y SHACL rechazaba las recomendaciones al juntarlos;
#: la 6 forma ese nombre con el término de EKO y no con el texto de la propiedad,
#: porque «pressure» y «extraction-pressure» son la misma especificación.
PROJECTION_VERSION = "6"


def process_next_extraction(
    repository: SqlRepository,
    fuseki: FusekiClient,
    reported: set[tuple[str, str]] | None = None,
) -> bool:
    extraction = repository.next_extraction_for_semantics(PROJECTION_VERSION)
    if extraction is None:
        return False
    extraction_id = str(extraction["id"])
    # Con sus revisiones: las medidas a mano y la catación viven en ellas, y
    # sin aplicarlas el grafo publicaba la preparación tal como se cerró.
    extraction = with_revisions(repository, "extraction", extraction)
    profile_id = str((extraction.get("profile") or {}).get("id") or "")
    profile = repository.get_document("profile", profile_id)
    if profile is None:
        if _report_once(reported, extraction_id, "PROFILE_NOT_FOUND"):
            logger.warning(
                "extracción %s rechazada: PROFILE_NOT_FOUND (perfil %r)",
                extraction_id,
                profile_id,
            )
        repository.save_extraction_publication(
            extraction_id,
            {
                "status": "rejected",
                "reason": "PROFILE_NOT_FOUND",
                "publishedAt": None,
                "projectionVersion": PROJECTION_VERSION,
            },
        )
        return True
    # Los valores puntuales son lo que lee el razonador; sin ellos el grafo de
    # la preparación solo lleva referencias a sus series.
    samples = sorted(
        repository.list_documents(
            "live-sample", page=1, page_size=1000, filters={"extractionId": extraction_id}
        ).items,
        key=lambda sample: int(sample.get("sequence", 0)),
    )
    observations = preparation_observations(
        extraction, samples, conformity_window(extraction, samples)
    )
    # Lo mismo que juzga la plataforma: la caldera contra la referencia que sale
    # del agua pedida (EKO 3.4.0). El agua estimada no se publica aquí; estimarla
    # con el gemelo simula horas de telemetría y la pide el análisis del barista.
    profile = {
        **profile,
        "specifications": judged_specifications(
            profile.get("specifications") or [], extraction.get("boilerOffsetAppliedC"),
            line_of(extraction),
        ) if measures_boiler(extraction) else profile.get("specifications") or [],
    }
    try:
        references = sensory_references(repository, extraction)
        document = validated_jsonld(extraction, profile, observations, references)
        graph = extraction_graph(extraction, profile, observations, references)
        name = extraction_graph_iri(extraction_id)
        fuseki.replace_graph(name, graph)
    except SemanticProjectionError as error:
        # Un fallo de las formas no cambia al reintentar: la misma extracción da
        # el mismo grafo. Dejarla «pending» la devolvía en cada vuelta, primero
        # que ninguna, y bloqueaba la publicación de todas las que venían
        # detrás. Se rechaza con su informe, que es lo que dice qué corregir.
        if _report_once(reported, extraction_id, "SHACL_NONCONFORMANT"):
            logger.warning(
                "extracción %s rechazada: SHACL_NONCONFORMANT (%s)%s",
                extraction_id,
                error,
                f"\n{error.report}" if error.report else "",
            )
        repository.save_extraction_publication(
            extraction_id,
            {
                "status": "rejected",
                "reason": "SHACL_NONCONFORMANT",
                "projectionVersion": PROJECTION_VERSION,
                "report": error.report,
                "publishedAt": None,
            },
        )
        return True
    except FusekiError as error:
        # El almacén puede volver: esto sí se reintenta.
        if _report_once(reported, extraction_id, "FusekiError"):
            logger.warning("extracción %s pendiente: FusekiError (%s)", extraction_id, error)
        repository.save_extraction_publication(
            extraction_id,
            {
                "status": "pending",
                "reason": "FusekiError",
                "publishedAt": None,
                "projectionVersion": PROJECTION_VERSION,
            },
        )
        return True
    repository.save_extraction_publication(
        extraction_id,
        {
            "status": "published",
            "namedGraphIri": name,
            "projectionVersion": PROJECTION_VERSION,
            "revisionCount": len(extraction.get("revisions") or []),
            "resourceIri": str(stable_iri("extraction", extraction_id)),
            "schemaVersion": document["schemaVersion"],
            "publishedAt": iso_now(),
        },
    )
    return True


RECOMMENDATION_PUBLICATION = "recommendation-semantic"


def recommendation_graph_iri(recommendation_id: str) -> str:
    return f"https://w3id.org/espresso-dt/resource/graph/recommendation/{recommendation_id}"


#: Decisión del barista y su estado de validación en EKO.
VALIDATION_STATUS = {"accepted": "Accepted", "rejected": "Rejected", "modified": "Modified"}


def _recommendation_action(item: dict[str, Any], graph: Graph) -> URIRef | None:
    """La acción que el barista decidió: la que se guardó, o la del resumen.

    Las recomendaciones anteriores a que se guardara su acción la recuperan
    por su descripción, que es el resumen que el barista vio.
    """
    if item.get("actionIri"):
        return URIRef(str(item["actionIri"]))
    for action in graph.objects(None, EDT.hasRecommendedAction):
        if str(graph.value(action, EDT.description) or "") == str(item.get("summary") or ""):
            return action
    return None


def decision_triples(graph: Graph, action: URIRef, decision: dict[str, Any]) -> None:
    """La decisión del barista sobre la acción recomendada (PC19, EKO 3.5.0).

    Hasta aquí solo se publicaba lo que el razonador propuso; lo que el barista
    hizo con ello quedaba en la plataforma.
    """
    status = VALIDATION_STATUS.get(str(decision.get("status")))
    if status is None or not decision.get("decidedBy") or not decision.get("decidedAt"):
        return
    barista = stable_iri("barista", str(decision["decidedBy"]))
    graph.add((barista, RDF.type, EDT.Barista))
    graph.add((barista, RDF.type, EDT.Agent))
    graph.add((EDT[status], RDF.type, EDT.ValidationStatus))
    graph.add((action, EDT.hasValidationStatus, EDT[status]))
    graph.add((action, EDT.validatedBy, barista))
    graph.add((action, EDT.validationTime, Literal(decision["decidedAt"], datatype=XSD.dateTime)))
    if decision.get("comment"):
        graph.add((action, EDT.validationRationale, Literal(str(decision["comment"]), lang="es")))


def process_next_recommendation(repository: SqlRepository, fuseki: FusekiClient) -> bool:
    """Publica lo que el razonador concluyó cuando alguien pidió analizar.

    Solo las conclusiones que cumplieron las formas al razonar: la validación
    necesita la evidencia de la preparación, que está a mano en ese momento y
    no aquí. Una rechazada se marca con su motivo y no se reintenta, porque el
    mismo grafo daría el mismo resultado. Cuando el barista decide sobre la
    recomendación, el grafo se vuelve a publicar con su decisión (PC19).
    """
    for item in repository.list_documents("recommendation", page=1, page_size=1000).items:
        identifier = str(item["recommendationId"])
        if not item.get("conclusionGraph"):
            continue
        publication = repository.get_document(RECOMMENDATION_PUBLICATION, identifier)
        decision = repository.get_document("recommendation-decision", identifier) or {}
        decided_at = decision.get("decidedAt")
        if (
            publication is not None and publication.get("status") != "pending"
            and (publication.get("status") == "rejected"
                 or publication.get("decidedAt") == decided_at)
        ):
            continue
        if not (item.get("conclusionValidation") or {}).get("conforms"):
            repository.upsert_document(
                RECOMMENDATION_PUBLICATION,
                identifier,
                {"status": "rejected", "reason": "SHACL_NONCONFORMANT", "publishedAt": None},
            )
            return True
        name = recommendation_graph_iri(identifier)
        graph = Graph().parse(data=item["conclusionGraph"], format="turtle")
        action = _recommendation_action(item, graph)
        if decision and action is not None:
            decision_triples(graph, action, decision)
        try:
            fuseki.replace_graph(name, graph)
        except FusekiError as error:
            logger.warning("conclusiones de %s pendientes: FusekiError (%s)", identifier, error)
            repository.upsert_document(
                RECOMMENDATION_PUBLICATION,
                identifier,
                {"status": "pending", "reason": "FusekiError", "publishedAt": None},
            )
            return False
        repository.upsert_document(
            RECOMMENDATION_PUBLICATION,
            identifier,
            {"status": "published", "namedGraphIri": name, "publishedAt": iso_now(),
             "decidedAt": decided_at},
        )
        return True
    return False


EPISODE_PUBLICATION = "episode-semantic"


def episode_graph_iri(episode_id: str) -> str:
    return f"https://w3id.org/espresso-dt/resource/graph/episode/{episode_id}"


def process_next_episode(repository: SqlRepository, fuseki: FusekiClient) -> bool:
    """Publica cada ajuste con lo que el razonador concluyó de él (PC21).

    El ajuste, la cadena inicial → ajuste → comprobación y la evaluación del
    razonador. Se vuelve a publicar cada vez que el razonador lo reevalúa.
    """
    for item in repository.list_documents("episode", page=1, page_size=1000).items:
        identifier = str(item["id"])
        if not item.get("conclusionGraph"):
            continue
        publication = repository.get_document(EPISODE_PUBLICATION, identifier) or {}
        computed = item.get("conclusionsComputedAt")
        if publication.get("computedAt") == computed and publication.get("status") != "pending":
            continue
        if not (item.get("conclusionValidation") or {}).get("conforms"):
            repository.upsert_document(EPISODE_PUBLICATION, identifier, {
                "status": "rejected", "reason": "SHACL_NONCONFORMANT",
                "computedAt": computed, "publishedAt": None,
            })
            return True
        name = episode_graph_iri(identifier)
        try:
            fuseki.replace_graph(name, Graph().parse(data=item["conclusionGraph"], format="turtle"))
        except FusekiError as error:
            logger.warning("ajuste %s pendiente: FusekiError (%s)", identifier, error)
            repository.upsert_document(EPISODE_PUBLICATION, identifier, {
                "status": "pending", "reason": "FusekiError",
                "computedAt": computed, "publishedAt": None,
            })
            return False
        repository.upsert_document(EPISODE_PUBLICATION, identifier, {
            "status": "published", "namedGraphIri": name,
            "computedAt": computed, "publishedAt": iso_now(),
        })
        return True
    return False


CONFORMITY_PUBLICATION = "conformity-semantic"


def conformity_graph_iri(extraction_id: str) -> str:
    return f"https://w3id.org/espresso-dt/resource/graph/conformity/{extraction_id}"


def process_next_conformity(repository: SqlRepository, fuseki: FusekiClient) -> bool:
    """Publica la conformidad de cada café medido y sus desviaciones (PC04, PC22).

    Antes el razonador solo publicaba lo que concluía cuando alguien pedía una
    recomendación, y de los demás cafés el almacén no sabía si cumplieron el
    perfil. Se publica la última evaluación de cada preparación, en su propio
    grafo, sin las propuestas: esas siguen saliendo solo a pedido del barista.
    Una evaluación nueva reemplaza la publicada.
    """
    latest: dict[str, dict[str, Any]] = {}
    for item in repository.list_documents(CONFORMITY_KIND, page=1, page_size=1_000_000).items:
        key = str(item.get("extractionId"))
        if int(item.get("sequence") or 0) > int((latest.get(key) or {}).get("sequence") or 0):
            latest[key] = item
    for extraction_id, item in latest.items():
        if not item.get("conclusionGraph"):
            continue
        publication = repository.get_document(CONFORMITY_PUBLICATION, extraction_id) or {}
        same = publication.get("sequence") == item.get("sequence")
        if same and publication.get("status") != "pending":
            continue
        if not (item.get("conclusionValidation") or {}).get("conforms"):
            repository.upsert_document(CONFORMITY_PUBLICATION, extraction_id, {
                "status": "rejected", "reason": "SHACL_NONCONFORMANT",
                "sequence": item.get("sequence"), "publishedAt": None,
            })
            return True
        name = conformity_graph_iri(extraction_id)
        try:
            fuseki.replace_graph(name, Graph().parse(data=item["conclusionGraph"], format="turtle"))
        except FusekiError as error:
            logger.warning("conformidad de %s pendiente: FusekiError (%s)", extraction_id, error)
            repository.upsert_document(CONFORMITY_PUBLICATION, extraction_id, {
                "status": "pending", "reason": "FusekiError",
                "sequence": item.get("sequence"), "publishedAt": None,
            })
            return False
        repository.upsert_document(CONFORMITY_PUBLICATION, extraction_id, {
            "status": "published", "namedGraphIri": name,
            "sequence": item.get("sequence"), "publishedAt": iso_now(),
        })
        return True
    return False


def run_forever(settings: Settings | None = None) -> None:
    settings = settings or get_settings()
    engine, session_factory = build_database(settings.database_url)
    repository = SqlRepository(session_factory)
    manifest = SemanticManifest(settings.resolved_semantic_manifest_path, settings.project_root)
    fuseki = FusekiClient(
        settings.fuseki_dataset_url,
        settings.fuseki_user,
        settings.fuseki_password,
    )
    # Sin configuración, los avisos se perderían: el registro del contenedor es
    # el único sitio donde se ve qué extracciones quedaron fuera.
    logging.basicConfig(
        level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s"
    )
    reported: set[tuple[str, str]] = set()
    try:
        publish_schema(manifest, fuseki)
        while True:
            worked = process_next(repository, manifest, fuseki)
            if not worked:
                process_next_extraction(repository, fuseki, reported)
                process_next_recommendation(repository, fuseki)
                process_next_conformity(repository, fuseki)
                process_next_episode(repository, fuseki)
                # Fuera de la condición anterior: una extracción que se rechaza
                # en cada vuelta mantiene ocupado al trabajador, y aun así las
                # saltadas tienen que constar.
                report_skipped_extractions(repository, reported)
            time.sleep(settings.semantic_worker_poll_seconds)
    finally:
        engine.dispose()


if __name__ == "__main__":
    run_forever()
