"""Un café registrado se puede corregir sin borrar lo que decía antes.

Comprueban propiedades: que la corrección queda, que lo anterior se conserva
en `corrections`, que la descripción declarada no se pierde y que sigue
haciendo falta una referencia.
"""

BASE = {"commercialReference": "Kajugui", "variety": "Castillo",
        "postHarvestProcess": "Lavado", "notApplicableFields": ["roastDate"]}


def _create(client, headers):
    response = client.post("/api/v1/coffees", json=BASE, headers=headers)
    assert response.status_code == 201, response.text
    return response.json()["id"]


def test_la_correccion_queda_y_conserva_lo_anterior(client, barista_headers):
    coffee_id = _create(client, barista_headers)
    changed = {**BASE, "notApplicableFields": [], "altitudeM": 1800}
    response = client.put(f"/api/v1/coffees/{coffee_id}", json=changed, headers=barista_headers)
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["notApplicableFields"] == [] and body["altitudeM"] == 1800
    (correction,) = body["corrections"]
    assert correction["previous"]["notApplicableFields"] == ["roastDate"]
    assert correction["previous"]["altitudeM"] is None


def test_sin_cambios_no_anota_correccion(client, barista_headers):
    coffee_id = _create(client, barista_headers)
    body = client.put(f"/api/v1/coffees/{coffee_id}", json=BASE, headers=barista_headers).json()
    assert not body.get("corrections")


def test_la_descripcion_declarada_no_se_pierde(client, barista_headers):
    coffee_id = _create(client, barista_headers)
    declared = {"originalText": "Chocolate y naranja", "declaredBy": "Tostador",
                "source": "Bolsa", "descriptors": ["Chocolate"]}
    put = client.put(f"/api/v1/coffees/{coffee_id}/declared-sensory-profile",
                     json=declared, headers=barista_headers)
    assert put.status_code == 200, put.text
    body = client.put(f"/api/v1/coffees/{coffee_id}", json={**BASE, "region": "Bolívar"},
                      headers=barista_headers).json()
    assert body["declaredSensoryProfile"]["originalText"] == "Chocolate y naranja"


def test_sigue_haciendo_falta_una_referencia(client, barista_headers):
    coffee_id = _create(client, barista_headers)
    response = client.put(f"/api/v1/coffees/{coffee_id}",
                          json={**BASE, "commercialReference": ""}, headers=barista_headers)
    assert response.status_code in {400, 422}
