"""La subida del primer uso viaja con la configuración que rige (ADR-018, enmienda del 2026-10-22).

Comprueban propiedades sobre la sincronización: que el reposo sale de la
telemetría de la placa y no del reloj del servidor, que un hueco en lo que la
plataforma vio no pide nada, que un uso en curso no cambia lo pedido, que la
subida que declara el perfil a mano manda y que solo se guarda cada cambio.
"""

from datetime import UTC, datetime, timedelta

DEVICE = "device-primer-uso"
START = datetime(2026, 10, 22, 12, 0, tzinfo=UTC)


def _sample(sequence: int, at: datetime, state: str) -> dict:
    return {
        "schemaVersion": "1.0",
        "messageId": f"tlm-primer-uso-{sequence:05d}",
        "messageType": "telemetry.sample",
        "deviceId": DEVICE,
        "sequence": sequence,
        "occurredAt": at.isoformat().replace("+00:00", "Z"),
        "payload": {
            "source": "measured",
            "state": state,
            "elapsedMs": 0,
            "temperature": {"value": 101.9, "unit": "Cel", "quality": "valid"},
            "pressure": {"value": 0.0, "unit": "bar", "quality": "valid"},
            "targets": {"temperatureC": 92.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": False, "pumpDutyPercent": 0, "virtual": False},
        },
    }


def _sync(client, headers, messages):
    response = client.post(
        "/api/v1/sync", json={"deviceId": DEVICE, "messages": messages}, headers=headers
    )
    assert response.status_code == 200
    return response.json()["profileProposals"]


def _accepted_profile(client, offset=None):
    profile = {
        "id": "perfil-primer-uso",
        "name": "Perfil del primer uso",
        "version": 1,
        "status": "accepted",
        "temperatureSetpointC": 92.0,
        "temperatureToleranceC": 0.5,
        "pressureSetpointBar": 9.0,
        "pressureToleranceBar": 0.5,
        "maximumTimeMs": 40000,
    }
    if offset is not None:
        profile["boilerOffsetC"] = offset
    client.app.state.repository.upsert_document("profile", profile["id"], profile)


def _rest(client, headers, minutes: float, *, every_s: float = 15.0, first: int = 0):
    """Un uso al principio y la máquina «lista» durante `minutes`, muestra a muestra."""
    messages = [_sample(first, START, "extraction")]
    steps = int(minutes * 60 / every_s)
    for step in range(1, steps + 1):
        messages.append(_sample(first + step, START + timedelta(seconds=step * every_s), "ready"))
    proposals = []
    for index in range(0, len(messages), 64):
        proposals = _sync(client, headers, messages[index:index + 64])
    return proposals


def _offset(proposals):
    (proposal,) = [item for item in proposals if item["id"] == "perfil-primer-uso"]
    return proposal.get("boilerOffsetC")


def test_tras_20_min_de_reposo_pide_la_subida_medida(client, barista_headers):
    _accepted_profile(client)
    assert _offset(_rest(client, barista_headers, 20)) == 2.7


def test_un_reposo_corto_no_pide_nada(client, barista_headers):
    _accepted_profile(client)
    assert _offset(_rest(client, barista_headers, 8)) is None


def test_un_hueco_en_la_telemetria_no_pide_nada(client, barista_headers):
    _accepted_profile(client)
    # Una muestra cada 2 min: la plataforma no vio la espera entera.
    assert _offset(_rest(client, barista_headers, 30, every_s=120)) is None


def test_durante_el_cafe_mantiene_lo_pedido(client, barista_headers):
    _accepted_profile(client)
    assert _offset(_rest(client, barista_headers, 30)) == 3.7
    pressed = START + timedelta(minutes=30, seconds=5)
    assert _offset(_sync(client, barista_headers, [_sample(500, pressed, "extraction")])) == 3.7
    # Al volver a «lista» tras el café el reposo es cero: ya no pide nada.
    after = pressed + timedelta(seconds=30)
    assert _offset(_sync(client, barista_headers, [_sample(501, after, "ready")])) is None


def test_la_subida_del_perfil_manda(client, barista_headers):
    _accepted_profile(client, offset=1.0)
    assert _offset(_rest(client, barista_headers, 30)) == 1.0


def test_solo_se_guarda_cada_cambio(client, barista_headers):
    _accepted_profile(client)
    _rest(client, barista_headers, 35)
    decisions = client.app.state.repository.list_documents(
        "first-use-decision", page_size=100, filters={"deviceId": DEVICE}
    ).items
    # Uso, reposo corto y los tramos de 15, 20, 25 y 30 min; ninguno repetido.
    offsets = [item["offsetC"] for item in sorted(decisions, key=lambda item: item["decidedAt"])]
    assert offsets == sorted(offsets, key=lambda value: -1 if value is None else value)
    assert len(offsets) == len(set(offsets))
    assert decisions[0]["ruleVersion"] == "primer-uso-v1"


def test_el_resumen_del_cafe_termina_el_reposo_aunque_no_llegue_telemetria(
    client, barista_headers
):
    # El firmware de operación no envía telemetría durante el café: el fin del
    # uso lo da el resumen de la preparación.
    _accepted_profile(client)
    assert _offset(_rest(client, barista_headers, 30)) == 3.7
    finished = START + timedelta(minutes=30, seconds=40)
    client.app.state.repository.add_document(
        "extraction",
        "prep-primer-uso",
        {"id": "prep-primer-uso", "source": "measured", "status": "closed",
         "finishedAt": finished.isoformat().replace("+00:00", "Z")},
    )
    after = finished + timedelta(seconds=15)
    assert _offset(_sync(client, barista_headers, [_sample(600, after, "ready")])) is None


def test_una_preparacion_simulada_no_termina_el_reposo(client, barista_headers):
    _accepted_profile(client)
    _rest(client, barista_headers, 30)
    finished = START + timedelta(minutes=30, seconds=40)
    client.app.state.repository.add_document(
        "extraction",
        "prep-simulada",
        {"id": "prep-simulada", "source": "simulation", "status": "closed",
         "finishedAt": finished.isoformat().replace("+00:00", "Z")},
    )
    after = finished + timedelta(seconds=15)
    assert _offset(_sync(client, barista_headers, [_sample(600, after, "ready")])) == 3.7


ADMIN = {"X-Test-Role": "admin", "X-Test-Actor": "admin-test"}


def test_la_exclusion_del_barista_deja_la_subida_en_cero_y_se_retira(client, barista_headers):
    _accepted_profile(client)
    assert _offset(_rest(client, barista_headers, 30)) == 3.7
    excluded = client.post(
        "/api/v1/profiles/perfil-primer-uso/sustainment-exclusion",
        json={"reason": "quiero ver la máquina sin ayuda"},
        headers=barista_headers,
    )
    assert excluded.status_code == 200
    later = START + timedelta(minutes=30, seconds=15)
    assert _offset(_sync(client, barista_headers, [_sample(700, later, "ready")])) is None
    # Retirada, queda registrada y la subida vuelve.
    withdrawn = client.delete(
        "/api/v1/profiles/perfil-primer-uso/sustainment-exclusion", headers=barista_headers
    )
    assert withdrawn.json()["status"] == "withdrawn"
    after = later + timedelta(seconds=15)
    assert _offset(_sync(client, barista_headers, [_sample(701, after, "ready")])) == 3.7


def test_la_suspension_es_del_protocolo_y_deja_la_subida_en_cero(client, barista_headers):
    _accepted_profile(client)
    assert _offset(_rest(client, barista_headers, 30)) == 3.7
    denied = client.post(
        "/api/v1/sustainment/suspension", json={"reason": "serie E3"}, headers=barista_headers
    )
    assert denied.status_code == 403
    suspended = client.post(
        "/api/v1/sustainment/suspension", json={"reason": "serie E3"}, headers=ADMIN
    )
    assert suspended.status_code == 201
    later = START + timedelta(minutes=30, seconds=15)
    assert _offset(_sync(client, barista_headers, [_sample(800, later, "ready")])) is None
    lifted = client.delete("/api/v1/sustainment/suspension", headers=ADMIN)
    assert lifted.json()["status"] == "lifted"
    after = later + timedelta(seconds=15)
    assert _offset(_sync(client, barista_headers, [_sample(801, after, "ready")])) == 3.7


def test_la_subida_no_cambia_el_tiempo_maximo_ni_crea_versiones(client, barista_headers):
    _accepted_profile(client)
    repository = client.app.state.repository
    before = {item["id"] for item in repository.list_documents("profile", page_size=100).items}
    (proposal,) = [
        item for item in _rest(client, barista_headers, 30) if item["id"] == "perfil-primer-uso"
    ]
    assert proposal["boilerOffsetC"] == 3.7
    assert proposal["maximumTimeMs"] == 40000
    after = {item["id"] for item in repository.list_documents("profile", page_size=100).items}
    assert after == before


def test_nadie_activa_por_una_via_que_no_sea_de_una_persona(client, barista_headers):
    # La cuarta vía de ADR-018 § 4.5 se retiró con la corrección después del café.
    _accepted_profile(client)
    response = client.post(
        "/api/v1/profiles/perfil-primer-uso/activate",
        json={"via": "sustainment"},
        headers=barista_headers,
    )
    assert response.status_code == 422


# --- Desde el 27-oct decide el modelo con la pared (primer-uso-pared-v1) ----------


def _coffees(client, count=3, every_min=5.0):
    """Tres cafés medidos seguidos desde START, con su resumen."""
    for k in range(count):
        started = START + timedelta(minutes=every_min * k)
        finished = started + timedelta(seconds=20)
        identifier = f"prep-pared-{k}"
        client.app.state.repository.add_document("extraction", identifier, {
            "id": identifier, "source": "measured", "status": "closed", "deviceId": DEVICE,
            "startedAt": started.isoformat().replace("+00:00", "Z"),
            "finishedAt": finished.isoformat().replace("+00:00", "Z"), "durationMs": 20000,
            "manualMeasurements": {"beverageMassG": 60.0},
        })


def _ready_until(client, headers, minutes_after_last, *, first=1000, every_s=15.0):
    """«Lista» desde 30 min antes del primer café hasta `minutes_after_last` tras el último."""
    begin = START - timedelta(minutes=30)
    end = START + timedelta(minutes=10, seconds=20) + timedelta(minutes=minutes_after_last)
    messages, k, at = [], 0, begin
    while at <= end:
        messages.append(_sample(first + k, at, "ready"))
        at += timedelta(seconds=every_s)
        k += 1
    proposals = []
    for index in range(0, len(messages), 64):
        proposals = _sync(client, headers, messages[index:index + 64])
    return proposals


def _last_decision(client):
    items = client.app.state.repository.list_documents(
        "first-use-decision", page_size=1, filters={"deviceId": DEVICE}
    ).items
    return items[0]


def test_con_tres_cafes_decide_el_modelo_y_guarda_la_pared(client, barista_headers, monkeypatch):
    from espresso_sustainment.first_use import WALL_FIRST_USE_V1

    from aplicacion_web.application import sustainment

    monkeypatch.setattr(sustainment, "FIRST_USE_MODEL", WALL_FIRST_USE_V1)
    sustainment._wall_cache.clear()
    _accepted_profile(client)
    _coffees(client)
    proposals = _ready_until(client, barista_headers, 40)
    decision = _last_decision(client)
    assert decision["reason"] == "model-wall"
    assert decision["ruleVersion"] == "primer-uso-pared-v1"
    assert decision["wall"]["relativeWallC"] < 0
    assert decision["wall"]["referenceUses"] == ["prep-pared-0", "prep-pared-1", "prep-pared-2"]
    assert 0 < _offset(proposals) <= 4.0


def test_justo_despues_de_los_cafes_el_modelo_no_pide_nada(client, barista_headers):
    from aplicacion_web.application import sustainment

    sustainment._wall_cache.clear()
    _accepted_profile(client)
    _coffees(client)
    proposals = _ready_until(client, barista_headers, 2)
    assert _last_decision(client)["reason"] == "model-no-deficit"
    assert _offset(proposals) is None
    assert "firstUseFeedforwardGain" not in proposals[0]


def _reinforcing_model(threshold_c):
    from dataclasses import replace

    from espresso_sustainment.first_use import WALL_FIRST_USE_V2

    return replace(WALL_FIRST_USE_V2, feedforward_min_deficit_c=threshold_c)


def test_con_deficit_el_modelo_v2_pide_el_refuerzo_y_no_la_subida(
    client, barista_headers, monkeypatch
):
    """ADR-009, enmienda del 2026-10-27: lo medido es el refuerzo sin subida.
    El umbral se baja aquí porque la pared de estas pruebas no llega a 2 °C."""
    from aplicacion_web.application import sustainment

    monkeypatch.setattr(sustainment, "FIRST_USE_MODEL", _reinforcing_model(0.5))
    sustainment._wall_cache.clear()
    _accepted_profile(client)
    _coffees(client)
    proposals = _ready_until(client, barista_headers, 40)
    decision = _last_decision(client)
    assert decision["reason"] == "model-feedforward"
    assert decision["ruleVersion"] == "primer-uso-pared-v2"
    assert decision["feedforwardGain"] == 1.25
    assert decision["offsetC"] is None
    assert proposals[0]["firstUseFeedforwardGain"] == 1.25
    assert _offset(proposals) is None


def test_bajo_el_umbral_el_modelo_v2_no_pide_nada(client, barista_headers, monkeypatch):
    from aplicacion_web.application import sustainment

    monkeypatch.setattr(sustainment, "FIRST_USE_MODEL", _reinforcing_model(50.0))
    sustainment._wall_cache.clear()
    _accepted_profile(client)
    _coffees(client)
    proposals = _ready_until(client, barista_headers, 40)
    assert _last_decision(client)["reason"] == "model-no-deficit"
    assert "firstUseFeedforwardGain" not in proposals[0]
    assert _offset(proposals) is None


def test_con_un_hueco_vuelve_la_regla(client, barista_headers):
    from aplicacion_web.application import sustainment

    sustainment._wall_cache.clear()
    _accepted_profile(client)
    _coffees(client)
    _ready_until(client, barista_headers, 40, every_s=120.0)
    assert _last_decision(client)["ruleVersion"] == "primer-uso-v1"


def test_tras_un_arranque_en_frio_decide_el_modelo_con_la_pared_de_referencia(
    client, barista_headers
):
    from aplicacion_web.application import sustainment

    sustainment._wall_cache.clear()
    _accepted_profile(client)
    # Encendida a 23 °C y calentando hasta 101,9 °C en 15 min; «lista» 20 min más.
    messages = []
    for k in range(0, 35 * 4):
        at = START + timedelta(seconds=15 * k)
        sample = _sample(2000 + k, at, "heating" if k < 60 else "ready")
        sample["payload"]["temperature"]["value"] = min(101.9, 23.0 + 78.9 * k / 60)
        messages.append(sample)
    proposals = []
    for index in range(0, len(messages), 64):
        proposals = _sync(client, barista_headers, messages[index:index + 64])
    decision = _last_decision(client)
    # Tras un arranque en frío sigue la subida: el refuerzo no se midió ahí.
    assert decision["ruleVersion"] == "primer-uso-pared-v5"
    assert decision["wall"]["coldStart"] is True
    assert decision["feedforwardGain"] is None
    assert _offset(proposals) == 4.0
    assert "firstUseFeedforwardGain" not in proposals[0]
