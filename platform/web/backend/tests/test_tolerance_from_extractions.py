"""La tolerancia de la receta sale de los cafés repetidos con ella.

Comprueban propiedades sobre la plataforma: que sin pedirlo no se escribe nada,
que al pedirlo la versión nueva rige y declara de qué cafés salió, que sus
cafés quedan dentro de la banda y que una simulación o un café de otro perfil
no cuentan.
"""

PROFILE = "perfil-receta"
COFFEE = "cafe-receta"
SPECIFICATIONS = [
    {"property": "water-temperature", "unit": "°C", "strategy": "target",
     "target": 92.0, "tolerance": 1.0},
    {"property": "pressure", "unit": "bar", "strategy": "target", "target": 9.0,
     "tolerance": 0.5},
    {"property": "extraction-time", "unit": "s", "strategy": "range", "minimum": 18.0,
     "maximum": 30.0},
    {"property": "coffee-dose", "unit": "g", "strategy": "target", "target": 18.0,
     "tolerance": 0.5},
    {"property": "beverage-mass", "unit": "g", "strategy": "target", "target": 36.0,
     "tolerance": 2.0},
]
CUPS = [(18.0, 35.2, 25100, 9.12), (18.1, 36.5, 26020, 9.4), (17.9, 37.1, 25800, 9.25)]


def _seed(client, *, source="measured", profile_id=PROFILE):
    repository = client.app.state.repository
    repository.upsert_document("coffee", COFFEE, {"id": COFFEE, "commercialReference": "Receta"})
    repository.upsert_document("profile", PROFILE, {
        "id": PROFILE, "familyId": PROFILE, "name": "Receta", "version": 1,
        "status": "accepted", "coffeeIds": [COFFEE], "specifications": SPECIFICATIONS,
    })
    identifiers = []
    for index, (dose, mass, duration, tds) in enumerate(CUPS):
        identifier = f"cafe-{source}-{index}"
        repository.upsert_document("extraction", identifier, {
            "id": identifier, "source": source, "profile": {"id": profile_id, "version": 1},
            "durationMs": duration, "finishedAt": f"2026-10-23T12:0{index}:00Z",
            "manualMeasurements": {"coffeeDoseG": dose, "beverageMassG": mass,
                                   "tdsPercent": tds},
        })
        identifiers.append(identifier)
    return identifiers


def _post(client, headers, identifiers, apply=False):
    return client.post(
        f"/api/v1/profiles/{PROFILE}/tolerance-from-extractions",
        json={"extractionIds": identifiers, "apply": apply}, headers=headers,
    )


def _spec(specifications, prop):
    (item,) = [s for s in specifications if s["property"] == prop]
    return item


def test_sin_aplicar_solo_propone(client, barista_headers):
    identifiers = _seed(client)
    response = _post(client, barista_headers, identifiers)
    assert response.status_code == 200, response.text
    body = response.json()
    assert _spec(body["specifications"], "beverage-mass")["tolerance"] == 1.1
    profiles = client.app.state.repository.list_documents("profile", page_size=50).items
    assert not [p for p in profiles if p.get("familyId") == PROFILE and p["id"] != PROFILE]


def test_al_aplicar_la_version_nueva_rige_y_dice_de_donde_sale(client, barista_headers):
    identifiers = _seed(client)
    response = _post(client, barista_headers, identifiers, apply=True)
    assert response.status_code == 200, response.text
    created = response.json()["profile"]
    assert created["status"] == "accepted"
    assert created["previousVersionId"] == PROFILE
    assert created["toleranceDerivation"]["extractionIds"] == identifiers
    previous = client.app.state.repository.get_document("profile", PROFILE)
    assert previous["status"] == "approved"
    for prop, column in (("coffee-dose", 0), ("beverage-mass", 1), ("tds", 3)):
        spec = _spec(created["specifications"], prop)
        assert all(abs(cup[column] - spec["target"]) <= spec["tolerance"] + 1e-9 for cup in CUPS)
    assert _spec(created["specifications"], "extraction-time") == SPECIFICATIONS[2]


def test_una_simulacion_no_cuenta(client, barista_headers):
    identifiers = _seed(client, source="simulation")
    assert _post(client, barista_headers, identifiers).status_code == 400


def test_un_cafe_de_otro_perfil_no_cuenta(client, barista_headers):
    identifiers = _seed(client, profile_id="otro-perfil")
    assert _post(client, barista_headers, identifiers).status_code == 400


def test_sin_valores_no_crea_una_version_vacia(client, barista_headers):
    identifiers = _seed(client)
    repository = client.app.state.repository
    for identifier in identifiers:
        cup = repository.get_document("extraction", identifier)
        repository.upsert_document("extraction", identifier, {**cup, "manualMeasurements": {}})
    assert _post(client, barista_headers, identifiers, apply=True).status_code == 400
