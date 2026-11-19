"""La estabilidad térmica de la máquina se juzga en toda la espera en «lista».

Comprueban que la espera sale de la telemetría desde la primera muestra en
«ready» tras la última con otro estado, que con la subida del primer uso cuenta
desde que se pidió, y que el agua estimada se compara con la preparación medida
anterior.
"""

from datetime import UTC, datetime, timedelta

import pytest

DEVICE = "device-estabilidad"
PRESS = datetime(2026, 10, 22, 12, 0, tzinfo=UTC)


def _text(instant: datetime) -> str:
    return instant.isoformat().replace("+00:00", "Z")


def _telemetry(repository, index: int, at: datetime, state: str, body: float) -> None:
    repository.add_document(
        "live-sample",
        f"tlm-estabilidad-{index:05d}",
        {"id": f"tlm-estabilidad-{index:05d}", "deviceId": DEVICE, "state": state,
         "occurredAtUtc": _text(at).replace("Z", ".000000Z"), "temperatureC": body,
         "quality": "valid", "source": "measured", "elapsedMs": 0},
    )


def _extraction(repository, identifier: str, press: datetime, offset: float = 0.0) -> dict:
    document = {
        "id": identifier, "deviceId": DEVICE, "source": "measured", "status": "closed",
        "startedAt": _text(press), "boilerOffsetAppliedC": offset,
        "profile": {"temperatureSetpointC": 92.0, "pressureSetpointBar": 8.5},
    }
    repository.add_document("extraction", identifier, document)
    return document


def test_la_espera_entera_cuenta_desde_que_la_maquina_quedo_lista(client):
    repository = client.app.state.repository
    service = client.app.state.service
    start = PRESS - timedelta(minutes=10)
    _telemetry(repository, 0, start - timedelta(seconds=15), "coffee-heating", 95.0)
    for i in range(40):
        # Los primeros 3 min, 2,5 °C por encima de la referencia de 101,9 °C.
        _telemetry(repository, 1 + i, start + timedelta(seconds=15 * i), "ready",
                   104.4 if i < 12 else 102.0)
    thermal = service._machine_stability(_extraction(repository, "prep-espera", PRESS))["thermal"]
    assert thermal["status"] == "measured"
    assert thermal["waitS"] == 600.0
    assert thermal["fraction"] == 0.7
    assert thermal["coverage"] == 1.0


def test_con_la_subida_del_primer_uso_cuenta_desde_que_se_pidio(client):
    repository = client.app.state.repository
    service = client.app.state.service
    start = PRESS - timedelta(minutes=30)
    for i in range(120):
        _telemetry(repository, 100 + i, start + timedelta(seconds=15 * i), "ready", 101.9)
    repository.add_document(
        "first-use-decision", "first-use-prueba",
        {"id": "first-use-prueba", "deviceId": DEVICE, "offsetC": 3.7,
         "decidedAt": _text(PRESS - timedelta(minutes=5))},
    )
    thermal = service._machine_stability(
        _extraction(repository, "prep-subida", PRESS, offset=3.7)
    )["thermal"]
    assert thermal["referenceC"] == pytest.approx((92.0 - 9.84) / 0.8063 + 3.7)
    assert thermal["waitS"] == 300.0


def test_un_cafe_de_la_v11_se_juzga_con_su_recta(client):
    """El café declara la recta con que se hizo; uno sin ella es de la v6."""
    repository = client.app.state.repository
    service = client.app.state.service
    start = PRESS - timedelta(minutes=10)
    for i in range(40):
        _telemetry(repository, 300 + i, start + timedelta(seconds=15 * i), "ready", 104.9)
    document = _extraction(repository, "prep-v11", PRESS)
    document["waterReference"] = {
        "plant": "acta-temperatura-20261028-v11", "gain": 0.8819, "offsetC": -0.49,
        "boilerMinimumC": 97.3, "boilerMaximumC": 105.6,
    }
    repository.upsert_document("extraction", "prep-v11", document)
    thermal = service._machine_stability(document)["thermal"]
    assert thermal["referenceC"] == pytest.approx((92.0 + 0.49) / 0.8819)
    document["waterReference"] = None
    assert service._machine_stability(document)["thermal"]["referenceC"] is None


def test_el_agua_se_compara_con_la_preparacion_anterior(client):
    repository = client.app.state.repository
    service = client.app.state.service
    for i in range(10):
        _telemetry(repository, 300 + i, PRESS - timedelta(minutes=40) + timedelta(seconds=15 * i),
                   "ready", 101.3)
    _extraction(repository, "prep-anterior", PRESS - timedelta(minutes=37, seconds=30))
    _telemetry(repository, 400, PRESS - timedelta(minutes=37), "extraction", 95.0)
    for i in range(10):
        _telemetry(repository, 401 + i, PRESS - timedelta(seconds=150) + timedelta(seconds=15 * i),
                   "ready", 101.9)
    water = service._machine_stability(_extraction(repository, "prep-actual", PRESS))["water"]
    assert water["status"] == "estimated"
    assert water["previousExtractionId"] == "prep-anterior"
    assert water["differenceC"] == pytest.approx(0.48, abs=0.01)
    assert water["within"] is True


def test_el_gemelo_estima_el_agua_con_el_reposo_y_lo_declara(client):
    repository = client.app.state.repository
    service = client.app.state.service
    # Dos horas en «lista» con el cuerpo en la referencia y un café al final.
    start = PRESS - timedelta(hours=2)
    for i in range(480):
        _telemetry(repository, 600 + i, start + timedelta(seconds=15 * i), "ready", 101.9)
    document = _extraction(repository, "prep-gemelo", PRESS)
    document.update(durationMs=20000, environment={"temperatureC": 23.0})
    repository.upsert_document("extraction", "prep-gemelo", document)
    twin = service._machine_stability(document)["water"]["twin"]
    assert twin["status"] == "estimated"
    assert twin["useIndex"] == 1
    assert twin["estimatedC"] is not None and twin["estimatedC"] < 94.2
    assert {"ambientC": 23.0} in twin["assumptions"]


def test_sin_temperatura_del_cuarto_el_gemelo_supone_la_de_la_franja(client):
    """Sin cuarto anotado, el promedio medido de la franja horaria, declarado."""
    repository = client.app.state.repository
    service = client.app.state.service
    for i in range(40):
        _telemetry(repository, 1200 + i, PRESS - timedelta(seconds=600 - 15 * i), "ready", 101.9)
    document = _extraction(repository, "prep-sin-cuarto", PRESS)
    document.update(durationMs=20000)
    repository.upsert_document("extraction", "prep-sin-cuarto", document)
    twin = service._machine_stability(document)["water"]["twin"]
    assert twin["status"] == "estimated"
    from aplicacion_web.application.room_temperature import assumed_room_c

    value, slot = assumed_room_c(PRESS)
    assert {"ambientC": value, "ambientAssumed": slot} in twin["assumptions"]


def test_el_cuarto_supuesto_sigue_la_hora_del_laboratorio():
    from datetime import UTC, datetime

    from aplicacion_web.application.room_temperature import assumed_room_c

    # 08:00, 15:00 y 21:00 en Colombia (UTC−5).
    assert assumed_room_c(datetime(2026, 10, 26, 13, tzinfo=UTC)) == (23.2, "mañana")
    assert assumed_room_c(datetime(2026, 10, 26, 20, tzinfo=UTC)) == (25.4, "tarde")
    assert assumed_room_c(datetime(2026, 10, 27, 2, tzinfo=UTC)) == (24.8, "noche")


def test_la_estabilidad_se_guarda_y_se_rehace_si_cambian_sus_datos(client, monkeypatch):
    """La segunda visita no relee la telemetría; anotar el cuarto sí la rehace."""
    repository = client.app.state.repository
    service = client.app.state.service
    start = PRESS - timedelta(hours=1)
    for i in range(120):
        _telemetry(repository, 3000 + i, start + timedelta(seconds=30 * i), "ready", 101.9)
    document = _extraction(repository, "prep-guardada", PRESS)
    document.update(durationMs=20000)
    repository.upsert_document("extraction", "prep-guardada", document)
    calls = []
    original = service._machine_stability
    monkeypatch.setattr(service, "_machine_stability",
                        lambda extraction: calls.append(1) or original(extraction))
    first = service._cached_machine_stability(document)
    second = service._cached_machine_stability(document)
    assert len(calls) == 1 and first == second
    document["environment"] = {"temperatureC": 22.0}
    repository.upsert_document("extraction", "prep-guardada", document)
    third = service._cached_machine_stability(document)
    assert len(calls) == 2
    assert {"ambientC": 22.0} in third["water"]["twin"]["assumptions"]


def test_la_caldera_se_juzga_al_pulsar_y_no_durante_el_cafe():
    """Cae siempre mientras sale el agua: lo que decide el agua es el metal al pulsar."""
    from aplicacion_web.application.observations import boiler_at_press, preparation_observations

    samples = [
        {"elapsedMs": -1000, "temperatureC": 101.6, "pressureBar": 0.0},
        {"elapsedMs": -500, "temperatureC": 101.8, "pressureBar": 0.0},
        *({"elapsedMs": 500 * i, "temperatureC": 101.8 - 0.5 * i, "pressureBar": 8.5}
          for i in range(1, 30)),
    ]
    assert boiler_at_press(samples) == 101.8
    assert boiler_at_press(samples[2:]) == samples[2]["temperatureC"]
    extraction = {"id": "x", "source": "measured", "finishedAt": "2026-10-26T03:20:00Z",
                  "manualMeasurements": {}, "durationMs": 15000, "sampleCount": 31}
    window = {"status": "stabilized", "startMs": 3000}
    observations = preparation_observations(extraction, samples, window)
    boiler = [item for item in observations if item["property"] == "boiler-temperature"]
    assert [item["value"] for item in boiler] == [101.8]
    assert boiler[0]["method"] == "urn:method:series-at-press"


def test_the_boiler_at_press_reads_back_as_the_requested_water():
    # Mismo veredicto en otras unidades: el objetivo vuelve a ser el agua que
    # se pidió, también con la subida del primer uso, y la caldera al pulsar
    # se lee sin la subida.
    from espresso_profiles.water_range import (
        CURRENT,
        CURRENT_METAL,
        boiler_reference_c,
        judged_specifications,
    )
    from espresso_stability.machine import estimated_water_c

    from aplicacion_web.application.services import _boiler_as_water

    water_spec = {"property": "water-temperature", "unit": "°C", "strategy": "target",
                  "target": 92.0, "tolerance": 1.0}
    boiler = next(item for item in judged_specifications([water_spec], 3.7)
                  if item["property"] == "boiler-temperature")
    view = _boiler_as_water({"target": boiler["target"], "tolerance": boiler["tolerance"]},
                            105.72, {"amount": None}, 3.7, CURRENT, CURRENT_METAL)
    assert view["objective"]["target"] == pytest.approx(92.0, abs=0.02)
    assert view["objective"]["tolerance"] == pytest.approx(1.0, abs=0.02)
    assert view["estimatedC"] == estimated_water_c(105.72 - 3.7)
    assert boiler["target"] == pytest.approx(boiler_reference_c(92.0) + 3.7, abs=0.01)
