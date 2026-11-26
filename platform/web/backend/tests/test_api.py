from datetime import UTC, datetime
from pathlib import Path

import pytest


def test_health_and_seeded_history(client):
    assert client.get("/health/live").json()["status"] == "alive"
    assert client.get("/health/ready").json()["status"] == "ready"
    response = client.get("/api/v1/extractions?page=1")
    assert response.status_code == 200
    assert response.json()["total"] == 3
    assert any(item["source"] == "simulation" for item in response.json()["items"])


def test_simulation_contract_is_idempotent_and_hides_fixture(client, barista_headers):
    models = client.get("/api/v1/simulation-models").json()["items"]
    assert [model["id"] for model in models] == ["espresso-detailed"]
    request = {
        "schemaVersion": "1.0",
        "model": {"id": "espresso-detailed", "version": "1.0.0"},
        "profileId": "espresso-lab",
        "durationSeconds": 30.0,
        "controller": {"id": "profile-tracking", "version": "1.0.0"},
    }
    headers = {**barista_headers, "Idempotency-Key": "simulation-test-001"}
    first = client.post("/api/v1/simulations", json=request, headers=headers)
    second = client.post("/api/v1/simulations", json=request, headers=headers)
    assert first.status_code == second.status_code == 202
    assert first.json()["simulationRunId"] == second.json()["simulationRunId"]
    assert first.json()["status"] == "queued"
    assert first.json()["profileSnapshot"]["resolution"] == {
        "temperature": "target-direct",
        "pressure": "target-direct",
    }
    assert client.get("/api/v1/simulations").json()["total"] == 1

    conflict = client.post(
        "/api/v1/simulations",
        json={**request, "durationSeconds": 40.0},
        headers=headers,
    )
    assert conflict.status_code == 409


def test_simulation_rejects_fixture_and_requires_writer_and_idempotency(client, barista_headers):
    request = {
        "schemaVersion": "1.0",
        "model": {"id": "espresso-reduced-fixture", "version": "1.0.0"},
        "profileId": "espresso-lab",
        "durationSeconds": 30.0,
        "controller": {"id": "profile-tracking", "version": "1.0.0"},
    }
    assert client.post("/api/v1/simulations", json=request).status_code == 403
    assert (
        client.post("/api/v1/simulations", json=request, headers=barista_headers).status_code == 422
    )
    rejected = client.post(
        "/api/v1/simulations",
        json=request,
        headers={**barista_headers, "Idempotency-Key": "simulation-fixture"},
    )
    assert rejected.status_code == 400


def test_queued_simulation_can_be_cancelled_by_requester(client, barista_headers):
    request = {
        "schemaVersion": "1.0",
        "model": {"id": "espresso-detailed", "version": "1.0.0"},
        "profileId": "espresso-lab",
        "durationSeconds": 30.0,
        "controller": {"id": "profile-tracking", "version": "1.0.0"},
    }
    created = client.post(
        "/api/v1/simulations",
        json=request,
        headers={**barista_headers, "Idempotency-Key": "simulation-cancel"},
    ).json()
    cancelled = client.post(
        f"/api/v1/simulations/{created['simulationRunId']}/cancel",
        headers=barista_headers,
    )
    assert cancelled.status_code == 200
    assert cancelled.json()["status"] == "cancelled"
    assert client.get(f"/api/v1/simulations/{created['simulationRunId']}/series").status_code == 404


def test_history_filters_joined_context_and_returns_a_complete_record(client):
    response = client.get(
        "/api/v1/extractions",
        params={
            "page": 1,
            "from": "2026-09-04T14:00:00Z",
            "to": "2026-09-04T15:00:00Z",
            "barista": "barista-lab-01",
            "coffeeId": "coffee-huila-001",
            "variety": "Caturra",
            "process": "Lavado",
            "roast": "Medio claro",
            "profileId": "espresso-lab",
            "profileVersion": 3,
            "machineId": "espresso-lab-01",
            "grinderId": "molino-lab-01",
            "calibrationId": "calibracion-turno-01",
            "technicalConformance": "insufficient-information",
            "sensoryAcceptance": "accepted",
            "result": "manual-normal-stop",
        },
    )
    assert response.status_code == 200
    assert [item["id"] for item in response.json()["items"]] == ["ext-20260904-001"]
    assert response.json()["items"][0]["history"]["variety"] == "Caturra"
    defect_response = client.get(
        "/api/v1/extractions", params={"page": 1, "defect": "ExcessiveAcidity"}
    )
    assert [item["id"] for item in defect_response.json()["items"]] == ["ext-20260904-002"]

    record = client.get("/api/v1/extractions/ext-20260904-001/history-record").json()
    assert record["extraction"]["profile"]["version"] == 3
    assert record["coffee"]["lotIdentifier"] == "HL-24-07"
    assert len(record["series"]["samples"]) == 3
    assert record["recommendations"][0]["recommendationId"] == "rec-demo-grind-001"
    assert record["episodes"][0]["id"] == "episode-001"
    assert record["relatedExtractionIds"] == ["ext-20260904-002"]


def test_history_paginates_without_embedding_series_samples(client):
    repository = client.app.state.repository
    template = repository.get_document("extraction", "ext-20260904-001")
    assert template is not None
    for index in range(105):
        identifier = f"history-rnf08-{index:03d}"
        repository.add_document(
            "extraction",
            identifier,
            {
                **template,
                "id": identifier,
                "finishedAt": f"2026-08-{(index % 28) + 1:02d}T12:00:00Z",
                "seriesRef": f"series://{identifier}",
            },
        )

    response = client.get("/api/v1/extractions?page=6&page_size=20")
    assert response.status_code == 200
    page = response.json()
    assert page["total"] == 108
    assert page["page"] == 6
    assert page["pages"] == 6
    assert len(page["items"]) == 8
    assert all("samples" not in item for item in page["items"])
    assert all(item.get("seriesRef") for item in page["items"])

    filtered = client.get(
        "/api/v1/extractions",
        params={"page": 1, "page_size": 10, "query": "history-rnf08-104"},
    ).json()
    assert filtered["total"] == 1
    assert filtered["items"][0]["id"] == "history-rnf08-104"
    assert "samples" not in filtered["items"][0]


def test_dashboard_summarizes_context_sensors_pending_work_and_recent_activity(client):
    response = client.get("/api/v1/dashboard")
    assert response.status_code == 200
    dashboard = response.json()
    assert dashboard["activeProfile"]["id"] == "espresso-lab"
    assert dashboard["activeProfile"]["name"] == "Espresso laboratorio"
    assert dashboard["activeCoffee"]["id"] == "coffee-huila-001"
    assert dashboard["activeEquipment"] == {
        "machineId": "espresso-lab-01",
        "grinderId": "molino-lab-01",
        "calibrationId": "calibracion-turno-01",
    }
    assert {sensor["id"] for sensor in dashboard["sensors"]} == {"temperature", "pressure"}
    assert all(sensor["status"] == "available" for sensor in dashboard["sensors"])
    assert dashboard["lastCalibration"] is None
    assert {action["kind"] for action in dashboard["pendingActions"]} >= {
        "coffee",
        "measurements",
        "sensory",
        "calibration",
    }
    assert len(dashboard["recentExtractions"]) == 3


def test_barista_can_login_use_session_and_logout(client):
    _, stored_hash = client.app.state.repository.user_credentials("barista")
    assert stored_hash != "espresso-barista"
    assert stored_hash.startswith("$argon2")
    login = client.post(
        "/api/v1/auth/login",
        json={"username": "barista", "password": "espresso-barista"},
    )
    assert login.status_code == 200
    assert login.json() == {
        "id": "user-barista",
        "username": "barista",
        "displayName": "Barista de laboratorio",
        "role": "barista",
    }
    cookie = login.headers["set-cookie"]
    assert "HttpOnly" in cookie
    assert "SameSite=strict" in cookie
    assert client.get("/api/v1/auth/session").json()["id"] == "user-barista"

    created = client.post(
        "/api/v1/coffees",
        json={"id": "coffee-session", "commercialReference": "Sesión"},
    )
    assert created.status_code == 201
    assert created.json()["provenance"]["actor"] == "user-barista"

    assert client.post("/api/v1/auth/logout", json={}).status_code == 204
    assert client.get("/api/v1/auth/session").status_code == 401


def test_login_rejects_invalid_credentials_without_revealing_the_field(client):
    response = client.post(
        "/api/v1/auth/login",
        json={"username": "barista", "password": "incorrecta"},
    )
    assert response.status_code == 401
    assert response.json()["error"]["code"] == "unauthorized"
    assert response.json()["error"]["message"] == "Usuario o contraseña incorrectos."


def test_coffee_accepts_one_identifier_and_preserves_information_states(client, barista_headers):
    response = client.post(
        "/api/v1/coffees",
        headers=barista_headers,
        json={
            "bagIdentifier": "BOLSA-RF03-01",
            "commercialReference": None,
            "lotIdentifier": None,
            "variety": None,
            "notApplicableFields": ["postHarvestProcess", "altitudeM"],
            "mineralComposition": "Cenizas declaradas: 4 %",
            "mineralSource": "Ficha del productor",
        },
    )
    assert response.status_code == 201
    coffee = response.json()
    assert coffee["id"].startswith("coffee-")
    assert coffee["variety"] is None
    assert coffee["notApplicableFields"] == ["postHarvestProcess", "altitudeM"]
    assert coffee["provenance"]["actor"] == "barista-test"


def test_coffee_requires_an_operational_identifier_and_mineral_source(client, barista_headers):
    no_identifier = client.post(
        "/api/v1/coffees",
        headers=barista_headers,
        json={"commercialReference": None, "lotIdentifier": None, "bagIdentifier": None},
    )
    no_source = client.post(
        "/api/v1/coffees",
        headers=barista_headers,
        json={"lotIdentifier": "LOTE-RF03", "mineralComposition": "Volcánica"},
    )
    assert no_identifier.status_code == 422
    assert no_source.status_code == 422


def test_validation_errors_are_actionable_and_do_not_replace_missing_values(
    client, barista_headers
):
    invalid = client.post(
        "/api/v1/coffees",
        headers=barista_headers,
        json={"lotIdentifier": "LOTE-RNF04", "altitudeM": 7000},
    )
    assert invalid.status_code == 422
    error = invalid.json()["error"]
    assert error["message"] == ("Revisa altitud: No puede superar el límite indicado.")
    assert error["fields"] == [
        {
            "field": "altitudeM",
            "label": "altitud",
            "message": "No puede superar el límite indicado.",
        }
    ]
    assert "Input" not in invalid.text

    created = client.post(
        "/api/v1/coffees",
        headers=barista_headers,
        json={"lotIdentifier": "LOTE-RNF04"},
    )
    assert created.status_code == 201
    assert created.json()["altitudeM"] is None
    assert created.json()["roastDate"] is None


def test_coffee_identifiers_are_reusable_but_not_duplicated(client, barista_headers):
    payload = {"lotIdentifier": "LOTE-UNICO-RF03"}
    first = client.post("/api/v1/coffees", headers=barista_headers, json=payload)
    second = client.post("/api/v1/coffees", headers=barista_headers, json=payload)
    found = client.get("/api/v1/coffees?page=1&query=LOTE-UNICO-RF03")
    assert first.status_code == 201
    assert second.status_code == 409
    assert found.json()["total"] == 1


def profile_payload(name: str = "Perfil Huila") -> dict:
    return {
        "name": name,
        "coffeeIds": ["coffee-huila-001"],
        "specifications": [
            {
                "property": "water-temperature",
                "unit": "°C",
                "strategy": "target",
                "target": 92,
                "tolerance": 1,
            },
            {
                "property": "pressure",
                "unit": "bar",
                "strategy": "range",
                "minimum": 8.5,
                "maximum": 9.5,
            },
        ],
    }


def test_profile_lifecycle_preserves_approved_version(client, barista_headers):
    created = client.post("/api/v1/profiles", json=profile_payload(), headers=barista_headers)
    assert created.status_code == 201
    version_one = created.json()
    assert version_one["status"] == "draft"
    assert version_one["version"] == 1
    assert version_one["coffeeIds"] == ["coffee-huila-001"]

    approved = client.post(f"/api/v1/profiles/{version_one['id']}/approve", headers=barista_headers)
    assert approved.status_code == 200
    assert approved.json()["status"] == "approved"

    changed = profile_payload("Perfil Huila ajustado")
    changed["specifications"][0]["target"] = 91.5
    versioned = client.post(
        f"/api/v1/profiles/{version_one['id']}/versions",
        json=changed,
        headers=barista_headers,
    )
    assert versioned.status_code == 201
    version_two = versioned.json()
    assert version_two["version"] == 2
    assert version_two["status"] == "draft"
    assert version_two["previousVersionId"] == version_one["id"]

    original = client.get(f"/api/v1/profiles/{version_one['id']}").json()
    assert original["name"] == "Perfil Huila"
    assert original["specifications"][0]["target"] == 92
    immutable = client.put(
        f"/api/v1/profiles/{version_one['id']}",
        json=changed,
        headers=barista_headers,
    )
    assert immutable.status_code == 409


def test_profile_comparison_highlights_version_changes_and_evidence(client, barista_headers):
    first = client.post("/api/v1/profiles", json=profile_payload(), headers=barista_headers).json()
    client.put(
        f"/api/v1/profiles/{first['id']}/sensory-target",
        headers=barista_headers,
        json={
            "coffeeId": "coffee-huila-001",
            "rationale": "Buscar dulzor y balance.",
            "descriptors": ["Panela", "Chocolate"],
            "desiredChanges": [{"attribute": "Sweetness", "direction": "Increase"}],
        },
    )
    client.post(f"/api/v1/profiles/{first['id']}/approve", headers=barista_headers)
    changed = profile_payload("Perfil Huila refinado")
    changed["specifications"][0]["target"] = 91.5
    second = client.post(
        f"/api/v1/profiles/{first['id']}/versions",
        json=changed,
        headers=barista_headers,
    ).json()
    calibration = client.post(
        "/api/v1/calibrations",
        json={"coffeeId": "coffee-huila-001", "profileId": first["id"], "context": {}},
        headers=barista_headers,
    ).json()
    client.post(
        f"/api/v1/calibrations/{calibration['id']}/tests",
        json={"extractionId": "ext-20260904-001"},
        headers=barista_headers,
    )
    client.post(
        f"/api/v1/calibrations/{calibration['id']}/close",
        json={"result": "validated-profile", "rationale": "Resultado consistente."},
        headers=barista_headers,
    )

    response = client.post(
        "/api/v1/profiles/compare",
        json={"ids": [first["id"], second["id"]]},
        headers=barista_headers,
    )
    assert response.status_code == 200
    comparison = response.json()
    assert [item["version"] for item in comparison["items"]] == [1, 2]
    assert comparison["items"][0]["author"] == "barista-test"
    assert comparison["items"][0]["applicableCoffees"][0]["name"] == "Huila · Microlote 24"
    assert comparison["items"][0]["validationEvidence"][0]["id"] == calibration["id"]
    assert comparison["items"][0]["validationEvidence"][0]["testExtractionIds"] == [
        "ext-20260904-001"
    ]
    changed_by_key = {item["key"]: item for item in comparison["changes"]}
    assert changed_by_key["water-temperature.target"]["values"] == [92, 91.5]
    assert changed_by_key["water-temperature.target"]["status"] == "different"
    assert changed_by_key["pressure.minimum"]["status"] == "equal"
    assert "water-temperature.target" in comparison["changedKeys"]

    duplicate = client.post(
        "/api/v1/profiles/compare",
        json={"ids": [first["id"], first["id"]]},
        headers=barista_headers,
    )
    assert duplicate.status_code == 422


def test_traceability_reconstructs_the_navigable_record(client, barista_headers):
    client.post(
        "/api/v1/recommendations/rec-demo-grind-001/explanation",
        json={},
        headers=barista_headers,
    )
    client.post(
        "/api/v1/recommendations/rec-demo-grind-001/decision",
        json={"status": "accepted", "comment": "Probar el ajuste."},
        headers=barista_headers,
    )
    response = client.get(
        "/api/v1/traceability/extraction/ext-20260904-001",
        headers=barista_headers,
    )
    assert response.status_code == 200
    record = response.json()
    kinds = {node["kind"] for node in record["nodes"]}
    assert {
        "profile",
        "extraction",
        "observation",
        "evaluation",
        "diagnosis",
        "explanation",
        "recommendation",
        "decision",
        "adjustment",
        "result",
    } <= kinds
    assert any(node["id"] == "extraction:ext-20260904-002" for node in record["nodes"])
    assert all(node["detailUrl"].startswith("/") for node in record["nodes"])

    profile_record = client.get(
        "/api/v1/traceability/profile/espresso-lab", headers=barista_headers
    ).json()
    assert any(node["kind"] == "extraction" for node in profile_record["nodes"])
    barista_record = client.get(
        "/api/v1/traceability/barista/barista-lab-01", headers=barista_headers
    ).json()
    assert any(node["kind"] == "adjustment" for node in barista_record["nodes"])


def test_complete_and_semantic_exports_keep_series_external(client, barista_headers):
    readable = client.get("/api/v1/extractions/ext-20260904-002/export", headers=barista_headers)
    assert readable.status_code == 200
    assert "attachment" in readable.headers["content-disposition"]
    record = readable.json()["record"]
    assert record["extraction"]["profile"]["version"] == 3
    assert record["coffee"]["lotIdentifier"] == "HL-24-07"
    assert record["series"]["samples"]

    semantic = client.get("/api/v1/extractions/ext-20260904-002/semantic", headers=barista_headers)
    assert semantic.status_code == 200, semantic.text
    document = semantic.json()
    assert document["validation"] == {
        "conforms": True,
        "shapes": "espresso-dt-shapes.ttl",
    }
    assert document["resourceIri"] == (
        "https://w3id.org/espresso-dt/resource/extraction/ext-20260904-002"
    )
    assert semantic.headers["content-type"].startswith("application/ld+json")
    serialized = str(document["@graph"])
    assert "https://w3id.org/espresso-dt#dataReference" in serialized
    assert "ext-20260904-002-sample-00000" not in serialized


def test_semantic_validation_failure_is_explained_without_a_technical_report(
    client, barista_headers, monkeypatch
):
    from espresso_semantics import extraction as semantic_export

    monkeypatch.setattr(
        semantic_export,
        "validate",
        lambda *args, **kwargs: (False, None, "internal validation report"),
    )
    response = client.get("/api/v1/extractions/ext-20260904-002/semantic", headers=barista_headers)
    assert response.status_code == 400
    assert response.json()["error"]["message"] == (
        "No se pudo comprobar la integridad de este registro. "
        "Revisa que tenga perfil, responsable, fechas y unidades."
    )
    assert "internal validation report" not in response.text


def test_equipment_catalog_declares_sensors_and_capabilities(client, barista_headers):
    created = client.post(
        "/api/v1/equipment",
        headers=barista_headers,
        json={
            "id": "machine-rf19",
            "type": "machine",
            "name": "Máquina de pruebas",
            "location": "Barra dos",
            "capabilities": [
                {
                    "property": "pressure",
                    "manualAdjustment": True,
                    "automaticAdjustment": False,
                }
            ],
            "sensors": [
                {
                    "id": "pressure-rf19",
                    "measuredProperty": "pressure",
                    "location": "Grupo uno",
                    "accuracy": 0.1,
                    "accuracyUnit": "bar",
                    "samplingFrequencyHz": 10,
                    "lastCalibrationAt": "2026-09-01T12:00:00Z",
                }
            ],
        },
    )
    assert created.status_code == 201
    assert created.json()["provenance"]["actor"] == "barista-test"
    catalog = client.get("/api/v1/equipment", headers=barista_headers).json()
    assert any(item["id"] == "machine-rf19" for item in catalog["items"])
    grinder = client.get("/api/v1/equipment/molino-lab-01", headers=barista_headers).json()
    assert grinder["capabilities"] == []

    undeclared = client.post(
        "/api/v1/equipment",
        headers=barista_headers,
        json={
            "id": "invalid-rf19",
            "type": "grinder",
            "name": "Molino inválido",
            "location": "Barra",
            "capabilities": [
                {
                    "property": "grind-setting",
                    "manualAdjustment": False,
                    "automaticAdjustment": False,
                }
            ],
            "sensors": [],
        },
    )
    assert undeclared.status_code == 422


def test_profile_rejects_a_water_temperature_the_machine_cannot_reach(client, barista_headers):
    """El perfil pide agua; la placa solo alcanza 85,72–92,97 °C (acta v12)."""
    for water in (85.7, 93.0):
        payload = profile_payload()
        payload["specifications"][0]["target"] = water
        response = client.post("/api/v1/profiles", json=payload, headers=barista_headers)
        assert response.status_code == 400
        assert "temperatura del agua" in response.text
    for water in (85.72, 92.97):
        payload = profile_payload()
        payload["specifications"][0]["target"] = water
        assert client.post(
            "/api/v1/profiles", json=payload, headers=barista_headers
        ).status_code == 201


def test_profile_rejects_invalid_or_mixed_specifications(client, barista_headers):
    invalid_range = profile_payload()
    invalid_range["specifications"] = [
        {
            "property": "pressure",
            "unit": "bar",
            "strategy": "range",
            "minimum": 10,
            "maximum": 8,
        }
    ]
    response = client.post("/api/v1/profiles", json=invalid_range, headers=barista_headers)
    assert response.status_code == 422

    mixed = profile_payload()
    mixed["specifications"] = [
        {
            "property": "water-temperature",
            "unit": "°C",
            "strategy": "target",
            "target": 92,
            "minimum": 92,
            "maximum": 94,
        }
    ]
    response = client.post("/api/v1/profiles", json=mixed, headers=barista_headers)
    assert response.status_code == 422


def test_profile_can_be_approved_and_retired(client, barista_headers):
    profile = client.post(
        "/api/v1/profiles", json=profile_payload(), headers=barista_headers
    ).json()
    client.post(f"/api/v1/profiles/{profile['id']}/approve", headers=barista_headers)
    retired = client.post(f"/api/v1/profiles/{profile['id']}/retire", headers=barista_headers)
    assert retired.status_code == 200
    assert retired.json()["status"] == "retired"


def test_legacy_profile_proposal_contract_is_preserved(client, barista_headers):
    response = client.post(
        "/api/v1/profiles",
        headers=barista_headers,
        json={
            "id": "legacy-profile",
            "name": "Perfil heredado",
            "version": 4,
            "temperatureSetpointC": 93,
            "temperatureToleranceC": 1,
            "pressureSetpointBar": 9,
            "pressureToleranceBar": 0.5,
            "maximumTimeMs": 30000,
        },
    )
    assert response.status_code == 201
    assert response.json()["id"] == "legacy-profile"
    assert response.json()["status"] == "proposed"


def test_declared_profile_and_barista_target_remain_distinct(client, barista_headers):
    declared = client.put(
        "/api/v1/coffees/coffee-huila-001/declared-sensory-profile",
        headers=barista_headers,
        json={
            "declaredBy": "Tostador Huila",
            "source": "Etiqueta del lote",
            "originalText": "Panela y cítrico",
            "descriptors": ["Panela", "Citrus"],
        },
    )
    assert declared.status_code == 200
    assert declared.json()["declaredSensoryProfile"]["declaredBy"] == "Tostador Huila"

    profile = client.post(
        "/api/v1/profiles", json=profile_payload(), headers=barista_headers
    ).json()
    target = client.put(
        f"/api/v1/profiles/{profile['id']}/sensory-target",
        headers=barista_headers,
        json={
            "coffeeId": "coffee-huila-001",
            "rationale": "Buscar mayor dulzor y mejor balance.",
            "descriptors": ["Panela", "Chocolate"],
            "desiredChanges": [
                {"attribute": "Sweetness", "direction": "Increase"},
                {"attribute": "Balance", "direction": "Improve"},
            ],
        },
    )
    assert target.status_code == 200
    sensory_target = target.json()["sensoryTarget"]
    assert sensory_target["definedBy"] == "barista-test"
    assert sensory_target["coffeeId"] == "coffee-huila-001"
    assert sensory_target["desiredChanges"][1] == {
        "attribute": "Balance",
        "direction": "Improve",
    }
    coffee = client.get("/api/v1/coffees/coffee-huila-001").json()
    assert coffee["declaredSensoryProfile"]["originalText"] == "Panela y cítrico"


def test_sensory_target_requires_an_applicable_coffee_and_draft(client, barista_headers):
    other_coffee = client.post(
        "/api/v1/coffees",
        headers=barista_headers,
        json={"commercialReference": "Otro café RF05"},
    ).json()
    profile = client.post(
        "/api/v1/profiles", json=profile_payload(), headers=barista_headers
    ).json()
    payload = {
        "coffeeId": other_coffee["id"],
        "rationale": "Objetivo incompatible para la prueba.",
        "descriptors": ["Chocolate"],
        "desiredChanges": [],
    }
    invalid = client.put(
        f"/api/v1/profiles/{profile['id']}/sensory-target",
        headers=barista_headers,
        json=payload,
    )
    assert invalid.status_code == 400

    client.post(f"/api/v1/profiles/{profile['id']}/approve", headers=barista_headers)
    payload["coffeeId"] = "coffee-huila-001"
    immutable = client.put(
        f"/api/v1/profiles/{profile['id']}/sensory-target",
        headers=barista_headers,
        json=payload,
    )
    assert immutable.status_code == 409


def test_the_sensory_target_needs_descriptors_or_changes_but_not_both(
    client, barista_headers
):
    """Un barista que solo quiere menos amargor no inventa un descriptor."""
    profile = client.post(
        "/api/v1/profiles", json=profile_payload(), headers=barista_headers
    ).json()
    path = f"/api/v1/profiles/{profile['id']}/sensory-target"
    base = {"coffeeId": "coffee-huila-001", "rationale": "Menos amargo."}
    only_changes = client.put(path, headers=barista_headers, json={
        **base, "desiredChanges": [{"attribute": "Bitterness", "direction": "Decrease"}],
    })
    assert only_changes.status_code == 200
    assert only_changes.json()["sensoryTarget"]["descriptors"] == []
    empty = client.put(path, headers=barista_headers, json={**base, "descriptors": []})
    assert empty.status_code == 422
    written = client.put(path, headers=barista_headers, json={
        **base, "otherDescriptors": [" Azúcar morena "],
    })
    assert written.status_code == 200
    assert written.json()["sensoryTarget"]["otherDescriptors"] == ["Azúcar morena"]


def test_legacy_list_shape_is_preserved(client):
    response = client.get("/api/v1/extractions")
    assert response.status_code == 200
    assert list(response.json()) == ["items"]
    assert isinstance(response.json()["items"], list)


def test_sync_is_idempotent(client, barista_headers):
    message = {
        "schemaVersion": "1.0",
        "messageId": "message-telemetry-0001",
        "messageType": "telemetry.sample",
        "deviceId": "device-test",
        "sequence": 1,
        "occurredAt": "2026-09-05T10:00:00Z",
        "payload": {
            "source": "measured",
            "state": "idle",
            "elapsedMs": 0,
            "measurements": {"temperatureC": 90.0, "pressureBar": 0.0},
            "quality": {"temperature": "valid", "pressure": "valid"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": False, "pumpDutyPercent": 0, "virtual": False},
        },
    }
    batch = {"deviceId": "device-test", "messages": [message]}
    first = client.post("/api/v1/sync", json=batch, headers=barista_headers)
    second = client.post("/api/v1/sync", json=batch, headers=barista_headers)
    assert first.json()["inserted"] == 1
    assert second.json()["duplicates"] == 1


def test_sync_projects_device_contract_telemetry_for_dashboard(client, barista_headers):
    message = {
        "schemaVersion": "1.0",
        "messageId": "message-device-contract-0001",
        "messageType": "telemetry.sample",
        "deviceId": "device-contract-test",
        "sequence": 1,
        "occurredAt": "2026-09-05T10:00:00Z",
        "payload": {
            "source": "simulation",
            "state": "extraction",
            "elapsedMs": 100,
            "temperature": {"value": 92.5, "unit": "Cel", "quality": "simulated"},
            "pressure": {"value": 8.8, "unit": "bar", "quality": "simulated"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": True, "pumpDutyPercent": 75, "virtual": True},
        },
    }
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": message["deviceId"], "messages": [message]},
        headers=barista_headers,
    )
    assert response.status_code == 200
    dashboard = client.get("/api/v1/dashboard", headers=barista_headers).json()
    assert dashboard["sensors"][0]["value"] == 92.5
    assert dashboard["sensors"][1]["value"] == 8.8


def test_sync_stores_device_samples_as_the_measured_series(client, barista_headers):
    """Las muestras del dispositivo deben quedar donde el comparador las busca.

    El comparador físico-virtual lee la serie de la preparación. Si las muestras
    del dispositivo se archivaran aparte, la comparación quedaría vacía aunque la
    cafetera estuviera enviando datos.
    """
    message = {
        "schemaVersion": "1.0",
        "messageId": "message-measured-series-0001",
        "messageType": "telemetry.sample",
        "deviceId": "device-contract-test",
        "sequence": 7,
        "occurredAt": "2026-09-05T10:00:01Z",
        "payload": {
            "source": "measured",
            "preparationId": "extraction-measured-001",
            "state": "extraction",
            "elapsedMs": 1500,
            "temperature": {"value": 92.4, "unit": "Cel", "quality": "valid"},
            "pressure": {"value": 9.1, "unit": "bar", "quality": "valid"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": True, "pumpDutyPercent": 80, "virtual": False},
        },
    }
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": message["deviceId"], "messages": [message]},
        headers=barista_headers,
    )
    assert response.status_code == 200

    samples = client.app.state.repository.list_documents(
        "live-sample", page=1, page_size=10, filters={"extractionId": "extraction-measured-001"}
    ).items
    assert len(samples) == 1
    sample = samples[0]
    assert sample["sequence"] == 7
    assert sample["temperatureC"] == 92.4
    assert sample["pressureBar"] == 9.1
    assert sample["quality"] == "valid"
    assert sample["source"] == "measured"


def _series_chunk(chunk_index, chunk_count, samples, preparation="extraction-series-001"):
    return {
        "schemaVersion": "1.0",
        "messageId": f"series-{preparation}-{chunk_index:03d}",
        "messageType": "extraction.series",
        "deviceId": "device-contract-test",
        "sequence": 40 + chunk_index,
        "occurredAt": "2026-09-05T10:00:30Z",
        "payload": {
            "preparationId": preparation,
            "source": "measured",
            "chunkIndex": chunk_index,
            "chunkCount": chunk_count,
            "intervalMs": 500,
            "samples": samples,
        },
    }


def _sample(elapsed_ms, temperature=92.4, pressure=9.1, quality="valid"):
    return {
        "elapsedMs": elapsed_ms,
        "temperature": {"value": temperature, "quality": quality},
        "pressure": {"value": pressure, "quality": quality},
    }


def test_an_incomplete_configuration_is_refused_where_it_can_be_fixed(
    client, barista_headers
):
    """Activar algo que la máquina va a rechazar no es activar nada.

    El dispositivo sigue siendo la autoridad sobre sus límites y conserva su
    veto. Lo que se comprueba aquí es otra cosa: que la configuración esté
    completa, en el único momento en que el barista puede corregirlo. Sin ello
    la activación se daba por buena, el dispositivo rechazaba con causa y esa
    causa aparecía solo en su consola de diagnóstico, que en la instalación
    definitiva nadie mira.
    """
    # Temperatura y presión, sin tiempo máximo: el dispositivo no sabría cuándo
    # cortar la extracción.
    sin_tiempo = client.post(
        "/api/v1/profiles", json=profile_payload("Sin tiempo máximo"), headers=barista_headers
    )
    assert sin_tiempo.status_code == 201
    profile_id = sin_tiempo.json()["id"]
    assert (
        client.post(f"/api/v1/profiles/{profile_id}/approve", headers=barista_headers).status_code
        == 200
    )

    rechazo = client.post(
        f"/api/v1/profiles/{profile_id}/activate",
        json={"via": "selection"},
        headers=barista_headers,
    )
    assert rechazo.status_code == 400
    # El mensaje nombra lo que falta: el barista no tiene que adivinarlo.
    assert "tiempo máximo" in rechazo.json()["error"]["message"]
    # Y no quedó activada a medias.
    assert (
        client.get(f"/api/v1/profiles/{profile_id}", headers=barista_headers).json()["status"]
        == "approved"
    )

    # La misma configuración con el tiempo declarado sí rige.
    completa = profile_payload("Con tiempo máximo")
    completa["specifications"].append(
        {"property": "extraction-time", "unit": "s", "strategy": "range",
         "minimum": 20.0, "maximum": 28.0}
    )
    creada = client.post("/api/v1/profiles", json=completa, headers=barista_headers)
    assert creada.status_code == 201
    completa_id = creada.json()["id"]
    client.post(f"/api/v1/profiles/{completa_id}/approve", headers=barista_headers)
    activada = client.post(
        f"/api/v1/profiles/{completa_id}/activate",
        json={"via": "selection"},
        headers=barista_headers,
    )
    assert activada.status_code == 200
    assert activada.json()["status"] == "accepted"


def test_the_series_of_a_preparation_survives_a_link_outage(client, barista_headers):
    """La serie medida llega al cerrar la preparación, no solo en vivo.

    Una preparación hecha sin enlace no emite telemetría, porque la telemetría
    no se retiene. Su serie viaja aparte, desde la cola persistente, y sin ella
    la preparación quedaría archivada y fuera de la comparación físico-virtual.
    """
    mensaje = _series_chunk(0, 2, [_sample(0), _sample(500)])
    respuesta = client.post(
        "/api/v1/sync",
        json={"deviceId": mensaje["deviceId"], "messages": [mensaje]},
        headers=barista_headers,
    )
    assert respuesta.status_code == 200

    muestras = client.app.state.repository.list_documents(
        "live-sample", page=1, page_size=50, filters={"extractionId": "extraction-series-001"}
    ).items
    assert len(muestras) == 2
    primera = min(muestras, key=lambda item: item["elapsedMs"])
    assert primera["elapsedMs"] == 0
    assert primera["temperatureC"] == 92.4
    assert primera["quality"] == "valid"
    assert primera["source"] == "measured"
    # El orden lo da el proceso: el número del sobre es del mensaje y se
    # repetiría dentro de cada tramo.
    assert [m["sequence"] for m in sorted(muestras, key=lambda i: i["elapsedMs"])] == [0, 500]


def test_the_series_replaces_the_live_samples_of_its_preparation(client, barista_headers):
    """No pueden convivir dos versiones de la misma serie.

    Mientras la preparación transcurre, la telemetría deja muestras a la cadencia
    de la pantalla. Al cerrarla llega la serie buena, a la rejilla que la
    comparación declara. Conservar ambas dejaría un recuento que no cuadra con lo
    que declara el resumen.
    """
    en_vivo = {
        "schemaVersion": "1.0",
        "messageId": "message-live-before-series",
        "messageType": "telemetry.sample",
        "deviceId": "device-contract-test",
        "sequence": 3,
        "occurredAt": "2026-09-05T10:00:01Z",
        "payload": {
            "source": "measured",
            "preparationId": "extraction-series-002",
            "state": "extraction",
            "elapsedMs": 1000,
            "temperature": {"value": 90.0, "unit": "Cel", "quality": "valid"},
            "pressure": {"value": 8.0, "unit": "bar", "quality": "valid"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": True, "pumpDutyPercent": 80, "virtual": False},
        },
    }
    client.post(
        "/api/v1/sync",
        json={"deviceId": en_vivo["deviceId"], "messages": [en_vivo]},
        headers=barista_headers,
    )
    repositorio = client.app.state.repository
    assert len(
        repositorio.list_documents(
            "live-sample", page=1, page_size=50, filters={"extractionId": "extraction-series-002"}
        ).items
    ) == 1

    tramo = _series_chunk(0, 1, [_sample(0), _sample(500)], preparation="extraction-series-002")
    client.post(
        "/api/v1/sync",
        json={"deviceId": tramo["deviceId"], "messages": [tramo]},
        headers=barista_headers,
    )

    muestras = repositorio.list_documents(
        "live-sample", page=1, page_size=50, filters={"extractionId": "extraction-series-002"}
    ).items
    assert len(muestras) == 2
    assert all(item["id"].startswith("series-") for item in muestras)
    # La muestra provisional de 1000 ms no sobrevive al reemplazo.
    assert 1000 not in [item["elapsedMs"] for item in muestras]


def test_a_later_chunk_does_not_erase_an_earlier_one(client, barista_headers):
    """Los tramos se completan entre sí; no compiten.

    El reemplazo de lo provisional debe distinguir lo que dejó la telemetría de
    lo que ya archivó otro tramo, porque si no el último tramo en llegar borraría
    a los anteriores y la serie quedaría con su cola solamente.
    """
    primero = _series_chunk(0, 2, [_sample(0), _sample(500)], preparation="extraction-series-003")
    segundo = _series_chunk(
        1, 2, [_sample(1000), _sample(1500)], preparation="extraction-series-003"
    )
    for mensaje in (primero, segundo):
        client.post(
            "/api/v1/sync",
            json={"deviceId": mensaje["deviceId"], "messages": [mensaje]},
            headers=barista_headers,
        )

    muestras = client.app.state.repository.list_documents(
        "live-sample", page=1, page_size=50, filters={"extractionId": "extraction-series-003"}
    ).items
    assert sorted(item["elapsedMs"] for item in muestras) == [0, 500, 1000, 1500]


def test_an_unusable_sample_of_the_series_is_kept_and_marked(client, barista_headers):
    """Una muestra no apta se archiva marcada, no se descarta ni se rellena.

    Quien compare decide qué hacer con ella: la comparación la excluye y la
    cuenta como no comparable, que es distinto de que nunca hubiera existido.
    """
    tramo = _series_chunk(
        0,
        1,
        [_sample(0), _sample(500, temperature=None, pressure=None, quality="invalid")],
        preparation="extraction-series-004",
    )
    client.post(
        "/api/v1/sync",
        json={"deviceId": tramo["deviceId"], "messages": [tramo]},
        headers=barista_headers,
    )

    muestras = client.app.state.repository.list_documents(
        "live-sample", page=1, page_size=50, filters={"extractionId": "extraction-series-004"}
    ).items
    assert len(muestras) == 2
    no_apta = next(item for item in muestras if item["elapsedMs"] == 500)
    assert no_apta["quality"] == "invalid"
    assert no_apta["temperatureC"] is None
    assert no_apta["channelQuality"]["temperature"] == "invalid"


def test_sample_without_declared_source_is_not_recorded_as_measured(client, barista_headers):
    """Una muestra sin origen declarado no puede presentarse como una medición."""
    message = {
        "schemaVersion": "1.0",
        "messageId": "message-unknown-source-0001",
        "messageType": "telemetry.sample",
        "deviceId": "device-contract-test",
        "sequence": 8,
        "occurredAt": "2026-09-05T10:00:02Z",
        "payload": {
            "preparationId": "extraction-unknown-source",
            "state": "extraction",
            "elapsedMs": 2000,
            "temperature": {"value": 92.0, "unit": "Cel", "quality": "valid"},
            "pressure": {"value": 9.0, "unit": "bar", "quality": "invalid"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": True, "pumpDutyPercent": 80, "virtual": False},
        },
    }
    client.post(
        "/api/v1/sync",
        json={"deviceId": message["deviceId"], "messages": [message]},
        headers=barista_headers,
    )
    samples = client.app.state.repository.list_documents(
        "live-sample", page=1, page_size=10, filters={"extractionId": "extraction-unknown-source"}
    ).items
    assert samples[0]["source"] == "unknown"
    # Un canal no válido invalida la muestra completa para la comparación.
    assert samples[0]["quality"] == "invalid"


def test_sync_response_matches_the_published_contract(client, barista_headers):
    """La respuesta de sincronización cumple el contrato que el dispositivo lee.

    Es la única vía por la que una configuración aceptada llega a la máquina, y
    el dispositivo la interpreta sin margen: si la forma cambia sin avisar, deja
    de liberar su cola o deja de recibir propuestas, y ninguna de las dos cosas
    se nota desde la aplicación.
    """
    import json as _json
    from pathlib import Path as _Path

    from jsonschema import Draft202012Validator
    from referencing import Registry, Resource

    message = {
        "schemaVersion": "1.0",
        "messageId": "message-sync-response-0001",
        "messageType": "telemetry.sample",
        "deviceId": "device-contract-test",
        "sequence": 11,
        "occurredAt": "2026-09-05T10:00:03Z",
        "payload": {
            "source": "measured",
            "preparationId": None,
            "state": "ready",
            "elapsedMs": 0,
            "temperature": {"value": 92.0, "unit": "Cel", "quality": "valid"},
            "pressure": {"value": 0.0, "unit": "bar", "quality": "valid"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": False, "pumpDutyPercent": 0, "virtual": True},
        },
    }
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": message["deviceId"], "messages": [message]},
        headers=barista_headers,
    )
    assert response.status_code == 200
    body = response.json()
    assert body["acknowledged"] == ["message-sync-response-0001"]

    contracts = _Path(__file__).resolve().parents[4] / "platform/contracts/device"
    registry = Registry()
    schemas = {}
    for path in sorted(contracts.glob("*.schema.json")):
        schema = _json.loads(path.read_text(encoding="utf-8"))
        schemas[path.name] = schema
        if schema.get("$id"):
            registry = registry.with_resource(schema["$id"], Resource.from_contents(schema))
    validator = Draft202012Validator(schemas["sync-response.schema.json"], registry=registry)
    errors = sorted(validator.iter_errors(body), key=lambda error: list(error.path))
    assert not errors, [f"{'/'.join(str(p) for p in e.path)}: {e.message}" for e in errors]


def test_sync_gives_the_time_only_with_the_platform_clock_synchronised(
    client, barista_headers, monkeypatch
):
    """Sin reloj sincronizado no hay hora: la placa se quedaría con una vieja."""
    from aplicacion_web.application import services

    payload = {"deviceId": "device-clock-test", "messages": []}
    monkeypatch.setattr(services, "clock_is_synchronised", lambda: True)
    synced = client.post("/api/v1/sync", json=payload, headers=barista_headers).json()
    assert "serverTime" in synced
    monkeypatch.setattr(services, "clock_is_synchronised", lambda: False)
    unsynced = client.post("/api/v1/sync", json=payload, headers=barista_headers).json()
    assert "serverTime" not in unsynced
    assert unsynced["acknowledged"] == []


def test_profile_purge_defaults_to_ten_seconds_and_respects_firmware_range(
    client, barista_headers
):
    """El purgado se fija desde el perfil, con el rango que valida el firmware."""
    created = client.post("/api/v1/profiles", json=profile_payload(), headers=barista_headers)
    assert created.status_code == 201
    assert created.json()["purgeMaximumTimeMs"] == 10000

    for out_of_range in (999, 30001):
        payload = {**profile_payload(), "purgeMaximumTimeMs": out_of_range}
        response = client.post("/api/v1/profiles", json=payload, headers=barista_headers)
        assert response.status_code == 422

    # Una versión nueva lleva su propio corte; la aprobada no cambia.
    version_one = created.json()
    client.post(f"/api/v1/profiles/{version_one['id']}/approve", headers=barista_headers)
    versioned = client.post(
        f"/api/v1/profiles/{version_one['id']}/versions",
        json={**profile_payload(), "purgeMaximumTimeMs": 15000},
        headers=barista_headers,
    )
    assert versioned.status_code == 201
    assert versioned.json()["purgeMaximumTimeMs"] == 15000
    original = client.get(f"/api/v1/profiles/{version_one['id']}").json()
    assert original["purgeMaximumTimeMs"] == 10000


def test_a_profile_version_carries_the_boiler_offset_to_the_device(client, barista_headers):
    """La subida de caldera del primer uso viaja en la versión que la declara, y solo en ella."""
    governing = {**profile_payload("Perfil con subida"), "specifications": [
        *profile_payload()["specifications"],
        {"property": "extraction-time", "unit": "s", "strategy": "range",
         "minimum": 0, "maximum": 35},
    ]}
    created = client.post("/api/v1/profiles", json=governing, headers=barista_headers)
    base = created.json()
    assert base.get("boilerOffsetC") is None
    negative = client.post(
        f"/api/v1/profiles/{base['id']}/versions",
        json={**governing, "boilerOffsetC": -1.0},
        headers=barista_headers,
    )
    assert negative.status_code == 422

    client.post(f"/api/v1/profiles/{base['id']}/approve", headers=barista_headers)
    versioned = client.post(
        f"/api/v1/profiles/{base['id']}/versions",
        json={**governing, "boilerOffsetC": 3.0},
        headers=barista_headers,
    )
    assert versioned.status_code == 201
    version = versioned.json()
    client.post(f"/api/v1/profiles/{version['id']}/approve", headers=barista_headers)
    activated = client.post(f"/api/v1/profiles/{version['id']}/activate", headers=barista_headers)
    assert activated.status_code == 200, activated.text

    response = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-contract-test", "messages": []},
        headers=barista_headers,
    )
    bajan = {item["id"]: item for item in response.json()["profileProposals"]}
    assert bajan[version["id"]]["boilerOffsetC"] == 3.0
    assert all(
        "boilerOffsetC" not in item for key, item in bajan.items() if key != version["id"]
    )


def test_selected_profile_reaches_the_device_in_contract_shape(client, barista_headers):
    """El ciclo completo de la vía de selección, de la elección a lo que baja.

    El catálogo describe las referencias como especificaciones y el dispositivo
    solo sabe leer campos planos. Si lo que baja no está traducido, la máquina
    recibe una configuración que no puede interpretar y la rechaza entera.
    """
    repository = client.app.state.repository
    repository.upsert_document(
        "profile",
        "profile-seleccionable",
        {
            "id": "profile-seleccionable",
            "name": "Perfil elegido a mano",
            "version": 2,
            "status": "approved",
            "provenance": {"actor": "barista-test"},
            "specifications": [
                {"property": "water-temperature", "strategy": "target",
                 "target": 92.5, "tolerance": 0.5, "unit": "°C"},
                {"property": "pressure", "strategy": "target",
                 "target": 9.0, "tolerance": 0.4, "unit": "bar"},
                {"property": "extraction-time", "strategy": "range",
                 "maximum": 35.0, "unit": "s"},
            ],
        },
    )

    activation = client.post(
        "/api/v1/profiles/profile-seleccionable/activate", headers=barista_headers
    )
    assert activation.status_code == 200
    assert activation.json()["activation"]["via"] == "selection"

    response = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-contract-test", "messages": []},
        headers=barista_headers,
    )
    proposals = response.json()["profileProposals"]
    elegido = next(item for item in proposals if item["id"] == "profile-seleccionable")
    # Traducido a lo que el firmware lee, sin rastro de la forma del catálogo.
    assert elegido["temperatureSetpointC"] == 92.5
    assert elegido["pressureSetpointBar"] == 9.0
    assert elegido["maximumTimeMs"] == 35000
    assert "specifications" not in elegido
    # Este perfil no declara el purgado: no baja, y la placa conserva el suyo.
    assert "purgeMaximumTimeMs" not in elegido


def test_only_one_profile_governs_at_a_time(client, barista_headers):
    """Rige la última elegida: dos vigentes dejarían sin saber cuál gobierna."""
    repository = client.app.state.repository
    for identifier in ("profile-primero", "profile-segundo"):
        repository.upsert_document(
            "profile",
            identifier,
            {
                "id": identifier,
                "name": identifier,
                "version": 1,
                "status": "approved",
                "temperatureSetpointC": 93.0,
                "temperatureToleranceC": 0.5,
                "pressureSetpointBar": 9.0,
                "pressureToleranceBar": 0.5,
                "maximumTimeMs": 40000,
            },
        )
    client.post("/api/v1/profiles/profile-primero/activate", headers=barista_headers)
    client.post("/api/v1/profiles/profile-segundo/activate", headers=barista_headers)

    response = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-contract-test", "messages": []},
        headers=barista_headers,
    )
    vigentes = {item["id"] for item in response.json()["profileProposals"]}
    assert "profile-segundo" in vigentes
    assert "profile-primero" not in vigentes


def _aprobada(repository, identifier: str, temperatura: float = 93.0) -> None:
    repository.upsert_document(
        "profile",
        identifier,
        {
            "id": identifier,
            "name": identifier,
            "version": 1,
            "status": "approved",
            "temperatureSetpointC": temperatura,
            "temperatureToleranceC": 0.5,
            "pressureSetpointBar": 9.0,
            "pressureToleranceBar": 0.5,
            "maximumTimeMs": 40000,
        },
    )


def test_an_edited_profile_records_its_own_route(client, barista_headers):
    """La vía queda declarada: elegir y editar no son el mismo acto."""
    _aprobada(client.app.state.repository, "profile-editado", temperatura=94.0)
    response = client.post(
        "/api/v1/profiles/profile-editado/activate",
        json={"via": "edition"},
        headers=barista_headers,
    )
    assert response.status_code == 200
    activation = response.json()["activation"]
    assert activation["via"] == "edition"
    assert activation["recommendationId"] is None


def test_activation_by_recommendation_requires_an_accepted_decision(client, barista_headers):
    """Una activación por recomendación se comprueba, no se declara.

    Sin decisión registrada detrás, la cadena de autoridad quedaría rota justo
    en el eslabón que debe demostrarse: que una persona lo aprobó.
    """
    _aprobada(client.app.state.repository, "profile-recomendado")
    response = client.post(
        "/api/v1/profiles/profile-recomendado/activate",
        json={"via": "recommendation", "recommendationId": "rec-inexistente"},
        headers=barista_headers,
    )
    assert response.status_code in {400, 404}

    response = client.post(
        "/api/v1/profiles/profile-recomendado/activate",
        json={"via": "recommendation"},
        headers=barista_headers,
    )
    assert response.status_code == 400


def test_a_selection_cannot_claim_a_recommendation(client, barista_headers):
    """Declarar una recomendación en una selección mezclaría dos procedencias."""
    _aprobada(client.app.state.repository, "profile-mezcla")
    response = client.post(
        "/api/v1/profiles/profile-mezcla/activate",
        json={"via": "selection", "recommendationId": "rec-cualquiera"},
        headers=barista_headers,
    )
    assert response.status_code == 400


def test_a_draft_profile_cannot_govern_the_machine(client, barista_headers):
    """Un borrador no rige: la cadena exige una configuración ya aprobada."""
    client.app.state.repository.upsert_document(
        "profile",
        "profile-borrador",
        {"id": "profile-borrador", "name": "Borrador", "version": 1, "status": "draft"},
    )
    response = client.post(
        "/api/v1/profiles/profile-borrador/activate", headers=barista_headers
    )
    assert response.status_code == 409


def test_sync_returns_only_profiles_the_barista_accepted(client, barista_headers):
    """Solo baja lo aceptado: el catálogo entero no es una propuesta."""
    repository = client.app.state.repository
    repository.upsert_document(
        "profile",
        "profile-draft-not-proposed",
        {
            "id": "profile-draft-not-proposed",
            "name": "Borrador sin proponer",
            "version": 1,
            "temperatureSetpointC": 93.0,
            "temperatureToleranceC": 0.5,
            "pressureSetpointBar": 9.0,
            "pressureToleranceBar": 0.5,
            "maximumTimeMs": 40000,
            "status": "draft",
        },
    )
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-contract-test", "messages": []},
        headers=barista_headers,
    )
    returned = {item["id"] for item in response.json()["profileProposals"]}
    assert "profile-draft-not-proposed" not in returned
    assert all(item["status"] == "accepted" for item in response.json()["profileProposals"])


def test_sync_rejects_messages_from_another_device(client, barista_headers):
    message = {
        "schemaVersion": "1.0",
        "messageId": "message-wrong-device-0001",
        "messageType": "operational.event",
        "deviceId": "device-b",
        "sequence": 1,
        "occurredAt": "2026-09-05T10:00:00Z",
        "payload": {},
    }
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-a", "messages": [message]},
        headers=barista_headers,
    )
    assert response.status_code == 422


def test_viewer_cannot_write_and_error_has_correlation(client):
    response = client.post("/api/v1/coffees", json={"id": "coffee-x", "commercialReference": "X"})
    assert response.status_code == 403
    assert response.json()["error"]["code"] == "forbidden"
    assert response.json()["error"]["correlationId"] == response.headers["X-Correlation-ID"]


def test_request_size_limit_uses_uniform_error(client):
    response = client.post(
        "/api/v1/coffees",
        content=b"x",
        headers={"Content-Length": "1048577", "X-Correlation-ID": "size-limit-test"},
    )
    assert response.status_code == 413
    assert response.json()["error"]["code"] == "request_too_large"
    assert response.headers["X-Correlation-ID"] == "size-limit-test"


def test_audit_uses_authenticated_identity(client, barista_headers):
    response = client.post(
        "/api/v1/coffees",
        json={"id": "coffee-audit", "commercialReference": "Auditable"},
        headers=barista_headers,
    )
    assert response.status_code == 201
    audit = client.get(
        "/api/v1/audit",
        headers={"X-Test-Role": "admin", "X-Test-Actor": "admin-test"},
    ).json()
    assert audit["items"][0]["actor"] == "barista-test"


def test_technical_administration_is_restricted_and_attributed(client, barista_headers):
    assert client.get("/api/v1/system", headers=barista_headers).status_code == 403
    assert client.get("/api/v1/admin/users", headers=barista_headers).status_code == 403

    admin_headers = {"X-Test-Role": "admin", "X-Test-Actor": "admin-rnf09"}
    created = client.post(
        "/api/v1/admin/users",
        headers=admin_headers,
        json={
            "username": "barista.rnf09",
            "displayName": "Barista RNF09",
            "password": "clave-segura-rnf09",
            "role": "barista",
        },
    )
    assert created.status_code == 201
    account = created.json()
    assert account["active"] is True
    assert "password" not in account
    assert any(
        item["id"] == account["id"]
        for item in client.get("/api/v1/admin/users", headers=admin_headers).json()["items"]
    )

    disabled = client.put(
        f"/api/v1/admin/users/{account['id']}",
        headers=admin_headers,
        json={"displayName": "Barista RNF09", "role": "barista", "active": False},
    )
    assert disabled.status_code == 200
    assert disabled.json()["active"] is False
    assert (
        client.post(
            "/api/v1/auth/login",
            json={"username": "barista.rnf09", "password": "clave-segura-rnf09"},
        ).status_code
        == 401
    )

    audit = client.get("/api/v1/audit", headers=admin_headers).json()["items"]
    account_changes = [item for item in audit if item["resource"].startswith("/api/v1/admin/users")]
    assert len(account_changes) == 2
    assert {item["actor"] for item in account_changes} == {"admin-rnf09"}
    assert {item["operation"].split()[0] for item in account_changes} == {"POST", "PUT"}


def test_recommendation_abstains_and_never_commands_actuators(client, barista_headers):
    response = client.post(
        "/api/v1/recommendations",
        json={"extractionId": "ext-20260904-001"},
        headers=barista_headers,
    )
    assert response.status_code == 201
    recommendation = response.json()
    assert recommendation["status"] == "abstained"
    # La fuente es el razonador de EKO; sin diagnóstico, lo dice en vez de callar.
    assert recommendation["origin"] == "reasoner"
    assert recommendation["reasonCode"] == "NO_REASONER_CONCLUSION"
    assert recommendation["actuatorCommand"] is None
    assert recommendation["respondsTo"]["summary"]
    assert recommendation["knowledgeSource"] == {"name": "EKO", "version": "3.5.0"}
    explanation = client.post(
        f"/api/v1/recommendations/{recommendation['recommendationId']}/explanation"
    )
    assert explanation.status_code == 200
    assert explanation.json()["evidenceIds"]


def test_learning_output_requires_an_approved_traceable_model(client, barista_headers):
    class ModelAdapter:
        def __init__(self, approval_status: str) -> None:
            self.approval_status = approval_status

        def recommend(self, extraction_id, evidence, knowledge=None):
            return {
                "schemaVersion": "1.0",
                "origin": "machine-learning",
                "model": {
                    "name": "Sensorial contextual",
                    "version": "1.2.0",
                    "approvalStatus": self.approval_status,
                },
                "recommendationId": f"rec-model-{self.approval_status}",
                "inferenceId": "inference-model-rf20",
                "extractionId": extraction_id,
                "status": "pending-human-review",
                "reasonCode": "MODEL_ESTIMATE",
                "summary": "Revisar el punto de molienda.",
                "respondsTo": {"code": "acidity", "summary": "Acidez estimada elevada."},
                "proposedAdjustment": {
                    "property": "grind-setting",
                    "direction": "increase",
                    "value": 4.4,
                    "unit": "punto",
                },
                "uncertainty": {
                    "value": 0.18,
                    "methodReference": "urn:method:interval-v1",
                },
                "evidenceIds": [item["id"] for item in evidence],
                "knowledgeSource": {"name": "Modelo sensorial", "version": "1.2.0"},
                "applicability": "Café y perfil equivalentes a los datos de validación.",
                "requiresHumanReview": True,
                "actuatorCommand": None,
                "predictedSensoryProfile": {
                    "coffeeId": "coffee-huila-001",
                    "descriptors": [{"descriptor": "Citrus", "label": "Cítrico", "intensity": 6.2}],
                    "provenance": {
                        "source": "inference-model-rf20",
                        "generatedAt": "2026-09-07T12:00:00Z",
                    },
                },
                "provenance": {
                    "source": "model-registry-approved",
                    "recordedAt": "2026-09-07T12:00:00Z",
                },
                "createdAt": "2026-09-07T12:00:00Z",
            }

    service = client.app.state.service
    original = service.recommender
    try:
        service.recommender = ModelAdapter("approved")
        accepted = client.post(
            "/api/v1/recommendations",
            json={"extractionId": "ext-20260904-001"},
            headers=barista_headers,
        )
        assert accepted.status_code == 201
        assert accepted.json()["model"]["version"] == "1.2.0"
        prediction = accepted.json()["predictedSensoryProfile"]
        assert prediction["descriptors"][0]["descriptor"] == "Citrus"

        service.recommender = ModelAdapter("candidate")
        rejected = client.post(
            "/api/v1/recommendations",
            json={"extractionId": "ext-20260904-001"},
            headers=barista_headers,
        )
        assert rejected.status_code == 409
    finally:
        service.recommender = original


def test_evidence_identifies_ontology_and_limits(client):
    response = client.get("/api/v1/extractions/ext-20260904-001/evidence")
    assert response.status_code == 200
    assert response.json()["ontology"] == {"name": "EKO", "version": "3.5.0"}
    assert "causalidad" in response.json()["limits"]


def test_sensory_contract_rejects_invalid_scale_with_uniform_error(client, barista_headers):
    response = client.put(
        "/api/v1/extractions/sim-20260904-003/sensory-evaluation",
        json={
            "id": "eval-invalid",
            "evaluatedAt": "2026-09-04T16:05:00Z",
            "scores": [{"attribute": "overall-liking", "dimension": "hedonic-liking", "value": 10}],
        },
        headers=barista_headers,
    )
    assert response.status_code == 422
    assert response.json()["error"]["code"] == "validation_error"
    assert response.json()["error"]["correlationId"] == response.headers["X-Correlation-ID"]


def test_sensory_evaluation_records_separate_descriptors_attributes_and_decision(
    client, barista_headers
):
    response = client.put(
        "/api/v1/extractions/sim-20260904-003/sensory-evaluation",
        json={
            "id": "eval-rf10",
            "evaluatedAt": "2026-09-04T16:05:00Z",
            "descriptors": ["Citrus", "Panela"],
            "descriptorRatings": [
                {"descriptor": "Citrus", "intensity": 6},
                {"descriptor": "Panela", "intensity": 8},
            ],
            "defects": ["ExcessiveAcidity"],
            "desiredChanges": [{"attribute": "Sweetness", "direction": "Increase"}],
            "scores": [
                {
                    "attribute": "Acidity",
                    "dimension": "perceived-intensity",
                    "value": 7,
                },
                {
                    "attribute": "OverallLiking",
                    "dimension": "hedonic-liking",
                    "value": 6,
                },
            ],
            "acceptance": {
                "status": "rejected",
                "rationale": "La acidez domina la bebida.",
            },
            "comparisons": [],
        },
        headers=barista_headers,
    )
    assert response.status_code == 200
    evaluation = response.json()["sensoryEvaluation"]
    assert evaluation["evaluatedBy"] == "barista-test"
    assert evaluation["descriptorRatings"][1] == {
        "descriptor": "Panela",
        "intensity": 8,
    }
    assert evaluation["acceptance"]["status"] == "rejected"


def test_a_descriptor_outside_the_list_is_kept_as_said(client, barista_headers):
    """«Azúcar morena en el retrogusto» no cabe en ninguna casilla."""
    path = "/api/v1/extractions/sim-20260904-003/sensory-evaluation"
    base = {
        "id": "eval-otros-descriptores",
        "evaluatedAt": "2026-09-04T16:05:00Z",
        "scores": [{"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 6}],
        "acceptance": {"status": "accepted"},
    }
    saved = client.put(path, headers=barista_headers, json={
        **base, "otherDescriptors": [{"name": " Azúcar morena ", "intensity": 5}],
    })
    assert saved.status_code == 200
    assert saved.json()["sensoryEvaluation"]["otherDescriptors"] == [
        {"name": "Azúcar morena", "intensity": 5}
    ]
    repeated = client.put(path, headers=barista_headers, json={
        **base,
        "otherDescriptors": [
            {"name": "Panela", "intensity": 5},
            {"name": "panela", "intensity": 3},
        ],
    })
    assert repeated.status_code == 422


def test_sensory_evaluation_requires_explicit_valid_decision_and_references(
    client, barista_headers
):
    base = {
        "id": "eval-rf10-invalid",
        "evaluatedAt": "2026-09-04T16:05:00Z",
        "scores": [
            {
                "attribute": "OverallLiking",
                "dimension": "hedonic-liking",
                "value": 6,
            }
        ],
    }
    no_decision = client.put(
        "/api/v1/extractions/sim-20260904-003/sensory-evaluation",
        json=base,
        headers=barista_headers,
    )
    rejected_without_reason = client.put(
        "/api/v1/extractions/sim-20260904-003/sensory-evaluation",
        json={**base, "acceptance": {"status": "rejected"}},
        headers=barista_headers,
    )
    absent_reference = client.put(
        "/api/v1/extractions/sim-20260904-003/sensory-evaluation",
        json={
            **base,
            "acceptance": {"status": "accepted"},
            "comparisons": ["barista-target"],
        },
        headers=barista_headers,
    )
    assert no_decision.status_code == 422
    assert rejected_without_reason.status_code == 422
    assert absent_reference.status_code == 400


def test_learning_episode_enforces_temporal_order(client, barista_headers):
    response = client.post(
        "/api/v1/learning-episodes",
        json={
            "coffeeId": "coffee-huila-001",
            "initialExtractionId": "ext-20260904-001",
            "followUpExtractionId": "ext-20260904-002",
            "adjustment": {
                "property": "grind-setting",
                "previousValue": 4.2,
                "newValue": 4.5,
                "unit": "dial",
                "occurredAt": "2026-09-04T14:00:00Z",
            },
            "targetOutcome": {"kind": "observable-property", "code": "balance"},
            "outcome": {
                "category": "expected-observed",
                "assessedAt": "2026-09-04T15:14:00Z",
                "rationale": "Mejora observada.",
            },
        },
        headers=barista_headers,
    )
    assert response.status_code == 400
    assert response.json()["error"]["code"] == "validation_error"


def test_guided_calibration_orders_tests_adjustments_and_creates_version(client, barista_headers):
    profile = client.post(
        "/api/v1/profiles",
        json={
            "name": "Perfil para calibrar",
            "coffeeIds": ["coffee-huila-001"],
            "specifications": [
                {
                    "property": "temperature",
                    "unit": "°C",
                    "strategy": "target",
                    "target": 92,
                    "tolerance": 1,
                }
            ],
        },
        headers=barista_headers,
    ).json()
    client.post(f"/api/v1/profiles/{profile['id']}/approve", headers=barista_headers)
    started = client.post(
        "/api/v1/calibrations",
        json={
            "coffeeId": "coffee-huila-001",
            "profileId": profile["id"],
            "context": {"station": "Barra principal"},
        },
        headers=barista_headers,
    )
    assert started.status_code == 201
    calibration_id = started.json()["id"]

    first = client.post(
        f"/api/v1/calibrations/{calibration_id}/tests",
        json={"extractionId": "ext-20260904-001"},
        headers=barista_headers,
    )
    adjusted = client.post(
        f"/api/v1/calibrations/{calibration_id}/adjustments",
        json={
            "property": "grind-setting",
            "previousValue": 4.2,
            "newValue": 4.5,
            "unit": "dial",
            "rationale": "Mejorar el balance.",
        },
        headers=barista_headers,
    )
    second = client.post(
        f"/api/v1/calibrations/{calibration_id}/tests",
        json={"extractionId": "ext-20260904-002"},
        headers=barista_headers,
    )
    closed = client.post(
        f"/api/v1/calibrations/{calibration_id}/close",
        json={
            "result": "new-profile-version",
            "rationale": "La segunda preparación requiere consolidar el ajuste.",
        },
        headers=barista_headers,
    )

    assert first.status_code == 200
    assert adjusted.status_code == 200
    assert second.status_code == 200
    assert closed.status_code == 200
    result = closed.json()
    assert result["status"] == "closed"
    assert result["testExtractionIds"] == ["ext-20260904-001", "ext-20260904-002"]
    assert [event["sequence"] for event in result["events"]] == [1, 2, 3, 4, 5]
    assert result["resultProfileId"].endswith("-v2")
    assert client.get(f"/api/v1/profiles/{result['resultProfileId']}").json()["status"] == "draft"


def test_calibration_requires_a_test_and_matching_coffee(client, barista_headers):
    started = client.post(
        "/api/v1/calibrations",
        json={"coffeeId": "coffee-huila-001", "context": {}},
        headers=barista_headers,
    ).json()
    calibration_id = started["id"]
    mismatch = client.post(
        f"/api/v1/calibrations/{calibration_id}/tests",
        json={"extractionId": "sim-20260904-003"},
        headers=barista_headers,
    )
    closed = client.post(
        f"/api/v1/calibrations/{calibration_id}/close",
        json={"result": "rejected", "rationale": "No se pudo completar."},
        headers=barista_headers,
    )
    assert mismatch.status_code == 400
    assert closed.status_code == 400


def test_live_extraction_tolerates_a_sample_already_written_by_another_view(
    client, barista_headers
):
    """Dos vistas abiertas a la vez no pueden dejar sin respuesta a la segunda.

    La muestra simulada tiene identificador determinista —depende de la secuencia,
    no de quién consulta—, de modo que cuando dos sesiones consultan a la vez las
    dos calculan la misma. Si la segunda tratara de crearla y fallara por existir,
    la lectura devolvería conflicto: con tres sesiones simultáneas eso era el 40 %
    de las lecturas (C8 del plan de medición de capacidad).
    """
    started = client.post(
        "/api/v1/extractions/live",
        json={"coffeeId": "coffee-huila-001", "profileId": "espresso-lab"},
        headers=barista_headers,
    )
    assert started.status_code == 201
    extraction_id = started.json()["id"]

    # Se escribe a mano la muestra que la siguiente lectura iba a crear: es la
    # carrera, sin depender de la temporización de dos hilos.
    repository = client.app.state.repository
    repository.add_document(
        "live-sample",
        f"{extraction_id}-sample-00000",
        {
            "id": f"{extraction_id}-sample-00000",
            "extractionId": extraction_id,
            "sequence": 0,
            "elapsedMs": 0,
            "occurredAt": "2026-09-15T23:00:00Z",
            "temperatureC": 91.2,
            "pressureBar": 0.0,
            "quality": "valid",
            "environment": {},
            "source": "simulation",
        },
    )

    respuesta = client.get(
        f"/api/v1/extractions/{extraction_id}/live", headers={"X-Test-Elapsed-Ms": "0"}
    )

    assert respuesta.status_code == 200
    # La que ya estaba se reconoce en lugar de duplicarse ni de romper la lectura.
    assert len(respuesta.json()["samples"]) == 1
    assert respuesta.json()["sampleCount"] == 1


def test_live_extraction_preserves_targets_environment_and_missing_samples(client, barista_headers):
    started = client.post(
        "/api/v1/extractions/live",
        json={
            "coffeeId": "coffee-huila-001",
            "profileId": "espresso-lab",
            "ambientTemperatureC": 22.4,
            "ambientHumidityPercent": 61,
        },
        headers=barista_headers,
    )
    assert started.status_code == 201
    extraction_id = started.json()["id"]

    # El número de muestras es función del tiempo transcurrido, no de cuántas
    # veces se consulte: 2500 ms a 500 ms por muestra son seis lecturas.
    current = client.get(
        f"/api/v1/extractions/{extraction_id}/live", headers={"X-Test-Elapsed-Ms": "2500"}
    )
    repeated = client.get(
        f"/api/v1/extractions/{extraction_id}/live", headers={"X-Test-Elapsed-Ms": "2500"}
    )
    assert len(repeated.json()["samples"]) == len(current.json()["samples"]) == 6
    assert current.status_code == 200
    live = current.json()
    assert live["source"] == "simulation"
    assert live["sourceLabel"] == "Datos simulados para pruebas"
    assert live["profile"]["name"] == "Espresso laboratorio"
    assert live["profile"]["targets"] == {"temperatureC": 93.0, "pressureBar": 9.0}
    assert live["environment"] == {"temperatureC": 22.4, "humidityPercent": 61.0}
    assert live["samples"][0]["elapsedMs"] == 0
    missing = [sample for sample in live["samples"] if sample["quality"] == "missing"]
    assert len(missing) == 1
    assert missing[0]["temperatureC"] is None
    assert missing[0]["pressureBar"] is None
    assert live["missingIntervals"] == [{"fromMs": 2000, "toMs": 2500}]

    finished = client.post(
        f"/api/v1/extractions/{extraction_id}/finish",
        json={"cause": "barista-decision"},
        headers=barista_headers,
    )
    assert finished.status_code == 200
    result = finished.json()
    assert result["status"] == "closed"
    assert result["termination"] == "barista-decision"
    assert result["finishedBy"] == "barista-test"
    assert result["sampleCount"] == 6
    series = client.get(f"/api/v1/extractions/{extraction_id}/series").json()
    assert series["samples"][4]["temperatureC"] is None
    assert client.get(f"/api/v1/extractions/{extraction_id}").status_code == 200
    assessment = client.get(f"/api/v1/extractions/{extraction_id}/result").json()
    assert assessment["technicalConformance"]["status"] == "non-conformant"
    assert assessment["sensoryAcceptance"]["status"] == "not-recorded"
    assert assessment["properties"][0]["observed"] is not None
    # En 2,5 s la presión de práctica no llega a su rango: la presión queda fuera
    # por ese motivo y la temperatura, sin tramo en que juzgarla, sin evaluar.
    by_property = {item["property"]: item for item in assessment["properties"]}
    assert by_property["pressure"]["deviation"]["status"] == "outside"
    assert by_property["pressure"]["deviation"]["reason"] == "pressure-never-reached-range"
    assert by_property["temperature"]["deviation"]["status"] == "not-evaluable"
    # La práctica simula el agua; la placa mide la caldera (EKO 3.4.0).
    assert by_property["temperature"]["label"] == "Temperatura del agua"


def test_extraction_result_keeps_insufficiency_sensory_decision_and_recommendations(
    client, barista_headers
):
    initial = client.get("/api/v1/extractions/ext-20260904-001/result")
    assert initial.status_code == 200
    result = initial.json()
    assert result["technicalConformance"]["status"] == "insufficient-information"
    assert result["technicalConformance"]["missingData"] == [
        "Temperatura media estimada del agua",
        "Presión",
    ]
    assert result["sensoryAcceptance"]["status"] == "accepted"
    assert result["recommendations"][0]["recommendationId"] == "rec-demo-grind-001"

    recommendation = client.post(
        "/api/v1/recommendations",
        json={"extractionId": "ext-20260904-001"},
        headers=barista_headers,
    )
    assert recommendation.status_code == 201
    updated = client.get("/api/v1/extractions/ext-20260904-001/result").json()
    assert recommendation.json()["recommendationId"] in {
        item["recommendationId"] for item in updated["recommendations"]
    }
    assert updated["diagnosis"]["knowledgeSource"]["name"]
    assert updated["diagnosis"]["generatedBy"]["name"]


def _live_with_profile(client, barista_headers, temperature_tolerance, elapsed_ms):
    temperature = {
        "property": "water-temperature", "unit": "°C", "strategy": "target", "target": 92
    }
    if temperature_tolerance is not None:
        temperature["tolerance"] = temperature_tolerance
    profile = client.post(
        "/api/v1/profiles",
        json={
            "name": "Perfil para la estabilidad",
            "coffeeIds": ["coffee-huila-001"],
            "specifications": [
                temperature,
                {"property": "pressure", "unit": "bar", "strategy": "target",
                 "target": 9, "tolerance": 0.5},
            ],
        },
        headers=barista_headers,
    ).json()
    client.post(f"/api/v1/profiles/{profile['id']}/approve", headers=barista_headers)
    extraction = client.post(
        "/api/v1/extractions/live",
        json={"coffeeId": "coffee-huila-001", "profileId": profile["id"]},
        headers=barista_headers,
    ).json()
    client.get(
        f"/api/v1/extractions/{extraction['id']}/live",
        headers={"X-Test-Elapsed-Ms": str(elapsed_ms)},
    )
    return client.post(
        f"/api/v1/extractions/{extraction['id']}/finish",
        json={"cause": "barista-decision"},
        headers=barista_headers,
    ).json()["statistics"]


def test_stability_is_measured_once_the_pressure_reaches_its_range(client, barista_headers):
    # La serie de práctica sube la presión en 4 s: la primera muestra dentro de
    # 9 ± 0,5 bar es la de 4000 ms, y lo anterior no cuenta.
    statistics = _live_with_profile(client, barista_headers, 1.0, 6000)
    assert statistics["window"] == {
        "rule": "pressure-first-within-tolerance-v1",
        "status": "stabilized",
        "startMs": 4000,
    }
    assert statistics["temperatureSite"] == "boiler"
    assert statistics["pressureMaeBar"] < 0.5
    assert statistics["temperatureWithinTolerancePercent"] is not None


def test_a_preparation_that_never_reaches_its_pressure_is_not_scored(client, barista_headers):
    statistics = _live_with_profile(client, barista_headers, 1.0, 2500)
    assert statistics["window"]["status"] == "never-stabilized"
    assert statistics["temperatureMaeC"] is None
    assert statistics["pressureWithinTolerancePercent"] is None


def test_without_a_declared_tolerance_the_percentage_is_not_invented(client, barista_headers):
    statistics = _live_with_profile(client, barista_headers, None, 6000)
    assert statistics["temperatureWithinTolerancePercent"] is None
    assert statistics["temperatureMaeC"] is not None


def _closed_preparation(client, barista_headers, specifications, elapsed_ms=6000):
    profile = client.post(
        "/api/v1/profiles",
        json={
            "name": "Perfil para la conformidad",
            "coffeeIds": ["coffee-huila-001"],
            "specifications": specifications,
        },
        headers=barista_headers,
    ).json()
    client.post(f"/api/v1/profiles/{profile['id']}/approve", headers=barista_headers)
    extraction = client.post(
        "/api/v1/extractions/live",
        json={"coffeeId": "coffee-huila-001", "profileId": profile["id"]},
        headers=barista_headers,
    ).json()
    client.get(
        f"/api/v1/extractions/{extraction['id']}/live",
        headers={"X-Test-Elapsed-Ms": str(elapsed_ms)},
    )
    client.post(
        f"/api/v1/extractions/{extraction['id']}/finish",
        json={"cause": "barista-decision"},
        headers=barista_headers,
    )
    return extraction["id"]


PRESSURE_SPECIFICATION = {
    "property": "pressure", "unit": "bar", "strategy": "target", "target": 9, "tolerance": 0.5
}


def test_a_preparation_without_stored_assessment_is_assessed_once(client, barista_headers):
    """Las anteriores a que se guardaran se guardan al consultarlas, no en cada consulta."""
    from sqlalchemy import delete

    from aplicacion_web.adapters.database import Document

    extraction_id = _closed_preparation(client, barista_headers, [PRESSURE_SPECIFICATION])
    with client.app.state.repository.session_factory.begin() as session:
        session.execute(delete(Document).where(Document.kind == "conformity-assessment"))
    url = f"/api/v1/extractions/{extraction_id}/conformity-assessments"
    assert client.get(url).json()["total"] == 0

    history = client.get("/api/v1/extractions?source=simulation", headers=barista_headers)
    assert history.status_code == 200
    stored = client.get(url).json()
    assert stored["total"] == 1
    assert stored["items"][0]["trigger"] == "reconstruction"

    result = client.get(f"/api/v1/extractions/{extraction_id}/result").json()
    assert result["assessment"]["recorded"] is True
    assert result["assessment"]["sequence"] == 1
    assert client.get(url).json()["total"] == 1


def test_the_stored_assessment_carries_conclusions_that_pass_the_shapes(
    client, barista_headers
):
    """Lo que se publica de cada café (PC04, PC22) cumple las formas de EKO."""
    extraction_id = _closed_preparation(client, barista_headers, [PRESSURE_SPECIFICATION])
    stored = client.app.state.repository.list_documents(
        "conformity-assessment", page=1, page_size=10, filters={"extractionId": extraction_id}
    ).items
    assert stored and stored[-1]["conclusionValidation"]["conforms"] is True
    assert "assessesExtraction" in stored[-1]["conclusionGraph"]
    # La pantalla no recibe el grafo.
    listed = client.get(f"/api/v1/extractions/{extraction_id}/conformity-assessments").json()
    assert "conclusionGraph" not in listed["items"][-1]


def test_each_change_in_the_assessment_is_kept_without_replacing_the_last(
    client, barista_headers
):
    extraction_id = _closed_preparation(
        client,
        barista_headers,
        [
            PRESSURE_SPECIFICATION,
            {"property": "tds", "unit": "%", "strategy": "range", "minimum": 8, "maximum": 10},
        ],
    )
    url = f"/api/v1/extractions/{extraction_id}/conformity-assessments"
    first = client.get(url).json()
    assert first["total"] == 1
    assert first["items"][0]["trigger"] == "extraction-closed"
    assert first["items"][0]["window"]["status"] == "stabilized"
    assert first["items"][0]["provenance"]["extractionSource"] == "simulation"
    assert first["items"][0]["technicalConformance"]["status"] == "insufficient-information"

    measurements = f"/api/v1/extractions/{extraction_id}/manual-measurements"
    client.put(measurements, json={"tdsPercent": 9.0}, headers=barista_headers)
    second = client.get(url).json()
    assert second["total"] == 2
    assert second["items"][0] == first["items"][0]
    assert second["items"][1]["trigger"] == "manual-measurements"
    result = client.get(f"/api/v1/extractions/{extraction_id}/result").json()
    assert result["assessment"]["sequence"] == 2
    assert result["assessment"]["recorded"] is True

    # Registrar lo mismo otra vez no cambia la evaluación y no añade otra.
    client.put(measurements, json={"tdsPercent": 9.0}, headers=barista_headers)
    assert client.get(url).json()["total"] == 2


def test_a_deviation_keeps_when_it_started_and_how_long_it_lasted(client, barista_headers):
    # La temperatura de práctica oscila ±0,22 °C: con 0,1 °C de margen sale fuera.
    extraction_id = _closed_preparation(
        client,
        barista_headers,
        [
            PRESSURE_SPECIFICATION,
            {"property": "water-temperature", "unit": "°C", "strategy": "target",
             "target": 92, "tolerance": 0.1},
        ],
    )
    result = client.get(f"/api/v1/extractions/{extraction_id}/result").json()
    temperature = next(
        item for item in result["properties"] if item["property"] == "water-temperature"
    )
    assert temperature["deviation"]["status"] == "outside"
    # El tramo empieza a los 4000 ms: nada anterior puede ser el primer instante.
    assert temperature["deviation"]["firstOutsideMs"] >= 4000
    assert temperature["deviation"]["outsideDurationMs"] > 0
    assert temperature["objective"]["tolerance"] == 0.1


def test_a_target_without_tolerance_is_not_judged(client, barista_headers):
    extraction_id = _closed_preparation(
        client,
        barista_headers,
        [
            PRESSURE_SPECIFICATION,
            {"property": "water-temperature", "unit": "°C", "strategy": "target", "target": 92},
        ],
    )
    result = client.get(f"/api/v1/extractions/{extraction_id}/result").json()
    temperature = next(
        item for item in result["properties"] if item["property"] == "water-temperature"
    )
    assert temperature["objective"]["tolerance"] is None
    assert temperature["observed"] is not None
    assert temperature["deviation"]["status"] == "no-tolerance"
    assert result["technicalConformance"]["status"] == "insufficient-information"
    assert "Temperatura del agua" in result["technicalConformance"]["missingData"]


def test_extraction_result_requires_complete_coverage_to_be_conformant(client, barista_headers):
    profile = client.post(
        "/api/v1/profiles",
        json={
            "name": "Perfil con cobertura completa",
            "coffeeIds": ["coffee-huila-001"],
            "specifications": [
                {
                    "property": "water-temperature",
                    "unit": "°C",
                    "strategy": "target",
                    "target": 92,
                    "tolerance": 3,
                },
                {
                    "property": "pressure",
                    "unit": "bar",
                    "strategy": "target",
                    "target": 9,
                    "tolerance": 9,
                },
            ],
        },
        headers=barista_headers,
    ).json()
    client.post(f"/api/v1/profiles/{profile['id']}/approve", headers=barista_headers)
    extraction = client.post(
        "/api/v1/extractions/live",
        json={"coffeeId": "coffee-huila-001", "profileId": profile["id"]},
        headers=barista_headers,
    ).json()
    client.get(f"/api/v1/extractions/{extraction['id']}/live")
    client.post(
        f"/api/v1/extractions/{extraction['id']}/finish",
        json={"cause": "barista-decision"},
        headers=barista_headers,
    )
    result = client.get(f"/api/v1/extractions/{extraction['id']}/result").json()
    assert result["technicalConformance"]["status"] == "conformant"
    assert result["technicalConformance"]["assessedProperties"] == 2
    assert result["technicalConformance"]["missingData"] == []
    # El veredicto lo da el razonador de EKO, no una regla de la plataforma.
    assert result["assessment"]["reasoning"]["agent"] == "EvidenceBasedReasoner"
    assert result["assessment"]["rule"].startswith("eko-")
    assert "técnicamente conforme" in result["diagnosis"]["conclusion"]
    # Estabilidad de la máquina (§ 4.4): la serie empieza al pulsar, así que la
    # térmica no se inventa con las muestras del café.
    machine = result["machineStability"]
    assert machine["rule"] == "machine-stability-v2"
    assert machine["thermal"]["status"] == "no-wait-samples"
    assert machine["pressure"]["bandBar"] == 0.18


def test_live_extraction_requires_an_applicable_approved_profile(client, barista_headers):
    profile = client.post(
        "/api/v1/profiles",
        json={
            "name": "Borrador",
            "coffeeIds": ["coffee-huila-001"],
            "specifications": [
                {
                    "property": "temperature",
                    "unit": "°C",
                    "strategy": "target",
                    "target": 92,
                    "tolerance": 1,
                }
            ],
        },
        headers=barista_headers,
    ).json()
    response = client.post(
        "/api/v1/extractions/live",
        json={"coffeeId": "coffee-huila-001", "profileId": profile["id"]},
        headers=barista_headers,
    )
    assert response.status_code == 400
    assert response.json()["error"]["code"] == "validation_error"


def test_live_observations_follow_each_users_preferences(client, barista_headers):
    started = client.post(
        "/api/v1/extractions/live",
        json={"coffeeId": "coffee-huila-001", "profileId": "espresso-lab"},
        headers=barista_headers,
    ).json()
    path = f"/api/v1/extractions/{started['id']}/live"
    first = client.get(path, headers=barista_headers).json()
    assert first["observations"][0]["code"] == "temperature-below-profile"
    assert first["observations"][1]["code"] == "pressure-below-profile"
    assert (
        "sensorial"
        not in " ".join(observation["message"] for observation in first["observations"]).lower()
    )

    saved = client.put(
        "/api/v1/me/extraction-alerts",
        json={"temperature": False, "pressure": False, "acquisition": True},
        headers=barista_headers,
    )
    assert saved.status_code == 200
    assert saved.json()["updatedBy"] == "barista-test"
    second = client.get(path, headers=barista_headers).json()
    assert second["observations"] == []
    other_user = client.get(
        "/api/v1/me/extraction-alerts",
        headers={"X-Test-Role": "barista", "X-Test-Actor": "other-barista"},
    ).json()
    assert other_user["temperature"] is True


def test_manual_data_can_be_saved_separately_and_explains_pending_results(client, barista_headers):
    path = "/api/v1/extractions/sim-20260904-003/manual-measurements"
    dose = client.put(path, json={"coffeeDoseG": 18}, headers=barista_headers)
    assert dose.status_code == 200
    measurements = dose.json()["manualMeasurements"]
    assert measurements["coffeeDoseG"] == 18
    assert measurements["entries"]["coffeeDoseG"]["recordedBy"] == "barista-test"
    assert measurements["entries"]["coffeeDoseG"]["recordedAt"]
    assert measurements["completion"]["results"]["extractionYieldPercent"] == {
        "status": "pending",
        "missingFields": ["masa de bebida", "TDS"],
    }
    assert [item["label"] for item in measurements["completion"]["missingFields"]] == [
        "masa de bebida",
        "TDS",
    ]

    other_headers = {"X-Test-Role": "barista", "X-Test-Actor": "other-barista"}
    completed = client.put(
        path,
        json={
            "beverageMassG": 36,
            "tdsPercent": 9.5,
            "instrument": "Refractómetro de barra",
        },
        headers=other_headers,
    )
    assert completed.status_code == 200
    measurements = completed.json()["manualMeasurements"]
    assert measurements["extractionYieldPercent"] == 19
    assert measurements["completion"]["missingFields"] == []
    assert measurements["completion"]["results"]["extractionYieldPercent"] == {
        "status": "available",
        "missingFields": [],
    }
    assert measurements["entries"]["coffeeDoseG"]["recordedBy"] == "barista-test"
    assert measurements["entries"]["beverageMassG"]["recordedBy"] == "other-barista"
    assert "bedCondition" not in measurements["entries"]
    rejected = client.put(path, json={"bedCondition": "uniform"}, headers=other_headers)
    assert rejected.status_code == 422


def test_the_brew_ratio_is_available_before_the_tds(client, barista_headers):
    """La relación café-agua sale de las dos masas y no espera al refractómetro.

    Es una condición de preparación que el barista fija como objetivo, de modo
    que conviene saber si la preparación la cumplió aunque el TDS no esté.
    """
    path = "/api/v1/extractions/sim-20260904-003/manual-measurements"
    dose = client.put(path, json={"coffeeDoseG": 18}, headers=barista_headers)
    assert dose.status_code == 200
    measurements = dose.json()["manualMeasurements"]
    assert "brewRatio" not in measurements
    assert measurements["completion"]["results"]["brewRatio"] == {
        "status": "pending",
        "missingFields": ["masa de bebida"],
    }

    both = client.put(path, json={"beverageMassG": 36}, headers=barista_headers)
    assert both.status_code == 200
    measurements = both.json()["manualMeasurements"]
    assert measurements["brewRatio"] == 2.0
    assert measurements["completion"]["results"]["brewRatio"] == {
        "status": "available",
        "missingFields": [],
    }
    # El rendimiento sigue esperando al TDS, que la relación no necesita.
    assert measurements["completion"]["results"]["extractionYieldPercent"]["status"] == "pending"


def test_manual_data_rejects_an_empty_update_and_reports_legacy_missing_fields(
    client, barista_headers
):
    empty = client.put(
        "/api/v1/extractions/sim-20260904-003/manual-measurements",
        json={},
        headers=barista_headers,
    )
    assert empty.status_code == 422
    legacy = client.get("/api/v1/extractions/ext-20260904-001").json()
    assert legacy["manualMeasurements"]["completion"]["missingFields"] == []
    assert (
        legacy["manualMeasurements"]["completion"]["results"]["extractionYieldPercent"]["status"]
        == "available"
    )


def test_closed_extraction_changes_are_append_only_revisions(client, barista_headers):
    repository = client.app.state.repository
    extraction_id = "sim-20260904-003"
    original = repository.get_document("extraction", extraction_id)

    measurements = client.put(
        f"/api/v1/extractions/{extraction_id}/manual-measurements",
        json={"coffeeDoseG": 18},
        headers=barista_headers,
    )
    sensory = client.put(
        f"/api/v1/extractions/{extraction_id}/sensory-evaluation",
        json={
            "id": "evaluation-rnf05",
            "evaluatedAt": "2026-09-05T12:00:00Z",
            "scores": [
                {
                    "attribute": "OverallLiking",
                    "dimension": "hedonic-liking",
                    "value": 7,
                }
            ],
            "acceptance": {"status": "accepted"},
        },
        headers=barista_headers,
    )

    assert measurements.status_code == 200
    assert sensory.status_code == 200
    assert repository.get_document("extraction", extraction_id) == original
    revisions = sorted(
        repository.list_documents(
            "extraction-revision",
            page=1,
            page_size=10,
            filters={"resourceId": extraction_id},
        ).items,
        key=lambda item: item["recordedAt"],
    )
    assert [item["category"] for item in revisions] == [
        "manual-measurements",
        "sensory-evaluation",
    ]
    assert revisions[0]["previousRecordId"] == extraction_id
    assert revisions[1]["previousRecordId"] == revisions[0]["id"]
    assert all(item["recordedBy"] == "barista-test" for item in revisions)
    projected = client.get(f"/api/v1/extractions/{extraction_id}").json()
    assert projected["manualMeasurements"]["coffeeDoseG"] == 18
    assert projected["sensoryEvaluation"]["id"] == "evaluation-rnf05"
    assert len(projected["revisions"]) == 2


def test_retried_barista_action_does_not_duplicate_a_revision(client, barista_headers):
    headers = {**barista_headers, "Idempotency-Key": "offline-action-rnf07"}
    path = "/api/v1/extractions/sim-20260904-003/manual-measurements"
    first = client.put(path, json={"coffeeDoseG": 18}, headers=headers)
    second = client.put(path, json={"coffeeDoseG": 18}, headers=headers)

    assert first.status_code == 200
    assert second.status_code == 200
    revisions = client.app.state.repository.list_documents(
        "extraction-revision",
        page=1,
        page_size=10,
        filters={"resourceId": "sim-20260904-003"},
    ).items
    assert len(revisions) == 1
    assert revisions[0]["category"] == "manual-measurements"


def test_retiring_profile_preserves_the_approved_document(client, barista_headers):
    repository = client.app.state.repository
    created = client.post(
        "/api/v1/profiles", json=profile_payload("Perfil inmutable"), headers=barista_headers
    ).json()
    approved = client.post(
        f"/api/v1/profiles/{created['id']}/approve", headers=barista_headers
    ).json()
    retired = client.post(f"/api/v1/profiles/{created['id']}/retire", headers=barista_headers)

    assert approved["status"] == "approved"
    assert retired.status_code == 200
    assert retired.json()["status"] == "retired"
    assert repository.get_document("profile", created["id"])["status"] == "approved"
    revision = repository.list_documents(
        "profile-revision",
        page=1,
        page_size=10,
        filters={"resourceId": created["id"]},
    ).items[0]
    assert revision["category"] == "profile-retired"
    assert revision["previousRecordId"] == created["id"]
    assert revision["recordedBy"] == "barista-test"


def test_decision_is_an_immutable_fact(client, barista_headers):
    recommendation = client.post(
        "/api/v1/recommendations",
        json={"extractionId": "ext-20260904-001"},
        headers=barista_headers,
    ).json()
    path = f"/api/v1/recommendations/{recommendation['recommendationId']}/decision"
    first = client.post(
        path,
        json={"status": "rejected", "comment": "No aplicar."},
        headers=barista_headers,
    )
    second = client.post(path, json={"status": "accepted"}, headers=barista_headers)
    stored = client.get(f"/api/v1/recommendations/{recommendation['recommendationId']}").json()
    assert first.status_code == 200
    assert second.status_code == 409
    assert stored["decision"]["status"] == "rejected"


def test_modified_recommendation_requires_justification_and_records_barista(
    client, barista_headers
):
    path = "/api/v1/recommendations/rec-demo-grind-001/decision"
    incomplete = client.post(
        path,
        json={
            "status": "modified",
            "modifiedAdjustment": {
                "property": "grind-setting",
                "direction": "increase",
                "value": 4.4,
                "unit": "punto",
            },
        },
        headers=barista_headers,
    )
    assert incomplete.status_code == 422

    recorded = client.post(
        path,
        json={
            "status": "modified",
            "comment": "Prefiero un cambio más pequeño para conservar el cuerpo.",
            "modifiedAdjustment": {
                "property": "grind-setting",
                "direction": "increase",
                "value": 4.4,
                "unit": "punto",
            },
        },
        headers=barista_headers,
    )
    assert recorded.status_code == 200
    decision = recorded.json()["decision"]
    assert decision["decidedBy"] == "barista-test"
    assert decision["decidedAt"]
    assert decision["modifiedAdjustment"]["value"] == 4.4


def test_adjustment_from_recommendation_requires_an_accepting_decision(client, barista_headers):
    payload = {
        "coffeeId": "coffee-huila-001",
        "initialExtractionId": "ext-20260904-001",
        "followUpExtractionId": "ext-20260904-002",
        "adjustment": {
            "property": "grind-setting",
            "previousValue": 4.2,
            "newValue": 4.5,
            "unit": "punto",
            "occurredAt": "2026-09-04T14:55:00Z",
            "recommendationId": "rec-demo-grind-001",
        },
        "targetOutcome": {"kind": "observable-property", "code": "balance"},
        "outcome": {
            "category": "expected-observed",
            "assessedAt": "2026-09-04T15:14:00Z",
            "rationale": "Mejora observada.",
        },
    }
    blocked = client.post("/api/v1/learning-episodes", json=payload, headers=barista_headers)
    assert blocked.status_code == 400

    decision = client.post(
        "/api/v1/recommendations/rec-demo-grind-001/decision",
        json={"status": "accepted"},
        headers=barista_headers,
    )
    assert decision.status_code == 200
    saved = client.post("/api/v1/learning-episodes", json=payload, headers=barista_headers)
    assert saved.status_code == 201
    assert saved.json()["adjustment"]["recommendationId"] == "rec-demo-grind-001"


def test_an_accepted_recommendation_becomes_the_configuration_that_governs(
    client, barista_headers
):
    """El ciclo entero: de aceptar una recomendación a lo que rige en la máquina.

    Es el tramo que cierra el gemelo digital. Hasta aquí la decisión del barista
    se registraba y se quedaba en la plataforma; ahora produce la configuración
    que la cafetera usará, sin que nadie la rehaga a mano.
    """
    decision = client.post(
        "/api/v1/recommendations/rec-demo-grind-001/decision",
        json={"status": "accepted"},
        headers=barista_headers,
    )
    assert decision.status_code == 200

    registered = client.post(
        "/api/v1/learning-episodes/adjustments",
        json={
            "initialExtractionId": "ext-20260904-001",
            "adjustment": {
                "property": "water-temperature",
                "previousValue": 93.0,
                "newValue": 92.0,
                "unit": "°C",
                "occurredAt": "2026-09-05T14:00:00Z",
                "rationale": "Bajar la temperatura para reducir amargor.",
                "recommendationId": "rec-demo-grind-001",
            },
            "targetOutcome": {"kind": "sensory-defect", "code": "amargor"},
        },
        headers=barista_headers,
    )
    assert registered.status_code == 201
    derived_id = registered.json()["derivedProfileId"]

    # La configuración derivada conserva de dónde viene y por qué vía rige.
    derived = client.get(f"/api/v1/profiles/{derived_id}", headers=barista_headers).json()
    assert derived["temperatureSetpointC"] == 92.0
    assert derived["derivedFrom"]["property"] == "water-temperature"
    assert derived["activation"]["via"] == "recommendation"
    assert derived["activation"]["recommendationId"] == "rec-demo-grind-001"

    # Y es la que baja a la máquina en la siguiente sincronización.
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-contract-test", "messages": []},
        headers=barista_headers,
    )
    bajan = {item["id"]: item for item in response.json()["profileProposals"]}
    assert derived_id in bajan
    assert bajan[derived_id]["temperatureSetpointC"] == 92.0


def test_a_derived_configuration_keeps_the_base_specifications(client, barista_headers):
    """Un ajuste de tiempo deriva un perfil completo, no la vista del contrato.

    El 2026-11-04 el perfil derivado de un ajuste de 25 a 20 s salió sin
    especificaciones, porque se copiaba de la vista que lleva la preparación,
    y la ontología rechazó la preparación siguiente (sim-00000578). El derivado
    tiene que heredar las especificaciones de la base, con la del tiempo
    movida al valor nuevo, y su familia y versión anterior.
    """
    assert client.post(
        "/api/v1/recommendations/rec-demo-grind-001/decision",
        json={"status": "accepted"},
        headers=barista_headers,
    ).status_code == 200
    initial = client.get("/api/v1/extractions/ext-20260904-001", headers=barista_headers).json()
    base = client.get(
        f"/api/v1/profiles/{initial['profile']['id']}", headers=barista_headers
    ).json()

    registered = client.post(
        "/api/v1/learning-episodes/adjustments",
        json={
            "initialExtractionId": "ext-20260904-001",
            "adjustment": {
                "property": "extraction-time",
                "previousValue": 25.0,
                "newValue": 20.0,
                "unit": "s",
                "occurredAt": "2026-09-05T14:00:00Z",
                "rationale": "Acortar para reducir astringencia.",
                "recommendationId": "rec-demo-grind-001",
            },
            "targetOutcome": {"kind": "sensory-defect", "code": "astringencia"},
        },
        headers=barista_headers,
    )
    assert registered.status_code == 201
    derived = client.get(
        f"/api/v1/profiles/{registered.json()['derivedProfileId']}", headers=barista_headers
    ).json()

    assert derived["maximumTimeMs"] == 20000
    assert derived["familyId"] == (base.get("familyId") or base["id"])
    assert derived["previousVersionId"] == base["id"]
    base_specs = base.get("specifications") or []
    assert base_specs, "la base de la prueba debe declarar especificaciones"
    assert len(derived["specifications"]) == len(base_specs)
    for before, after in zip(base_specs, derived["specifications"], strict=True):
        assert after["property"] == before["property"]
        if before["property"] in {"extraction-time", "time"}:
            assert float(after["maximum"]) == 20.0
        else:
            assert after == before


def test_a_manual_adjustment_does_not_reach_the_machine(client, barista_headers):
    """Sin recomendación detrás no se deriva nada: faltaría el eslabón que la justifica."""
    registered = client.post(
        "/api/v1/learning-episodes/adjustments",
        json={
            "initialExtractionId": "ext-20260904-001",
            "adjustment": {
                "property": "water-temperature",
                "previousValue": 93.0,
                "newValue": 92.5,
                "unit": "°C",
                "occurredAt": "2026-09-05T14:00:00Z",
                "rationale": "Prueba por criterio propio.",
            },
            "targetOutcome": {"kind": "sensory-defect", "code": "amargor"},
        },
        headers=barista_headers,
    )
    assert registered.status_code == 201
    assert "derivedProfileId" not in registered.json()


def test_a_real_coffee_is_linked_as_the_check_of_an_adjustment(client, barista_headers):
    """La comprobación es el café que el barista prepara en la máquina, no el simulador."""
    registered = client.post(
        "/api/v1/learning-episodes/adjustments",
        json={
            "initialExtractionId": "ext-20260904-001",
            "adjustment": {
                "property": "grind-setting",
                "previousValue": 12,
                "newValue": 11,
                "unit": "clics",
                "occurredAt": "2026-09-05T14:00:00Z",
            },
            "targetOutcome": {"kind": "sensory-defect", "code": "ExcessiveBitterness"},
        },
        headers=barista_headers,
    ).json()
    repository = client.app.state.repository
    coffee = repository.get_document("extraction", "ext-20260904-001").get("coffeeId")
    base = {"coffeeId": coffee, "finishedAt": "2026-09-05T14:10:00Z", "status": "closed"}
    repository.add_document("extraction", "prep-simulada", {
        **base, "id": "prep-simulada", "source": "simulation",
    })
    repository.add_document("extraction", "prep-antes", {
        **base, "id": "prep-antes", "source": "measured", "finishedAt": "2026-09-05T13:00:00Z",
    })
    repository.add_document("extraction", "prep-comprobacion", {
        **base, "id": "prep-comprobacion", "source": "measured",
    })
    path = f"/api/v1/learning-episodes/{registered['id']}/follow-up"

    def link(extraction_id: str):
        return client.post(path, json={"extractionId": extraction_id}, headers=barista_headers)

    assert link("prep-simulada").status_code == 400
    assert link("prep-antes").status_code == 400
    linked = link("prep-comprobacion")
    assert linked.status_code == 200
    assert linked.json()["followUpExtractionId"] == "prep-comprobacion"
    assert linked.json()["status"] == "verification-closed"
    # Sin series ni agua estimada no se puede saber si cambiaron las condiciones:
    # queda indeterminado, nunca atribuible (8-nov-2026).
    attribution = linked.json()["attribution"]
    assert attribution["status"] == "undetermined"
    assert attribution["followUpExtractionId"] == "prep-comprobacion"
    assert {check["condition"] for check in attribution["checks"]} == {"water", "pressure"}
    assert link("prep-comprobacion").status_code == 409


def test_a_grind_adjustment_produces_no_configuration(client, barista_headers):
    """La molienda la ejecuta el barista: derivar una configuración anotaría un
    cambio que la máquina no puede hacer."""
    client.post(
        "/api/v1/recommendations/rec-demo-grind-001/decision",
        json={"status": "accepted"},
        headers=barista_headers,
    )
    registered = client.post(
        "/api/v1/learning-episodes/adjustments",
        json={
            "initialExtractionId": "ext-20260904-001",
            "adjustment": {
                "property": "grind-setting",
                "previousValue": 4.2,
                "newValue": 4.4,
                "unit": "punto",
                "occurredAt": "2026-09-05T14:00:00Z",
                "rationale": "Reducir la acidez percibida.",
                "recommendationId": "rec-demo-grind-001",
            },
            "targetOutcome": {"kind": "sensory-defect", "code": "acidez elevada"},
        },
        headers=barista_headers,
    )
    assert registered.status_code == 201
    assert "derivedProfileId" not in registered.json()


def test_adjustment_starts_verification_and_only_then_accepts_a_traced_outcome(
    client, barista_headers
):
    registered = client.post(
        "/api/v1/learning-episodes/adjustments",
        json={
            "initialExtractionId": "ext-20260904-001",
            "adjustment": {
                "property": "grind-setting",
                "previousValue": 4.2,
                "newValue": 4.4,
                "unit": "punto",
                "occurredAt": "2026-09-05T14:00:00Z",
                "rationale": "Reducir la acidez percibida.",
            },
            "targetOutcome": {
                "kind": "sensory-defect",
                "code": "acidez elevada",
            },
        },
        headers=barista_headers,
    )
    assert registered.status_code == 201
    episode = registered.json()
    assert episode["adjustment"]["performedBy"] == "barista-test"
    assert episode["adjustment"]["registeredAt"]
    assert episode["status"] == "awaiting-verification"

    started = client.post(
        f"/api/v1/learning-episodes/{episode['id']}/verification",
        json={},
        headers=barista_headers,
    )
    assert started.status_code == 200
    verification = started.json()
    assert verification["verificationForEpisodeId"] == episode["id"]

    too_early = client.post(
        f"/api/v1/learning-episodes/{episode['id']}/outcome",
        json={"category": "expected-observed", "rationale": "Aún no terminó."},
        headers=barista_headers,
    )
    assert too_early.status_code == 404

    finished = client.post(
        f"/api/v1/extractions/{verification['id']}/finish",
        json={"cause": "completed"},
        headers=barista_headers,
    )
    assert finished.status_code == 200
    assessed = client.post(
        f"/api/v1/learning-episodes/{episode['id']}/outcome",
        json={
            "category": "expected-observed",
            "rationale": "La observación se acercó al resultado buscado.",
        },
        headers=barista_headers,
    )
    assert assessed.status_code == 200
    outcome = assessed.json()["outcome"]
    assert outcome["assessedBy"] == "barista-test"
    assert outcome["comparativeEvidence"]


def test_comparison_requires_unique_known_extractions(client):
    response = client.post(
        "/api/v1/extractions/compare",
        json={
            "ids": ["ext-20260904-001", "ext-20260904-002"],
            "dimensions": [
                "profile",
                "coffee",
                "equipment",
                "barista",
                "environment",
                "preparation",
                "sensory",
                "curves",
                "adjustments",
            ],
        },
    )
    assert response.status_code == 200
    comparison = response.json()
    assert len(comparison["items"]) == 2
    assert comparison["dimensions"][-1] == "adjustments"
    assert comparison["items"][0]["profile"]["version"] == 3
    assert comparison["items"][1]["coffeeSnapshot"]["variety"] == "Caturra"
    duration = next(metric for metric in comparison["metrics"] if metric["key"] == "durationMs")
    assert duration["comparisons"][0]["status"] == "equal"
    assert duration["comparisons"][1]["absolute"] == 1770
    assert duration["comparisons"][1]["relativePercent"] == 6.07
    environment = next(
        metric for metric in comparison["metrics"] if metric["key"] == "ambientTemperatureC"
    )
    assert environment["comparisons"][1]["status"] == "different"
    assert len(comparison["series"]) == 2
    assert comparison["series"][1]["samples"][1]["elapsedMs"] == 10000
    assert comparison["sharedCalibrationId"] == "calibracion-turno-01"
    assert comparison["adjustmentSequence"][0]["property"] == "grind-setting"

    preparation_only = client.post(
        "/api/v1/extractions/compare",
        json={
            "ids": ["ext-20260904-001", "ext-20260904-002"],
            "dimensions": ["preparation"],
        },
    ).json()
    assert preparation_only["series"] == []
    assert all(metric["dimension"] == "preparation" for metric in preparation_only["metrics"])


def test_openapi_contains_no_actuation_route(client):
    paths = client.get("/api/openapi.json").json()["paths"]
    forbidden = ("actuator", "heater", "pump", "start-extraction", "control")
    assert not any(term in path.lower() for path in paths for term in forbidden)


def test_clean_architecture_dependencies():
    root = Path(__file__).parents[1] / "src" / "aplicacion_web"
    domain = "\n".join(path.read_text() for path in (root / "domain").glob("*.py"))
    application = "\n".join(path.read_text() for path in (root / "application").glob("*.py"))
    assert "fastapi" not in domain.lower()
    assert "sqlalchemy" not in domain.lower()
    assert "fastapi" not in application.lower()
    assert "sqlalchemy" not in application.lower()


def _telemetry_at(
    occurred_at,
    temperature,
    *,
    message_id,
    quality="valid",
    device="device-campaign-1",
    preparation=None,
):
    return {
        "schemaVersion": "1.0",
        "messageId": message_id,
        "messageType": "telemetry.sample",
        "deviceId": device,
        "sequence": 1,
        "occurredAt": occurred_at,
        "payload": {
            "source": "measured",
            "preparationId": preparation,
            "state": "extraction" if preparation else "ready",
            "elapsedMs": 0,
            "temperature": {"value": temperature, "unit": "Cel", "quality": quality},
            "pressure": {"value": 0.0, "unit": "bar", "quality": "valid"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": False, "pumpDutyPercent": 0, "virtual": False},
        },
    }


def _sync(client, headers, *messages):
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": messages[0]["deviceId"], "messages": list(messages)},
        headers=headers,
    )
    assert response.status_code == 200


@pytest.fixture
def platform_clock(monkeypatch):
    """Fija la hora de la plataforma, que es la que sella cada lectura."""
    from aplicacion_web.application import services

    instant = datetime(2026, 10, 1, 9, 0, tzinfo=UTC)
    monkeypatch.setattr(services, "utc_now", lambda: instant)
    return instant


def _reading(**overrides):
    return {
        "deviceId": "device-campaign-1",
        "takenAt": "2026-10-01T09:00:00Z",
        "waterTemperatureC": 90.4,
        "instrument": "Termómetro de cocina TP101",
        "condition": "rest",
        **overrides,
    }


def test_a_water_reading_takes_the_body_temperature_from_the_series(
    client, barista_headers, platform_clock
):
    """ADR-019 § 4.3: la temperatura del cuerpo sale de la serie, no se teclea.

    El instante de la muestra llega con otra zona horaria a propósito: el
    emparejamiento debe comparar instantes, no textos.
    """
    _sync(
        client,
        barista_headers,
        _telemetry_at("2026-10-01T03:59:59.400-05:00", 94.1, message_id="campaign-sample-1"),
        _telemetry_at("2026-10-01T09:00:02Z", 95.0, message_id="campaign-sample-2"),
    )

    response = client.post("/api/v1/water-references", json=_reading(), headers=barista_headers)

    assert response.status_code == 201
    pairing = response.json()["pairing"]
    assert pairing["status"] == "paired"
    assert pairing["sampleId"] == "campaign-sample-1"
    assert pairing["gapMs"] == 600
    assert pairing["bodyTemperatureC"] == 94.1
    assert pairing["temperatureQuality"] == "valid"
    assert pairing["source"] == "measured"


def test_a_water_reading_is_paired_at_the_platform_instant_not_the_browser_one(
    client, barista_headers, platform_clock
):
    """ADR-019 § 6.2: el dispositivo toma la hora de la plataforma, así que el
    par se fija con ese mismo reloj. Un navegador adelantado cinco segundos no
    deja la lectura sin pareja; su hora queda guardada con el desfase."""
    _sync(
        client,
        barista_headers,
        _telemetry_at("2026-10-01T09:00:00.300Z", 94.2, message_id="campaign-sample-now"),
        _telemetry_at("2026-10-01T09:00:05Z", 96.0, message_id="campaign-sample-browser"),
    )

    response = client.post(
        "/api/v1/water-references",
        json=_reading(takenAt="2026-10-01T09:00:05Z"),
        headers=barista_headers,
    )

    document = response.json()
    assert document["takenAt"] == "2026-10-01T09:00:00.000000Z"
    assert document["takenAtSource"] == "platform"
    assert document["declaredTakenAt"] == "2026-10-01T09:00:05.000000Z"
    assert document["clockOffsetMs"] == 5000
    assert document["pairing"]["sampleId"] == "campaign-sample-now"


def test_a_water_reading_needs_no_browser_instant(client, barista_headers, platform_clock):
    reading = _reading()
    del reading["takenAt"]

    response = client.post("/api/v1/water-references", json=reading, headers=barista_headers)

    assert response.status_code == 201
    document = response.json()
    assert document["declaredTakenAt"] is None
    assert document["clockOffsetMs"] is None


def test_the_water_pairs_export_carries_every_reading_with_its_provenance(
    client, barista_headers, platform_clock, monkeypatch
):
    """El ajuste de la transformación (ADR-019 § 4.2) parte de esta exportación:
    van todas las lecturas, también las que quedaron sin pareja, y la cabecera
    describe exactamente las columnas de cada fila."""
    import csv
    import io

    from aplicacion_web.application import services

    _sync(
        client,
        barista_headers,
        _telemetry_at("2026-10-01T09:00:00.200Z", 94.3, message_id="export-sample"),
    )
    paired = client.post("/api/v1/water-references", json=_reading(), headers=barista_headers)
    monkeypatch.setattr(
        services, "utc_now", lambda: datetime(2026, 10, 1, 9, 10, tzinfo=UTC)
    )
    unpaired = client.post(
        "/api/v1/water-references",
        json=_reading(condition="after-extraction"),
        headers=barista_headers,
    )

    response = client.get("/api/v1/water-references/export", headers=barista_headers)

    assert response.status_code == 200
    assert response.headers["content-type"].startswith("text/csv")
    rows = list(csv.DictReader(io.StringIO(response.text)))
    assert [row["id"] for row in rows] == [paired.json()["id"], unpaired.json()["id"]]
    first, second = rows
    assert first["pairingStatus"] == "paired"
    assert first["bodyTemperatureC"] == "94.3"
    assert first["sampleId"] == "export-sample"
    assert first["gapMs"] == "200"
    assert first["temperatureQuality"] == "valid"
    assert first["bodySource"] == "measured"
    assert first["waterTemperatureC"] == "90.4"
    assert second["pairingStatus"] == "unpaired"
    assert second["pairingReason"] == "no-sample-within-margin"
    assert second["bodyTemperatureC"] == ""
    assert second["bodySource"] == ""


def test_a_water_reading_rejects_a_typed_body_temperature(client, barista_headers):
    response = client.post(
        "/api/v1/water-references", json=_reading(bodyTemperatureC=94.0), headers=barista_headers
    )

    assert response.status_code == 422


def test_a_water_reading_without_a_nearby_sample_is_kept_unpaired(
    client, barista_headers, platform_clock
):
    """Con el periodo de reposo de 15 s la lectura se queda sin pareja: no se
    empareja con una muestra lejana ni se descarta."""
    _sync(
        client,
        barista_headers,
        _telemetry_at("2026-10-01T08:59:52Z", 94.0, message_id="campaign-sample-far"),
    )

    response = client.post("/api/v1/water-references", json=_reading(), headers=barista_headers)

    assert response.status_code == 201
    document = response.json()
    assert document["pairing"]["status"] == "unpaired"
    assert "bodyTemperatureC" not in document["pairing"]
    listed = client.get("/api/v1/water-references", headers=barista_headers).json()["items"]
    assert [item["id"] for item in listed] == [document["id"]]


def test_a_water_reading_does_not_pair_with_another_device(
    client, barista_headers, platform_clock
):
    _sync(
        client,
        barista_headers,
        _telemetry_at(
            "2026-10-01T09:00:00Z", 94.0, message_id="other-device-sample", device="other-device"
        ),
    )

    response = client.post("/api/v1/water-references", json=_reading(), headers=barista_headers)

    assert response.json()["pairing"]["status"] == "unpaired"


def test_a_water_reading_during_an_extraction_names_its_preparation(
    client, barista_headers, platform_clock
):
    """Sin preparación escrita y sin muestra que la diga, se rechaza: ADR-019
    § 4.3 la exige durante una extracción."""
    response = client.post(
        "/api/v1/water-references",
        json=_reading(condition="during-extraction"),
        headers=barista_headers,
    )

    assert response.status_code == 400
    assert "preparación" in response.json()["error"]["message"]


def test_a_water_reading_during_an_extraction_takes_the_preparation_from_its_sample(
    client, barista_headers, platform_clock
):
    """Teclear el identificador en mitad de la extracción retrasaba la lectura;
    la muestra emparejada ya dice qué preparación corría."""
    _sync(
        client,
        barista_headers,
        _telemetry_at(
            "2026-10-01T09:00:00.400Z", 92.1, message_id="during-sample", preparation="prep-0042"
        ),
    )

    response = client.post(
        "/api/v1/water-references",
        json=_reading(condition="during-extraction"),
        headers=barista_headers,
    )

    assert response.status_code == 201
    document = response.json()
    assert document["extractionId"] == "prep-0042"
    assert document["extractionIdSource"] == "telemetry"


def test_a_written_preparation_prevails_and_the_sample_one_is_kept(
    client, barista_headers, platform_clock
):
    _sync(
        client,
        barista_headers,
        _telemetry_at(
            "2026-10-01T09:00:00.400Z", 92.1, message_id="during-sample", preparation="prep-0042"
        ),
    )

    response = client.post(
        "/api/v1/water-references",
        json=_reading(condition="during-extraction", extractionId="prep-0041"),
        headers=barista_headers,
    )

    document = response.json()
    assert document["extractionId"] == "prep-0041"
    assert document["extractionIdSource"] == "declared"
    assert document["pairing"]["sampleExtractionId"] == "prep-0042"


def test_a_preparation_resent_with_another_message_is_a_duplicate(client, barista_headers):
    """La misma preparación con otro `messageId` no rompe la sincronización.

    Antes violaba la unicidad del documento y el lote entero fallaba con un 500,
    perdiendo también los mensajes válidos que lo acompañaban.
    """

    def completed(message_id: str, duration_ms: int) -> dict:
        return {
            "schemaVersion": "1.0",
            "messageId": message_id,
            "messageType": "extraction.completed",
            "deviceId": "device-test",
            "sequence": 1,
            "occurredAt": "2026-10-09T10:00:00Z",
            "payload": {"id": "prep-repetida", "source": "measured", "durationMs": duration_ms},
        }

    first = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-test", "messages": [completed("msg-a", 28000)]},
        headers=barista_headers,
    )
    assert first.status_code == 200
    second = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-test", "messages": [completed("msg-b", 99999)]},
        headers=barista_headers,
    )
    assert second.status_code == 200
    assert second.json()["duplicates"] == 1
    stored = client.app.state.repository.get_document("extraction", "prep-repetida")
    assert stored["durationMs"] == 28000


def test_a_preparation_keeps_the_calibration_and_firmware_it_was_measured_with(
    client, barista_headers
):
    """La preparación conserva con qué se midió, tal como lo declaró el dispositivo."""
    message = {
        "schemaVersion": "1.0",
        "messageId": "msg-con-calibracion",
        "messageType": "extraction.completed",
        "deviceId": "device-test",
        "sequence": 1,
        "occurredAt": "2026-10-09T10:00:00Z",
        "payload": {
            "id": "prep-con-calibracion",
            "source": "measured",
            "calibration": {"id": "ntc-2026-10-09", "version": 4},
            "firmwareVersion": "861347f",
        },
    }
    response = client.post(
        "/api/v1/sync", json={"deviceId": "device-test", "messages": [message]},
        headers=barista_headers,
    )
    assert response.status_code == 200
    stored = client.app.state.repository.get_document("extraction", "prep-con-calibracion")
    assert stored["calibration"] == {"id": "ntc-2026-10-09", "version": 4}
    assert stored["firmwareVersion"] == "861347f"
    # La evaluación queda guardada al sincronizar y dice con qué se midió. Sin
    # perfil no hay nada que juzgar, y eso no es conformidad.
    assessments = client.get(
        "/api/v1/extractions/prep-con-calibracion/conformity-assessments"
    ).json()["items"]
    assert [item["trigger"] for item in assessments] == ["extraction-synced"]
    assert assessments[0]["provenance"]["calibration"] == {"id": "ntc-2026-10-09", "version": 4}
    assert assessments[0]["technicalConformance"]["status"] == "insufficient-information"


def _power_loss_message(**payload_changes) -> dict:
    """Lo que el dispositivo envía al rearrancar tras un corte de energía."""
    from test_contracts import POWER_LOSS

    return {
        "schemaVersion": "1.0",
        "messageId": "msg-corte-energia",
        "messageType": "extraction.completed",
        "deviceId": "device-test",
        "sequence": 12,
        "occurredAt": "2026-10-12T09:05:00Z",
        "payload": {**POWER_LOSS, **payload_changes},
    }


def test_a_preparation_cut_by_power_loss_does_not_fail_the_sync_batch(client, barista_headers):
    """ADR-005 § 8 punto 10: un 4xx en el lote bloquearía la cola entera del dispositivo."""
    telemetry = {
        "schemaVersion": "1.0",
        "messageId": "msg-tras-el-corte",
        "messageType": "operational.event",
        "deviceId": "device-test",
        "sequence": 13,
        "occurredAt": "2026-10-12T09:05:01Z",
        "payload": {"type": "state-transition", "from": "boot", "to": "ready"},
    }
    response = client.post(
        "/api/v1/sync",
        json={"deviceId": "device-test", "messages": [_power_loss_message(), telemetry]},
        headers=barista_headers,
    )
    assert response.status_code == 200
    assert response.json()["acknowledged"] == ["msg-corte-energia", "msg-tras-el-corte"]
    assert response.json()["inserted"] == 2

    stored = client.app.state.repository.get_document("extraction", "prep-cortada-0001")
    # Lo que no consta queda nulo: ni se rellena con el instante del rearranque
    # ni se calcula una duración.
    assert stored["startedAt"] is None
    assert stored["finishedAt"] is None
    assert stored["durationMs"] is None
    assert stored["termination"] == "unexpected-power-loss"
    assert stored["calibration"] == {"id": "ntc-2026-10-09", "version": 4}
    assert stored["firmwareVersion"] == "861347f"


def test_a_preparation_cut_by_power_loss_is_not_judged(client, barista_headers):
    """Sin serie ni indicadores no hay evaluación ni réplica que hacer."""
    client.post(
        "/api/v1/sync",
        json={"deviceId": "device-test", "messages": [_power_loss_message()]},
        headers=barista_headers,
    )
    identifier = "prep-cortada-0001"

    assessments = client.get(f"/api/v1/extractions/{identifier}/conformity-assessments")
    assert assessments.json()["items"] == []
    result = client.get(f"/api/v1/extractions/{identifier}/result").json()
    assert result["technicalConformance"]["status"] == "insufficient-information"
    assert result["properties"] == []
    assert result["assessment"]["recorded"] is False
    assert result["assessment"]["provenance"]["calibration"] == {
        "id": "ntc-2026-10-09",
        "version": 4,
    }
    assert "corte de energía" in result["diagnosis"]["conclusion"]

    simulation = client.post(
        "/api/v1/simulations",
        json={
            "model": {"id": "espresso-detailed", "version": "1.0.0"},
            "replicates": {"extractionId": identifier},
        },
        headers={**barista_headers, "Idempotency-Key": "replica-corte"},
    )
    assert simulation.status_code == 400
    assert "corte de energía" in simulation.json()["error"]["message"]


def test_the_history_lists_a_preparation_without_a_known_end(client, barista_headers):
    """El historial ordenaba por final y fallaba con un 500 si faltaba."""
    client.post(
        "/api/v1/sync",
        json={"deviceId": "device-test", "messages": [_power_loss_message()]},
        headers=barista_headers,
    )
    listed = client.get("/api/v1/extractions?page=1")
    assert listed.status_code == 200
    assert "prep-cortada-0001" in [item["id"] for item in listed.json()["items"]]

    # Sin final, un filtro por fechas la sitúa donde la ordena el historial: en
    # el rearranque que la cerró, que es cuando se supo de ella.
    def filtered(start: str, end: str) -> list[str]:
        response = client.get(f"/api/v1/extractions?page=1&from={start}&to={end}")
        assert response.status_code == 200
        return [item["id"] for item in response.json()["items"]]

    assert "prep-cortada-0001" in filtered("2026-10-12T00:00:00Z", "2026-10-13T00:00:00Z")
    assert "prep-cortada-0001" not in filtered("2026-10-01T00:00:00Z", "2026-10-11T00:00:00Z")
    assert client.get("/api/v1/dashboard").status_code == 200


def test_the_barista_guide_estimates_inside_what_was_measured_only(client, barista_headers):
    guide = client.get(
        "/api/v1/barista-guide?cutSeconds=20&ratio=2&doseG=13", headers=barista_headers
    ).json()
    assert {fact["status"] for fact in guide["facts"]} == {"measured"}
    assert guide["cupMass"]["status"] == "estimated"
    assert guide["timeForRatio"]["status"] == "estimated"
    outside = client.get(
        "/api/v1/barista-guide?cutSeconds=40", headers=barista_headers
    ).json()
    assert outside["cupMass"]["status"] == "no-data"
    assert outside["timeForRatio"] is None


def test_the_minute_before_pressing_only_feeds_the_machine_stability(client, barista_headers):
    """La placa envía el minuto previo con elapsedMs negativo: no es la preparación."""
    extraction_id = _closed_preparation(client, barista_headers, [
        PRESSURE_SPECIFICATION,
        {"property": "water-temperature", "unit": "°C", "strategy": "target",
         "target": 92, "tolerance": 1},
    ])
    repository = client.app.state.repository
    extraction = client.get(f"/api/v1/extractions/{extraction_id}").json()
    from espresso_profiles import profile_reference
    from espresso_profiles.water_range import line_of

    water, _ = profile_reference(extraction["profile"], "temperature")
    # La referencia es la de la recta con que se hizo el café, no la vigente.
    boiler = line_of(extraction).boiler_reference_c(water)
    for index in range(120):
        repository.add_document("live-sample", f"{extraction_id}-previo-{index}", {
            "id": f"{extraction_id}-previo-{index}",
            "extractionId": extraction_id,
            "sequence": -1000 + index,
            "elapsedMs": -60_000 + index * 500,
            "temperatureC": boiler,
            "pressureBar": 0.0,
            "quality": "valid",
            "source": "measured",
        })
    series = client.get(f"/api/v1/extractions/{extraction_id}/series").json()["samples"]
    assert all(int(sample["elapsedMs"]) >= 0 for sample in series)
    thermal = client.get(f"/api/v1/extractions/{extraction_id}/result").json()[
        "machineStability"
    ]["thermal"]
    assert thermal["status"] == "measured"
    assert thermal["sampleCount"] == 120
    assert thermal["fraction"] == 1.0


def test_the_dashboard_knows_the_machine_state_since_when_and_why(client, barista_headers):
    from datetime import UTC, datetime, timedelta

    repository = client.app.state.repository
    now = datetime.now(UTC)
    entered = (now - timedelta(minutes=3)).isoformat().replace("+00:00", "Z")
    repository.add_document("event", "evento-lista", {
        "id": "evento-lista", "kind": "state-transition", "state": "ready",
        "previousState": "coffee-heating", "severity": "info",
        "code": "coffee-temperature-ready", "description": "Caldera en temperatura",
        "occurredAt": entered,
    })
    repository.add_document("live-sample", "tlm-prueba", {
        "id": "tlm-prueba", "source": "measured", "state": "ready", "elapsedMs": 0,
        "occurredAtUtc": now.isoformat().replace("+00:00", "Z"),
        "temperature": {"value": 101.8, "unit": "Cel", "quality": "valid"},
        "pressure": {"value": 0.1, "unit": "bar", "quality": "valid"},
        "targets": {"temperatureC": 92.0, "pressureBar": 8.5},
        "actuators": {"heaterOn": False, "pumpDutyPercent": 0, "virtual": False},
    })
    state = client.get("/api/v1/machine-state", headers=barista_headers).json()
    assert state["connected"] is True
    assert state["state"] == "ready"
    assert state["since"] == entered
    assert state["reason"]["code"] == "coffee-temperature-ready"
    # El panel habla de la placa de ahora: la recta vigente (acta v12).
    from espresso_profiles.water_range import CURRENT
    assert state["boilerReferenceC"] == pytest.approx(CURRENT.boiler_reference_c(92.0), abs=0.01)
    # El agua de ahora sale de la caldera por la misma recta que el resultado.
    from espresso_stability.machine import estimated_water_c
    assert state["estimatedWaterC"] == estimated_water_c(101.8)
    assert state["estimatedWaterBound"] is None


def test_without_the_transition_notice_the_machine_state_since_comes_from_telemetry(
    client, barista_headers
):
    """El aviso que no llega no deja el panel sin «desde cuándo», ni con uno viejo."""
    from datetime import UTC, datetime, timedelta

    repository = client.app.state.repository
    now = datetime.now(UTC)

    def instant(minutes: float) -> str:
        return (now - timedelta(minutes=minutes)).isoformat().replace("+00:00", "Z")

    # Un aviso de «lista» de una vez anterior: después hubo un café.
    repository.add_document("event", "evento-lista-viejo", {
        "id": "evento-lista-viejo", "kind": "state-transition", "state": "ready",
        "code": "coffee-temperature-ready", "occurredAt": instant(20),
    })
    for index, (state, minutes) in enumerate(
        [("extraction", 10), ("coffee-heating", 9), ("ready", 4), ("ready", 0)]
    ):
        repository.add_document("live-sample", f"tlm-sin-aviso-{index}", {
            "id": f"tlm-sin-aviso-{index}", "source": "measured", "state": state,
            "occurredAtUtc": instant(minutes),
            "temperature": {"value": 101.8}, "targets": {"temperatureC": 92.0},
        })
    state = client.get("/api/v1/machine-state", headers=barista_headers).json()
    assert state["state"] == "ready"
    assert state["since"] == instant(4)
    assert state["sinceSource"] == "telemetry"
    assert state["reason"] is None


def test_the_coffee_in_progress_shows_what_the_board_is_measuring(client, barista_headers):
    """Las muestras de estado del café en curso, en orden; sin café, ninguna."""
    from datetime import UTC, datetime, timedelta

    repository = client.app.state.repository
    now = datetime.now(UTC)

    def sample(identifier, seconds_ago, state, elapsed, preparation="prep-en-curso"):
        repository.add_document("live-sample", identifier, {
            "id": identifier, "source": "measured", "state": state,
            "preparationId": preparation, "extractionId": preparation,
            "elapsedMs": elapsed, "temperatureC": 101.0, "pressureBar": elapsed / 1000,
            "quality": "valid",
            "occurredAtUtc": (now - timedelta(seconds=seconds_ago))
            .isoformat().replace("+00:00", "Z"),
        })

    idle = client.get("/api/v1/machine-state/live-preparation", headers=barista_headers).json()
    assert idle == {"active": False, "preparationId": None, "samples": []}
    sample("tlm-otro", 30, "extraction", 5000, preparation="prep-anterior")
    sample("tlm-vivo-1", 2, "extraction", 1000)
    sample("tlm-vivo-2", 1, "extraction", 2000)
    live = client.get("/api/v1/machine-state/live-preparation", headers=barista_headers).json()
    assert live["active"] is True
    assert live["preparationId"] == "prep-en-curso"
    assert [item["elapsedMs"] for item in live["samples"]] == [1000, 2000]
    sample("tlm-vivo-fin", 0, "coffee-heating", 0, preparation=None)
    done = client.get("/api/v1/machine-state/live-preparation", headers=barista_headers).json()
    assert done["active"] is False


def test_the_context_is_completed_in_parts_and_keeps_its_history(client, barista_headers):
    """Café y equipos se ponen antes o después, por partes; lo medido no cambia."""
    extraction_id = _closed_preparation(client, barista_headers, [PRESSURE_SPECIFICATION])
    before = client.get(f"/api/v1/extractions/{extraction_id}").json()
    machine = client.get("/api/v1/equipment", headers=barista_headers).json()["items"][0]["id"]
    first = client.put(f"/api/v1/extractions/{extraction_id}/context",
                       json={"grindSetting": 38, "notes": "molino recién limpiado"},
                       headers=barista_headers)
    assert first.status_code == 200
    second = client.put(f"/api/v1/extractions/{extraction_id}/context",
                        json={"coffeeId": "coffee-huila-001", "machineId": machine},
                        headers=barista_headers)
    assert second.status_code == 200
    after = client.get(f"/api/v1/extractions/{extraction_id}").json()
    assert after["grindSetting"] == 38
    assert after["coffeeId"] == "coffee-huila-001"
    assert after["machineId"] == machine
    assert len(after["revisions"]) == len(before.get("revisions") or []) + 2
    for measured in ("durationMs", "sampleCount", "statistics", "termination"):
        assert after.get(measured) == before.get(measured)
    empty = client.put(f"/api/v1/extractions/{extraction_id}/context", json={},
                       headers=barista_headers)
    assert empty.status_code == 400


# Sin autor en la semilla: es lo que llega de la placa, que no sabe quién pulsó.
UNAUTHORED = "sim-20260904-003"
OTHER_BARISTA = {"X-Test-Role": "barista", "X-Test-Actor": "other-barista"}


def test_the_first_person_to_record_on_a_preparation_becomes_its_author(
    client, barista_headers
):
    """El autor queda como una revisión más y nadie lo cambia después."""
    assert client.get(f"/api/v1/extractions/{UNAUTHORED}").json().get("barista") is None
    first = client.put(f"/api/v1/extractions/{UNAUTHORED}/manual-measurements",
                       json={"coffeeDoseG": 13}, headers=barista_headers)
    assert first.status_code == 200
    assert first.json()["barista"] == "barista-test"
    assert first.json()["revisions"][-1]["recordedBy"] == "barista-test"
    later = client.put(f"/api/v1/extractions/{UNAUTHORED}/context",
                       json={"notes": "corrección ajena"}, headers=OTHER_BARISTA)
    assert later.json()["barista"] == "barista-test"
    # Una preparación con autor tampoco lo pierde.
    authored = client.put("/api/v1/extractions/ext-20260904-002/context",
                          json={"notes": "otra nota"}, headers=barista_headers)
    assert authored.json()["barista"] == "barista-lab-01"

    mine = client.get("/api/v1/extractions",
                      params={"page": 1, "barista": "barista-test"}).json()["items"]
    assert [item["id"] for item in mine] == [UNAUTHORED]
    trace = client.get("/api/v1/traceability/barista/barista-test",
                       headers=barista_headers).json()
    assert any(node["id"] == f"extraction:{UNAUTHORED}" for node in trace["nodes"])
    semantic = client.get(f"/api/v1/extractions/{UNAUTHORED}/semantic",
                          headers=barista_headers)
    assert semantic.status_code == 200, semantic.text
    assert "resource/barista/barista-test" in str(semantic.json()["@graph"])


def test_an_old_preparation_without_author_keeps_none(client, barista_headers):
    """Leerla no le inventa un autor: solo registrar algo sobre ella."""
    client.get(f"/api/v1/extractions/{UNAUTHORED}", headers=barista_headers)
    client.get("/api/v1/extractions", params={"page": 1}, headers=barista_headers)
    assert client.get(f"/api/v1/extractions/{UNAUTHORED}").json().get("barista") is None


def test_the_export_follows_the_history_filters(client, barista_headers):
    client.put(f"/api/v1/extractions/{UNAUTHORED}/context", json={"notes": "mía"},
               headers=barista_headers)
    admin = {"X-Test-Role": "admin", "X-Test-Actor": "admin-test"}
    everything = client.get("/api/v1/extractions/export", headers=admin).json()
    assert len(everything["exported"]) > 1
    mine = client.get("/api/v1/extractions/export",
                      params={"barista": "barista-test"}, headers=admin).json()
    assert [item["id"] for item in mine["exported"]] == [UNAUTHORED]
    csv_text = client.get("/api/v1/extractions/export",
                          params={"barista": "barista-test", "format": "csv"},
                          headers=admin).text
    assert len(csv_text.strip().splitlines()) == 2


def test_a_barista_sees_only_their_own_preparations(client, barista_headers):
    """Lo decide la sesión: ni omitir el filtro ni pedir otro barista lo salta."""
    client.put(f"/api/v1/extractions/{UNAUTHORED}/context", json={"notes": "mía"},
               headers=barista_headers)
    for params in ({}, {"barista": "otro-barista"}):
        listed = client.get("/api/v1/extractions", params={"page": 1, **params},
                            headers=barista_headers).json()
        assert [item["id"] for item in listed["items"]] == [UNAUTHORED]
        exported = client.get("/api/v1/extractions/export", params=params,
                              headers=barista_headers).json()
        assert [item["id"] for item in exported["exported"]] == [UNAUTHORED]


def test_preparar_cafe_finds_the_new_coffee_that_the_history_hides(client, barista_headers):
    """El café recién llegado no tiene barista: el historial no lo muestra, la búsqueda sí."""
    repository = client.app.state.repository
    other = repository.get_document("extraction", "ext-20260904-001")
    assert other["source"] == "measured" and other["barista"]
    fresh = {key: value for key, value in other.items() if key != "barista"}
    fresh["id"] = "ext-recien-llegado"
    repository.add_document("extraction", fresh["id"], fresh)

    listed = client.get("/api/v1/extractions", params={"page": 1}, headers=barista_headers).json()
    assert fresh["id"] not in [item["id"] for item in listed["items"]]
    claimable = client.get("/api/v1/extractions/claimable", headers=barista_headers).json()
    ids = [item["id"] for item in claimable["items"]]
    assert fresh["id"] in ids
    # El café de otro barista no se puede tomar, y uno simulado tampoco.
    assert "ext-20260904-001" not in ids
    assert all(item["source"] == "measured" for item in claimable["items"])


def test_the_context_records_the_room_and_the_tasting_session_by_parts(
    client, barista_headers
):
    path = f"/api/v1/extractions/{UNAUTHORED}/context"
    first = client.put(path, json={
        "ambientTemperatureC": 23.4,
        "tastingSession": {"sessionCode": "cata-1", "blindCode": "kxt", "block": "C"},
    }, headers=barista_headers)
    assert first.status_code == 200, first.text
    second = client.put(path, json={
        "ambientHumidityPercent": 61,
        "tastingSession": {"condition": "sostenimiento activo"},
    }, headers=barista_headers).json()
    assert second["environment"] == {"temperatureC": 23.4, "humidityPercent": 61}
    assert second["tastingSession"] == {
        "sessionCode": "cata-1", "blindCode": "KXT", "block": "C",
        "condition": "sostenimiento activo",
    }
    # El cuarto llega al grafo por donde lo publica el trabajador semántico,
    # como las demás observaciones; la sesión no tiene término en la ontología.
    from espresso_semantics.extraction import EDT, extraction_graph

    from aplicacion_web.application.observations import preparation_observations
    from aplicacion_web.application.revisions import with_revisions

    repository = client.app.state.repository
    stored = with_revisions(repository, "extraction",
                            repository.get_document("extraction", UNAUTHORED))
    profile = repository.get_document("profile", stored["profile"]["id"])
    graph = extraction_graph(stored, profile, preparation_observations(stored, [], None))
    observed = set(graph.objects(None, EDT.observedProperty))
    assert {EDT.AmbientTemperature, EDT.RelativeHumidity} <= observed
    for invalid in (
        {"ambientTemperatureC": 80},
        {"ambientHumidityPercent": 101},
        {"tastingSession": {"blindCode": "AB"}},
        {"tastingSession": {"block": "F"}},
        {"tastingSession": {}},
    ):
        assert client.put(path, json=invalid, headers=barista_headers).status_code == 422


def test_the_sensory_sheet_keeps_serving_temperature_and_minutes(client, barista_headers):
    sheet = {
        "id": "eval-servicio",
        "evaluatedAt": "2026-09-04T16:10:00Z",
        "protocolReference": "docs/engineering/protocolo-catacion.md",
        "servingTemperatureC": 62.5,
        "minutesSinceExtraction": 4,
        "scores": [{"attribute": "OverallLiking", "dimension": "hedonic-liking", "value": 7}],
        "acceptance": {"status": "accepted"},
    }
    path = f"/api/v1/extractions/{UNAUTHORED}/sensory-evaluation"
    saved = client.put(path, json=sheet, headers=barista_headers)
    assert saved.status_code == 200, saved.text
    evaluation = saved.json()["sensoryEvaluation"]
    assert evaluation["servingTemperatureC"] == 62.5
    assert evaluation["minutesSinceExtraction"] == 4
    assert saved.json()["barista"] == "barista-test"
    for invalid in ({"minutesSinceExtraction": -1}, {"servingTemperatureC": 120}):
        response = client.put(path, json={**sheet, **invalid}, headers=barista_headers)
        assert response.status_code == 422


def test_the_tds_mean_comes_from_its_readings_and_a_correction_is_a_revision(
    client, barista_headers
):
    path = f"/api/v1/extractions/{UNAUTHORED}/manual-measurements"
    read = client.put(path, json={"tdsReadings": [9.5, 9.6, 9.7]}, headers=barista_headers)
    assert read.status_code == 200, read.text
    measurements = read.json()["manualMeasurements"]
    assert measurements["tdsPercent"] == 9.6
    assert measurements["tdsReadings"] == [9.5, 9.6, 9.7]
    assert measurements["entries"]["tdsPercent"]["readings"] == [9.5, 9.6, 9.7]
    revisions = len(read.json()["revisions"])

    corrected = client.put(path, json={"tdsPercent": 9.8}, headers=barista_headers).json()
    assert corrected["manualMeasurements"]["tdsPercent"] == 9.8
    # La media nueva no sale de las lecturas viejas: se quitan.
    assert "tdsReadings" not in corrected["manualMeasurements"]
    assert len(corrected["revisions"]) == revisions + 1

    for invalid in (
        {"tdsReadings": [9.5, 9.6, 9.7, 9.8]},
        {"tdsReadings": []},
        {"tdsReadings": [0]},
        {"tdsReadings": [9.5], "tdsPercent": 9.5},
    ):
        assert client.put(path, json=invalid, headers=barista_headers).status_code == 422


def test_without_readings_the_state_says_whether_the_machine_answers_on_the_network(
    client, barista_headers, monkeypatch
):
    # Sin lecturas no se distingue apagada de encendida sin enviar; el ping a
    # la dirección desde la que sincronizó la placa sí lo distingue.
    service = client.app.state.service
    stale = {"source": "measured", "state": "ready", "receivedAtUtc": "2026-10-25T23:44:00Z",
             "occurredAtUtc": "2026-10-25T23:44:00Z", "targets": {}}
    monkeypatch.setattr(service.repository, "latest_machine_telemetry", lambda: stale)
    asked: list[str] = []
    answers = {"192.168.1.100": False}
    service.ping = lambda address: asked.append(address) or answers.get(address)
    assert client.get("/api/v1/machine-state", headers=barista_headers).json()["reachable"] is None
    client.post("/api/v1/sync", json={"deviceId": "device-test", "messages": []},
                headers={**barista_headers, "X-Real-IP": "192.168.1.100"})
    assert client.get("/api/v1/machine-state", headers=barista_headers).json()["reachable"] is False
    # Cada pantalla pregunta cada 2 s: el ping no se repite en cada consulta.
    answers["192.168.1.100"] = True
    assert client.get("/api/v1/machine-state", headers=barista_headers).json()["reachable"] is False
    assert asked == ["192.168.1.100"]
    service._reachability = None
    assert client.get("/api/v1/machine-state", headers=barista_headers).json()["reachable"] is True
    # Sin ninguna lectura nunca, igual se prueba.
    monkeypatch.setattr(service.repository, "latest_machine_telemetry", lambda: None)
    assert client.get("/api/v1/machine-state", headers=barista_headers).json()["reachable"] is True


def test_with_fresh_readings_the_machine_is_not_pinged(client, barista_headers):
    from datetime import UTC, datetime

    service = client.app.state.service
    asked: list[str] = []
    service.ping = lambda address: asked.append(address) or True
    client.post("/api/v1/sync", json={"deviceId": "device-test", "messages": []},
                headers={**barista_headers, "X-Real-IP": "192.168.1.100"})
    client.app.state.repository.add_document("live-sample", "tlm-viva", {
        "id": "tlm-viva", "source": "measured", "state": "ready", "elapsedMs": 0,
        "occurredAtUtc": datetime.now(UTC).isoformat().replace("+00:00", "Z"),
        "temperature": {"value": 101.8, "unit": "Cel", "quality": "valid"},
        "targets": {"temperatureC": 92.0, "pressureBar": 8.5},
    })
    state = client.get("/api/v1/machine-state", headers=barista_headers).json()
    assert state["connected"] is True
    assert state["reachable"] is None
    assert asked == []


def test_above_the_measured_boiler_range_the_water_is_given_as_a_bound(
    client, barista_headers, monkeypatch
):
    from datetime import UTC, datetime

    # El agua de ahora se estima desde el metal: su borde es el de la recta del metal.
    from espresso_profiles.water_range import CURRENT_METAL

    service = client.app.state.service
    sample = {"source": "measured", "state": "ready",
              "receivedAtUtc": datetime.now(UTC).isoformat().replace("+00:00", "Z"),
              "temperature": {"value": CURRENT_METAL.boiler_maximum_c + 0.3}, "targets": {}}
    monkeypatch.setattr(service.repository, "latest_machine_telemetry", lambda: sample)
    state = client.get("/api/v1/machine-state", headers=barista_headers).json()
    assert state["estimatedWaterC"] is None
    assert state["estimatedWaterBound"] == {
        "side": "above", "limitC": CURRENT_METAL.water_maximum_c
    }


def test_with_the_first_use_raise_the_water_is_estimated_without_it(
    client, barista_headers, monkeypatch
):
    # La subida pone la caldera alta a propósito para que el primer café salga
    # como en uso continuo; el agua se estima con la caldera sin la subida.
    from datetime import UTC, datetime

    from espresso_profiles.water_range import boiler_reference_c
    from espresso_stability.machine import estimated_water_c

    service = client.app.state.service
    service.repository.add_document("first-use-decision", "first-use-prueba", {
        "id": "first-use-prueba", "deviceId": "device-test", "offsetC": 3.7,
        "decidedAt": "2026-10-26T15:00:00Z", "reason": "rest",
    })
    sample = {"source": "measured", "state": "ready", "deviceId": "device-test",
              "receivedAtUtc": datetime.now(UTC).isoformat().replace("+00:00", "Z"),
              "temperature": {"value": 105.5}, "targets": {"temperatureC": 92.0}}
    monkeypatch.setattr(service.repository, "latest_machine_telemetry", lambda: sample)
    state = client.get("/api/v1/machine-state", headers=barista_headers).json()
    assert state["firstUseOffsetC"] == 3.7
    assert state["estimatedWaterC"] == estimated_water_c(105.5 - 3.7)
    assert state["boilerReferenceC"] == pytest.approx(boiler_reference_c(92.0) + 3.7, abs=0.01)


def test_a_barista_lists_only_his_profiles_the_shared_one_and_the_ruling_one(client):
    # Pedido del 3-nov-2026: cada barista ve los perfiles que creó y el común;
    # el que rige se ve siempre porque la máquina es una.
    admin = {"X-Test-Role": "admin", "X-Test-Actor": "admin-test"}
    ana = {"X-Test-Role": "barista", "X-Test-Actor": "barista-ana"}
    luis = {"X-Test-Role": "barista", "X-Test-Actor": "barista-luis"}
    shared = client.post(
        "/api/v1/profiles", json={**profile_payload("Común"), "id": "default-profile"},
        headers=admin,
    ).json()
    house = client.post("/api/v1/profiles", json=profile_payload("De ensayo"), headers=admin).json()
    of_ana = client.post("/api/v1/profiles", json=profile_payload("De Ana"), headers=ana).json()
    of_luis = client.post("/api/v1/profiles", json=profile_payload("De Luis"), headers=luis).json()

    def seen(headers):
        items = client.get("/api/v1/profiles", headers=headers).json()["items"]
        return {item["id"] for item in items}

    assert seen(luis) == {shared["id"], of_luis["id"]}
    assert seen(ana) == {shared["id"], of_ana["id"]}
    assert {shared["id"], house["id"], of_ana["id"], of_luis["id"]} <= seen(admin)

    # Que rija se pone directo: lo que se prueba es la lista, no la activación.
    repository = client.app.state.repository
    repository.upsert_document("profile", of_ana["id"], {**of_ana, "status": "accepted"})
    assert of_ana["id"] in seen(luis)
    # Leerlo por su identificador sigue abierto: los cafés de otros lo citan.
    assert client.get(f"/api/v1/profiles/{house['id']}", headers=luis).status_code == 200


def test_a_water_pass_is_archived_for_the_barista_but_kept_for_the_admin(client, barista_headers):
    """Una pasada de agua marcada sale del historial del barista y de Preparar café,
    no la juzga el razonador, y la administración la sigue viendo (4-nov-2026)."""
    repository = client.app.state.repository
    other = repository.get_document("extraction", "ext-20260904-001")
    pass_ = {key: value for key, value in other.items() if key != "barista"}
    pass_["id"] = "ext-pasada-de-agua"
    repository.add_document("extraction", pass_["id"], pass_)
    client.put(f"/api/v1/extractions/{UNAUTHORED}/context", json={"notes": "mía"},
               headers=barista_headers)
    client.put(f"/api/v1/extractions/{pass_['id']}/context", json={"notes": "agua"},
               headers=barista_headers)

    def mine():
        listed = client.get("/api/v1/extractions", params={"page": 1},
                            headers=barista_headers).json()
        return {item["id"] for item in listed["items"]}

    assert pass_["id"] in mine()
    marked = client.put(f"/api/v1/extractions/{pass_['id']}/context",
                        json={"purpose": "water-pass"}, headers=barista_headers)
    assert marked.status_code == 200
    assert mine() == {UNAUTHORED}
    claimable = client.get("/api/v1/extractions/claimable", headers=barista_headers).json()
    assert pass_["id"] not in [item["id"] for item in claimable["items"]]

    admin = {"X-Test-Role": "admin", "X-Test-Actor": "admin-test"}
    everything = client.get("/api/v1/extractions", params={"page": 1, "page_size": 100},
                            headers=admin).json()
    archived = [item for item in everything["items"] if item["id"] == pass_["id"]]
    assert archived and archived[0]["purpose"] == "water-pass"

    recommendation = client.post("/api/v1/recommendations",
                                 json={"extractionId": pass_["id"]}, headers=barista_headers)
    assert recommendation.status_code == 201
    assert recommendation.json()["status"] == "abstained"


def test_comparison_summarises_how_much_the_series_varies(client):
    """La comparación resume la serie: masa, tiempo y agua, sin rellenar lo que falta."""
    response = client.post(
        "/api/v1/extractions/compare",
        json={"ids": ["ext-20260904-001", "ext-20260904-002"], "dimensions": ["preparation"]},
    )
    assert response.status_code == 200
    repeatability = response.json()["repeatability"]
    metrics = {metric["key"]: metric for metric in repeatability["metrics"]}
    assert set(metrics) == {"beverageMassG", "durationS", "estimatedWaterC"}
    for metric in metrics.values():
        assert metric["count"] + metric["missing"] == 2
        if metric["count"] < 2:
            assert metric["range"] is None
    # Con dos preparaciones no hay desviación estándar: con tan pocas no dice nada.
    assert all(metric["standardDeviation"] is None for metric in metrics.values())
    assert metrics["estimatedWaterC"]["bandC"] == 1.0
    assert isinstance(repeatability["sharedProfile"], bool)
