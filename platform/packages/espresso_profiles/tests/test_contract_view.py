"""La traducción debe leer las tres formas y no inventar lo que falte."""

from espresso_profiles import profile_contract_view, profile_reference

CATALOGO = {
    "id": "profile-lab",
    "name": "Perfil de laboratorio",
    "version": 2,
    "status": "approved",
    "provenance": {"actor": "barista-lab-01"},
    "specifications": [
        {"property": "water-temperature", "strategy": "target", "target": 93.0,
         "tolerance": 0.5, "unit": "°C"},
        {"property": "pressure", "strategy": "target", "target": 9.0,
         "tolerance": 0.4, "unit": "bar"},
        {"property": "extraction-time", "strategy": "range", "maximum": 40.0, "unit": "s"},
    ],
}

INSTANTANEA = {
    "id": "profile-lab",
    "version": 2,
    "status": "effective",
    "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
    "tolerances": {"temperatureC": 0.5, "pressureBar": 0.4},
}

CONTRATO = {
    "id": "profile-lab",
    "name": "Perfil de laboratorio",
    "version": 2,
    "status": "accepted",
    "temperatureSetpointC": 93.0,
    "temperatureToleranceC": 0.5,
    "pressureSetpointBar": 9.0,
    "pressureToleranceBar": 0.4,
    "maximumTimeMs": 40000,
}


def test_las_tres_formas_dan_las_mismas_referencias():
    for configuracion in (CATALOGO, INSTANTANEA, CONTRATO):
        vista = profile_contract_view(configuracion)
        assert vista["temperatureSetpointC"] == 93.0
        assert vista["temperatureToleranceC"] == 0.5
        assert vista["pressureSetpointBar"] == 9.0
        assert vista["pressureToleranceBar"] == 0.4


def test_el_tiempo_del_catalogo_se_expresa_en_milisegundos():
    assert profile_contract_view(CATALOGO)["maximumTimeMs"] == 40000


def test_un_intervalo_declara_su_centro_y_su_media_anchura():
    intervalo = {
        "id": "p",
        "specifications": [
            {"property": "water-temperature", "strategy": "range",
             "minimum": 92.0, "maximum": 94.0}
        ],
    }
    assert profile_reference(intervalo, "temperature") == (93.0, 1.0)


def test_una_magnitud_no_declarada_queda_ausente():
    """Una referencia que nadie declaró no se sustituye: gobernaría la máquina."""
    sin_presion = {k: v for k, v in CONTRATO.items()
                   if k not in {"pressureSetpointBar", "pressureToleranceBar"}}
    vista = profile_contract_view(sin_presion)
    assert "pressureSetpointBar" not in vista
    assert profile_reference(sin_presion, "pressure") == (None, None)


def test_el_autor_se_toma_de_la_procedencia_cuando_no_esta_declarado():
    assert profile_contract_view(CATALOGO)["definedBy"] == "barista-lab-01"


def test_el_purgado_declarado_baja_como_entero():
    vista = profile_contract_view({**CATALOGO, "purgeMaximumTimeMs": 12000})
    assert vista["purgeMaximumTimeMs"] == 12000
    assert isinstance(vista["purgeMaximumTimeMs"], int)


def test_un_perfil_sin_purgado_no_lo_envia():
    # La placa rechaza el null y conserva su corte compilado si el campo falta.
    assert "purgeMaximumTimeMs" not in profile_contract_view(CATALOGO)


def test_la_subida_de_caldera_declarada_baja_tal_cual():
    vista = profile_contract_view({**CATALOGO, "boilerOffsetC": 3})
    assert vista["boilerOffsetC"] == 3.0
    assert isinstance(vista["boilerOffsetC"], float)


def test_un_perfil_sin_subida_de_caldera_no_la_envia():
    # Ni null ni 0 de relleno: la ausencia es la que dice «sin subida».
    assert "boilerOffsetC" not in profile_contract_view(CATALOGO)
    assert "boilerOffsetC" not in profile_contract_view({**CATALOGO, "boilerOffsetC": None})
