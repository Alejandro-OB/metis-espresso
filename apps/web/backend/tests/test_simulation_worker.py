from __future__ import annotations

import json

import pytest
from espresso_simulation import FmuOutput, run_simulation

from aplicacion_web.workers import semantic as semantic_worker
from aplicacion_web.workers import simulation as worker_module


class FakePlant:
    def __init__(self, _path, step_seconds, output_channels, parameters=None):
        self.step_seconds = step_seconds
        self.output_channels = output_channels
        self.parameters = parameters or {}
        self.time = 0.0
        self.temperature = 92.6

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return None

    def step(self, heater, pump):
        self.time += self.step_seconds
        self.temperature += heater * 0.1
        return FmuOutput(
            round(self.time * 1000),
            {"temperatureC": self.temperature, "pressureBar": pump * 9},
        )


class FakeFuseki:
    def __init__(self):
        self.graphs = {}

    def replace_graph(self, graph_name, graph):
        self.graphs[graph_name] = graph


def request_payload():
    return {
        "schemaVersion": "1.0",
        "model": {"id": "espresso-detailed", "version": "1.0.0"},
        "profileId": "espresso-lab",
        "durationSeconds": 1.0,
        "controller": {"id": "profile-tracking", "version": "1.0.0"},
    }


def test_worker_completes_job_and_publishes_integrity_checked_series(client, barista_headers):
    created = client.post(
        "/api/v1/simulations",
        json=request_payload(),
        headers={**barista_headers, "Idempotency-Key": "worker-success"},
    ).json()

    def inline_executor(run, model, store, progress, cancelled, _timeout):
        result = run_simulation(
            run["simulationRunId"],
            model.artifact_path,
            run["durationSeconds"],
            run["profileSnapshot"],
            store,
            model.output_channels,
            model.data["capabilities"]["communicationStepSeconds"],
            adapter_factory=FakePlant,
            progress=progress,
            cancelled=cancelled,
        )
        return {"artifact": result.artifact, "summary": result.summary}

    assert worker_module.process_next(
        client.app.state.repository,
        client.app.state.service.model_registry,
        client.app.state.service.series_store,
        "worker-test",
        30,
        executor=inline_executor,
    )
    finished = client.get(f"/api/v1/simulations/{created['simulationRunId']}").json()
    assert finished["status"] == "succeeded"
    assert finished["progress"]["percent"] == 100
    assert finished["artifact"]["sha256"]
    series = client.get(f"/api/v1/simulations/{created['simulationRunId']}/series").json()
    assert len(series["samples"]) == 10
    assert series["samples"][0]["quality"] == "simulated"
    assert (
        worker_module.process_next(
            client.app.state.repository,
            client.app.state.service.model_registry,
            client.app.state.service.series_store,
            "worker-test",
            30,
            executor=inline_executor,
        )
        is False
    )

    fuseki = FakeFuseki()
    assert semantic_worker.process_next(
        client.app.state.repository,
        client.app.state.semantic_manifest,
        fuseki,
    )
    semantic = client.get(f"/api/v1/simulations/{created['simulationRunId']}/semantic").json()
    assert semantic["status"] == "published"
    assert semantic["publication"]["validation"]["conforms"] is True
    assert semantic["document"]["@id"].endswith(created["simulationRunId"])
    assert semantic["publication"]["namedGraphIri"] in fuseki.graphs


def test_semantic_worker_publishes_confirmed_extraction_to_canonical_graph(client):
    fuseki = FakeFuseki()
    assert semantic_worker.process_next_extraction(client.app.state.repository, fuseki)
    publications = client.app.state.repository.list_documents(
        "extraction-semantic", page=1, page_size=100
    )
    assert publications.total == 1
    assert publications.items[0]["status"] == "published"
    assert publications.items[0]["namedGraphIri"] in fuseki.graphs


def test_replica_mode_derives_conditions_from_the_extraction_and_persists_the_comparison(
    client, barista_headers
):
    """Una preparación cerrada puede replicarse y compararse de extremo a extremo."""
    extraction = _closed_extraction(client, barista_headers)
    created = client.post(
        f"/api/v1/extractions/{extraction['id']}/twin-comparison",
        json={},
        headers={**barista_headers, "Idempotency-Key": "twin-1"},
    )
    assert created.status_code == 202
    run = created.json()

    # las condiciones iniciales salen de la primera muestra válida, en kelvin
    conditions = run["initialConditions"]
    assert conditions["unit"] == "K"
    assert conditions["source"] == f"extraction:{extraction['id']}"
    measured_start = conditions["channels"]["temperatureC"]
    # Sin modelo pedido, la réplica corre con la versión vigente del registro (la 2.1)
    assert run["model"]["version"] == "2.1.0"
    assert conditions["fmuParameters"]["T_inicial"] == pytest.approx(measured_start + 273.15)
    # la duración se deriva de la preparación, no de la solicitud
    assert run["durationSeconds"] == pytest.approx(
        round(extraction["durationMs"] / 1000 / 0.1) * 0.1
    )
    # Con la caldera como canal, la réplica la gobierna el control de la placa,
    # y la ejecución dice de qué actas salen sus números.
    assert run["controller"]["id"] == "board-replica"
    assert run["controller"]["commissioning"] == [
        "acta-temperatura-20261019-v7",
        "acta-presion-20261012-v1",
    ]

    def inline_executor(run_, model, store, progress, cancelled, _timeout):
        result = run_simulation(
            run_["simulationRunId"],
            model.artifact_path,
            run_["durationSeconds"],
            run_["profileSnapshot"],
            store,
            model.output_channels,
            model.data["capabilities"]["communicationStepSeconds"],
            adapter_factory=FakePlant,
            progress=progress,
            cancelled=cancelled,
            controller_definition=worker_module.run_controller_definition(run_, model.root),
            initial_conditions=run_.get("initialConditions"),
            temperature_quantity=model.channel_quantity("temperatureC"),
        )
        return {"artifact": result.artifact, "summary": result.summary}

    assert worker_module.process_next(
        client.app.state.repository,
        client.app.state.service.model_registry,
        client.app.state.service.series_store,
        "worker-twin",
        30,
        executor=inline_executor,
    )

    stored = client.get(f"/api/v1/extractions/{extraction['id']}/twin-comparison").json()
    assert stored["total"] == 1
    comparison = stored["latest"]
    assert comparison["status"] == "succeeded"
    assert comparison["simulationRunId"] == run["simulationRunId"]
    assert comparison["provenance"]["source"] == "simulation-comparison"
    assert comparison["alignment"]["method"] == "linear-interpolation-on-measured-grid"
    assert comparison["alignment"]["minimumCoverageRatio"] == 0.8
    assert comparison["controller"]["id"] == "board-replica"
    assert comparison["controller"]["version"] == "1.0.0"

    channels = {item["channel"]: item for item in comparison["channels"]}
    assert channels["temperatureC"]["modelTolerance"] == 2.22
    assert channels["temperatureC"]["contributesToVerdict"] is True
    # En la v2 la presión es estable en el registro y vota con su tolerancia.
    assert channels["pressureBar"]["modelTolerance"] == 0.89
    assert channels["pressureBar"]["contributesToVerdict"] is True

    # la ficha de resultado la expone junto al resto de la evaluación
    result = client.get(f"/api/v1/extractions/{extraction['id']}/result").json()
    assert result["digitalTwin"]["comparisonId"] == comparison["comparisonId"]


def test_board_replica_is_refused_for_a_free_simulation(client, barista_headers):
    """Arranca en el pulsado: sin preparación que replicar no tiene de dónde partir."""
    payload = {**request_payload(), "model": {"id": "espresso-detailed", "version": "2.0.0"}}
    payload["controller"] = {"id": "board-replica", "version": "1.0.0"}
    response = client.post(
        "/api/v1/simulations",
        json=payload,
        headers={**barista_headers, "Idempotency-Key": "board-free"},
    )
    assert response.status_code in {400, 422}
    assert "board-replica" in response.text


def test_the_v1_replica_keeps_the_profile_tracking_controller(client, barista_headers):
    """La v1 no expone la caldera: su réplica sigue con el controlador de antes."""
    extraction = _closed_extraction(client, barista_headers)
    created = client.post(
        f"/api/v1/extractions/{extraction['id']}/twin-comparison",
        json={"model": {"id": "espresso-detailed", "version": "1.0.0"}},
        headers={**barista_headers, "Idempotency-Key": "twin-v1"},
    )
    assert created.status_code == 202
    assert created.json()["controller"]["id"] == "profile-tracking"
    assert "commissioning" not in created.json()["controller"]


def test_replica_mode_rejects_an_extraction_that_is_not_closed(client, barista_headers):
    """Una preparación en curso no puede replicarse: aún no tiene duración ni cierre."""
    live = _running_extraction(client, barista_headers)
    response = client.post(
        f"/api/v1/extractions/{live['id']}/twin-comparison",
        json={},
        headers={**barista_headers, "Idempotency-Key": "twin-running"},
    )
    assert response.status_code == 404


def _approved_profile(client, headers, name="Perfil réplica"):
    profile = client.post(
        "/api/v1/profiles",
        json={
            "name": name,
            "coffeeIds": ["coffee-huila-001"],
            "specifications": [
                {"property": "temperature", "unit": "degC", "strategy": "target",
                 "target": 92, "tolerance": 1},
                {"property": "pressure", "unit": "bar", "strategy": "target",
                 "target": 9, "tolerance": 0.5},
            ],
        },
        headers=headers,
    ).json()
    client.post(f"/api/v1/profiles/{profile['id']}/approve", headers=headers)
    return profile


def _running_extraction(client, headers):
    profile = _approved_profile(client, headers, "Perfil en curso")
    return client.post(
        "/api/v1/extractions/live",
        json={"coffeeId": "coffee-huila-001", "profileId": profile["id"],
              "ambientTemperatureC": 22.5},
        headers=headers,
    ).json()


def _closed_extraction(client, headers, samples=12):
    """Preparación finalizada con muestras suficientes para compararla."""
    live = _running_extraction(client, headers)
    client.get(
        f"/api/v1/extractions/{live['id']}/live",
        headers={**headers, "X-Test-Elapsed-Ms": str((samples - 1) * 500)},
    )
    return client.post(
        f"/api/v1/extractions/{live['id']}/finish",
        json={"cause": "barista-decision"},
        headers=headers,
    ).json()


def test_traceability_now_walks_through_the_simulation_of_a_comparison(client, barista_headers):
    """La trazabilidad incorpora la réplica simulada, que antes quedaba fuera del recorrido."""
    extraction = _closed_extraction(client, barista_headers)
    client.post(
        f"/api/v1/extractions/{extraction['id']}/twin-comparison",
        json={},
        headers={**barista_headers, "Idempotency-Key": "twin-trace"},
    )

    def inline_executor(run_, model, store, progress, cancelled, _timeout):
        result = run_simulation(
            run_["simulationRunId"], model.artifact_path, run_["durationSeconds"],
            run_["profileSnapshot"], store, model.output_channels,
            model.data["capabilities"]["communicationStepSeconds"],
            adapter_factory=FakePlant, progress=progress, cancelled=cancelled,
            initial_conditions=run_.get("initialConditions"),
        )
        return {"artifact": result.artifact, "summary": result.summary}

    worker_module.process_next(
        client.app.state.repository,
        client.app.state.service.model_registry,
        client.app.state.service.series_store,
        "worker-trace", 30, executor=inline_executor,
    )

    trace = client.get(f"/api/v1/traceability/extraction/{extraction['id']}").json()
    simulation_nodes = [node for node in trace["nodes"] if node["kind"] == "simulation"]
    assert len(simulation_nodes) == 1
    node = simulation_nodes[0]
    assert node["relatedTo"] == f"extraction:{extraction['id']}"
    assert node["detailUrl"].startswith("/simulations/")
    assert node["summary"]


def test_the_published_graph_carries_the_comparison_and_its_comparability_link(
    client, barista_headers
):
    """La comparación llega al grafo semántico con su declaración de comparabilidad."""
    extraction = _closed_extraction(client, barista_headers)
    created = client.post(
        f"/api/v1/extractions/{extraction['id']}/twin-comparison",
        json={},
        headers={**barista_headers, "Idempotency-Key": "twin-semantic"},
    ).json()

    def inline_executor(run_, model, store, progress, cancelled, _timeout):
        result = run_simulation(
            run_["simulationRunId"], model.artifact_path, run_["durationSeconds"],
            run_["profileSnapshot"], store, model.output_channels,
            model.data["capabilities"]["communicationStepSeconds"],
            adapter_factory=FakePlant, progress=progress, cancelled=cancelled,
            initial_conditions=run_.get("initialConditions"),
        )
        return {"artifact": result.artifact, "summary": result.summary}

    worker_module.process_next(
        client.app.state.repository,
        client.app.state.service.model_registry,
        client.app.state.service.series_store,
        "worker-semantic-twin", 30, executor=inline_executor,
    )

    fuseki = FakeFuseki()
    assert semantic_worker.process_next(
        client.app.state.repository, client.app.state.semantic_manifest, fuseki
    )
    semantic = client.get(f"/api/v1/simulations/{created['simulationRunId']}/semantic").json()
    assert semantic["status"] == "published"
    assert semantic["publication"]["validation"]["conforms"] is True

    document = json.dumps(semantic["document"], ensure_ascii=False)
    assert "PhysicalVirtualComparison" in document
    assert "canBeComparedWith" in document
    assert extraction["id"] in document
    # el módulo vigente, que incluye la comparación desde 1.1.0
    assert semantic["publication"]["simulationModule"]["version"] == "1.1.4"


def test_schema_publication_waits_for_the_graph_store_instead_of_dying(client):
    """Una indisponibilidad momentánea de Fuseki no debe terminar el trabajador."""
    from espresso_semantics import FusekiError

    class FlakyFuseki:
        def __init__(self, failures):
            self.failures = failures
            self.graphs = {}

        def replace_graph(self, iri, graph):
            if self.failures:
                self.failures -= 1
                raise FusekiError("Fuseki no respondió correctamente")
            self.graphs[iri] = graph

    waits: list[float] = []
    fuseki = FlakyFuseki(failures=3)
    semantic_worker.publish_schema(
        client.app.state.semantic_manifest, fuseki, sleep=waits.append
    )
    assert len(fuseki.graphs) == 1
    assert waits == [1.0, 2.0, 5.0]          # la espera crece entre intentos
    # el grafo de esquema nombra la versión vigente del módulo
    assert "simulation-1.1.4" in next(iter(fuseki.graphs))

    agotado = FlakyFuseki(failures=99)
    with pytest.raises(FusekiError):
        semantic_worker.publish_schema(
            client.app.state.semantic_manifest, agotado, attempts=2, sleep=lambda _: None
        )


def test_semantic_worker_logs_the_extractions_it_skips_for_lacking_an_end(client, caplog):
    """Una preparación abierta sin causa que lo explique no se publica, pero tiene que constar."""
    repository = client.app.state.repository
    seeded = repository.get_document("extraction", "ext-20260904-001")
    repository.add_document(
        "extraction", "ext-sin-fin", {**seeded, "id": "ext-sin-fin", "finishedAt": None}
    )
    reported: set[tuple[str, str]] = set()
    with caplog.at_level("WARNING", logger="aplicacion_web.workers.semantic"):
        skipped = semantic_worker.report_skipped_extractions(repository, reported)
        semantic_worker.report_skipped_extractions(repository, reported)

    assert skipped == ["ext-sin-fin"]
    lines = [record.getMessage() for record in caplog.records]
    # Una sola vez por proceso, aunque el bucle vuelva a pasar por ella.
    assert len([line for line in lines if "ext-sin-fin" in line]) == 1
    assert "NO_END_TIME" in lines[0]


def test_a_preparation_cut_by_power_loss_is_published_as_incomplete(
    client, barista_headers
):
    """EKO 3.2.0: la cortada se publica con su causa y sin los instantes que no constan."""
    from rdflib import Graph, Namespace
    from test_api import _power_loss_message

    edt = Namespace("https://w3id.org/espresso-dt#")
    client.post(
        "/api/v1/sync",
        json={"deviceId": "device-test", "messages": [_power_loss_message()]},
        headers=barista_headers,
    )
    repository = client.app.state.repository
    # El perfil del dispositivo tiene que constar en la aplicación para proyectarla.
    seeded_profile = repository.get_document(
        "profile", repository.get_document("extraction", "ext-20260904-001")["profile"]["id"]
    )
    repository.add_document(
        "profile", "profile-placa-01", {**seeded_profile, "id": "profile-placa-01"}
    )
    # Ya no se cuenta entre las saltadas: su falta de fin está explicada.
    assert "prep-cortada-0001" not in repository.extractions_without_end()

    fuseki = FakeFuseki()
    _drain(repository, fuseki)

    publication = repository.get_document("extraction-semantic", "prep-cortada-0001")
    assert publication["status"] == "published"
    graph = Graph()
    for triple in fuseki.graphs[publication["namedGraphIri"]]:
        graph.add(triple)
    extraction = next(graph.subjects(edt.hasTerminationCause, edt.UnexpectedPowerLoss))
    # Nada inventado: ni fin, ni inicio, ni una serie que no tiene muestras.
    assert graph.value(extraction, edt.endTime) is None
    assert graph.value(extraction, edt.startTime) is None
    assert graph.value(extraction, edt.hasObservationSeries) is None


def _drain(repository, fuseki, reported=None, limit=20) -> int:
    """Corre el trabajador hasta que no quede nada que publicar; falla si no termina."""
    for turn in range(limit):
        if not semantic_worker.process_next_extraction(repository, fuseki, reported):
            return turn
    raise AssertionError("el trabajador no terminó: alguna extracción se reintenta sin fin")


def test_a_shacl_failure_is_rejected_with_its_report_and_not_retried(client, caplog):
    """Un fallo de las formas no cambia al reintentar: se rechaza y se dice por qué."""
    repository = client.app.state.repository
    seeded = repository.get_document("extraction", "ext-20260904-001")
    broken = {**seeded, "id": "ext-sin-inicio", "startedAt": None}
    repository.add_document("extraction", "ext-sin-inicio", broken)

    reported: set[tuple[str, str]] = set()
    with caplog.at_level("WARNING", logger="aplicacion_web.workers.semantic"):
        _drain(repository, FakeFuseki(), reported)

    publication = repository.get_document("extraction-semantic", "ext-sin-inicio")
    assert publication["status"] == "rejected"
    assert publication["reason"] == "SHACL_NONCONFORMANT"
    assert "Validation Report" in publication["report"]
    assert "startTime" in publication["report"]
    rejections = [r.getMessage() for r in caplog.records if "ext-sin-inicio" in r.getMessage()]
    assert len(rejections) == 1
    assert "SHACL_NONCONFORMANT" in rejections[0] and "Validation Report" in rejections[0]
    # Ya no vuelve: ni la selección la devuelve ni otra pasada la toca.
    assert repository.next_extraction_for_semantics() is None


def test_a_rejected_extraction_does_not_block_the_next_one(client):
    """Antes, la rechazada quedaba «pending» y se devolvía primero en cada vuelta."""
    repository = client.app.state.repository
    seeded = repository.get_document("extraction", "ext-20260904-001")
    repository.add_document(
        "extraction", "ext-sin-inicio", {**seeded, "id": "ext-sin-inicio", "startedAt": None}
    )
    repository.add_document(
        "extraction", "ext-valida-despues", {**seeded, "id": "ext-valida-despues"}
    )

    fuseki = FakeFuseki()
    _drain(repository, fuseki)

    assert repository.get_document("extraction-semantic", "ext-sin-inicio")["status"] == (
        "rejected"
    )
    published = repository.get_document("extraction-semantic", "ext-valida-despues")
    assert published["status"] == "published"
    assert published["namedGraphIri"] in fuseki.graphs


def test_a_store_failure_stays_pending_to_be_retried(client, caplog):
    """El almacén puede volver: un fallo de Fuseki sí se reintenta."""
    from espresso_semantics import FusekiError

    class UnavailableFuseki:
        def replace_graph(self, graph_name, graph):
            raise FusekiError("almacén no disponible")

    repository = client.app.state.repository
    with caplog.at_level("WARNING", logger="aplicacion_web.workers.semantic"):
        assert semantic_worker.process_next_extraction(repository, UnavailableFuseki(), set())

    pending = repository.list_documents("extraction-semantic", page=1, page_size=10).items
    assert [item["status"] for item in pending] == ["pending"]
    assert pending[0]["reason"] == "FusekiError"
    assert any("FusekiError" in r.getMessage() for r in caplog.records)
    # Cuando el almacén vuelve, se publica.
    fuseki = FakeFuseki()
    _drain(repository, fuseki)
    assert all(
        item["status"] == "published"
        for item in repository.list_documents("extraction-semantic", page=1, page_size=10).items
    )


def test_semantic_worker_logs_an_extraction_whose_profile_is_missing(client, caplog):
    repository = client.app.state.repository
    seeded = repository.get_document("extraction", "ext-20260904-001")
    orphan = {
        **seeded,
        "id": "ext-sin-perfil",
        "profile": {**seeded["profile"], "id": "perfil-inexistente"},
    }
    repository.add_document("extraction", "ext-sin-perfil", orphan)

    with caplog.at_level("WARNING", logger="aplicacion_web.workers.semantic"):
        for _ in range(6):
            semantic_worker.process_next_extraction(repository, FakeFuseki(), set())

    assert repository.get_document("extraction-semantic", "ext-sin-perfil")["status"] == "rejected"
    lines = [r.getMessage() for r in caplog.records if "ext-sin-perfil" in r.getMessage()]
    assert lines and "PROFILE_NOT_FOUND" in lines[0] and "perfil-inexistente" in lines[0]


def test_only_conforming_reasoner_conclusions_reach_the_graph_store(client):
    """Lo que el razonador concluyó se publica una vez y solo si cumplió las formas."""
    repository = client.app.state.repository
    turtle = (
        "@prefix edt: <https://w3id.org/espresso-dt#> .\n"
        "<https://w3id.org/espresso-dt/resource/extraction/prep-x> "
        "edt:hasRecommendedAction <urn:accion> .\n"
    )
    for identifier, conforms in (("rec-buena", True), ("rec-mala", False)):
        repository.add_document("recommendation", identifier, {
            "recommendationId": identifier,
            "origin": "reasoner",
            "conclusionGraph": turtle,
            "conclusionValidation": {"conforms": conforms, "report": None},
        })
    fuseki = FakeFuseki()
    while semantic_worker.process_next_recommendation(repository, fuseki):
        pass
    good = repository.get_document("recommendation-semantic", "rec-buena")
    bad = repository.get_document("recommendation-semantic", "rec-mala")
    assert good["status"] == "published"
    assert good["namedGraphIri"] in fuseki.graphs
    assert bad["status"] == "rejected"
    assert len(fuseki.graphs) == 1


def test_the_barista_decision_is_published_on_the_recommended_action(client):
    """Aceptar, cambiar o rechazar una recomendación llega al grafo (PC19)."""
    from rdflib import Graph, URIRef

    repository = client.app.state.repository
    action = "https://w3id.org/espresso-dt#InferredAction-prep-d"
    turtle = (
        "@prefix edt: <https://w3id.org/espresso-dt#> .\n"
        "<https://w3id.org/espresso-dt/resource/extraction/prep-d> "
        f"edt:hasRecommendedAction <{action}> .\n"
        f"<{action}> edt:description \"Acortar el tiempo\"@es .\n"
    )
    repository.add_document("recommendation", "rec-decidida", {
        "recommendationId": "rec-decidida", "origin": "reasoner", "summary": "Acortar el tiempo",
        "conclusionGraph": turtle, "conclusionValidation": {"conforms": True, "report": None},
    })
    fuseki = FakeFuseki()
    while semantic_worker.process_next_recommendation(repository, fuseki):
        pass
    name = semantic_worker.recommendation_graph_iri("rec-decidida")
    first = fuseki.graphs[name]
    first = first if isinstance(first, Graph) else Graph().parse(data=first, format="turtle")
    assert (URIRef(action), semantic_worker.EDT.hasValidationStatus, None) not in first

    repository.upsert_document("recommendation-decision", "rec-decidida", {
        "status": "modified", "decidedBy": "barista-1", "decidedAt": "2026-10-23T10:00:00Z",
        "comment": "Mejor un poco menos",
    })
    assert semantic_worker.process_next_recommendation(repository, fuseki)
    second = fuseki.graphs[name]
    second = second if isinstance(second, Graph) else Graph().parse(data=second, format="turtle")
    assert (URIRef(action), semantic_worker.EDT.hasValidationStatus,
            semantic_worker.EDT.Modified) in second
    assert not semantic_worker.process_next_recommendation(repository, fuseki)


def test_the_conformance_of_every_coffee_reaches_the_graph_store(client):
    """La última evaluación de cada café se publica, y una nueva la reemplaza (PC04, PC22)."""
    repository = client.app.state.repository
    turtle = (
        "@prefix edt: <https://w3id.org/espresso-dt#> .\n"
        "<urn:evaluacion> edt:assessesExtraction "
        "<https://w3id.org/espresso-dt/resource/extraction/prep-c> .\n"
    )
    for sequence in (1, 2):
        repository.add_document("conformity-assessment", f"prep-c:{sequence:04d}", {
            "id": f"prep-c:{sequence:04d}", "extractionId": "prep-c", "sequence": sequence,
            "conclusionGraph": turtle,
            "conclusionValidation": {"conforms": True, "report": None},
        })
    fuseki = FakeFuseki()
    while semantic_worker.process_next_conformity(repository, fuseki):
        pass
    published = repository.get_document("conformity-semantic", "prep-c")
    assert published["status"] == "published"
    assert published["sequence"] == 2
    assert semantic_worker.conformity_graph_iri("prep-c") in fuseki.graphs
    assert not semantic_worker.process_next_conformity(repository, fuseki)


def test_an_old_or_revised_publication_is_published_again(client):
    """Un grafo publicado con otra proyección, o antes de una revisión, se rehace."""
    repository = client.app.state.repository
    fuseki = FakeFuseki()
    _drain(repository, fuseki, limit=400)
    publication = repository.get_document("extraction-semantic", "ext-20260904-001")
    assert publication["projectionVersion"] == semantic_worker.PROJECTION_VERSION

    # Publicada con la proyección anterior: vuelve a publicarse una sola vez.
    repository.upsert_document(
        "extraction-semantic", "ext-20260904-001", {**publication, "projectionVersion": "1"}
    )
    assert _drain(repository, fuseki, limit=400) == 1

    # Una medida a mano registrada después de publicar también la rehace, y el
    # grafo nuevo la lleva.
    repository.add_document("extraction-revision", "extraction-revision-prueba", {
        "id": "extraction-revision-prueba",
        "resourceId": "ext-20260904-001",
        "category": "manual-measurements",
        "recordedBy": "barista-test",
        "recordedAt": "2999-01-01T00:00:00Z",
        "changes": {"manualMeasurements": {"coffeeDoseG": 13.0, "beverageMassG": 31.0,
                                           "recordedAt": "2999-01-01T00:00:00Z",
                                           "recordedBy": "barista-test"}},
    })
    assert _drain(repository, fuseki, limit=400) == 1
    graph = fuseki.graphs[publication["namedGraphIri"]]
    assert any("31" in str(value) for value in graph.objects())


def test_a_preparation_from_the_device_can_be_replicated(client, barista_headers):
    """La que llega de la placa no lleva `status`: su final es `finishedAt`."""
    extraction = _closed_extraction(client, barista_headers)
    repository = client.app.state.repository
    device = {key: value for key, value in repository.get_document(
        "extraction", extraction["id"]).items() if key != "status"}
    device["finishedAt"] = device.get("finishedAt") or "2026-10-20T14:58:25.940Z"
    repository.upsert_document("extraction", extraction["id"], device)
    created = client.post(
        f"/api/v1/extractions/{extraction['id']}/twin-comparison",
        json={},
        headers={**barista_headers, "Idempotency-Key": "twin-device"},
    )
    assert created.status_code == 202
