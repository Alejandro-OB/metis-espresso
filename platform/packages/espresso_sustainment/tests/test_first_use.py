"""Pruebas de la subida del primer uso (ADR-018, enmienda del 2026-10-22).

Comprueban propiedades: que la subida nunca baja al crecer el reposo, que nunca
pasa del máximo del acta, que redondear el reposo la deja corta y no larga, que
una persona prevalece sobre cualquier dato y que lo que no consta no pide nada.
"""

import pytest
from espresso_sustainment.first_use import (
    FIRST_USE_RULE_V1,
    WALL_FIRST_USE_V1,
    WALL_FIRST_USE_V2,
    FirstUseInputs,
    FirstUseReason,
    FirstUseRule,
    WallFirstUseModel,
    decide_first_use,
)

RULE = FIRST_USE_RULE_V1


def test_la_subida_nunca_baja_al_crecer_el_reposo():
    offsets = [RULE.offset_for_rest(rest) for rest in range(0, 4 * 3600, 30)]
    assert offsets == sorted(offsets)


def test_la_subida_nunca_pasa_del_maximo_del_acta():
    assert max(RULE.offset_for_rest(rest) for rest in range(0, 24 * 3600, 60)) <= RULE.max_offset_c


def test_los_puntos_medidos_salen_tal_cual():
    for rest, offset in RULE.points:
        assert RULE.offset_for_rest(rest) == offset


def test_redondear_el_reposo_deja_la_subida_corta():
    # Entre dos tramos pide lo del tramo de abajo, nunca más que la recta exacta.
    (rest_a, offset_a), (rest_b, offset_b), (rest_c, offset_c) = RULE.points
    for rest in range(int(rest_a), int(rest_c), 7):
        if rest <= rest_b:
            exact = offset_a + (rest - rest_a) / (rest_b - rest_a) * (offset_b - offset_a)
        else:
            exact = offset_b + (rest - rest_b) / (rest_c - rest_b) * (offset_c - offset_b)
        assert RULE.offset_for_rest(rest) <= exact + 1e-9


def test_un_reposo_corto_no_pide_nada():
    decision = decide_first_use(FirstUseInputs(rest_s=9 * 60), RULE)
    assert decision.offset_c is None
    assert decision.reason is FirstUseReason.SHORT_REST


def test_dentro_de_lo_medido_compensa():
    decision = decide_first_use(FirstUseInputs(rest_s=20 * 60), RULE)
    assert decision.offset_c == 2.7
    assert decision.reason is FirstUseReason.COMPENSATED
    assert decision.rule_version == RULE.version


def test_mas_alla_de_lo_medido_se_queda_en_el_ultimo_punto_y_lo_declara():
    decision = decide_first_use(FirstUseInputs(rest_s=3 * 3600), RULE)
    assert decision.offset_c == RULE.points[-1][1]
    assert decision.reason is FirstUseReason.BEYOND_MEASURED


def test_sin_uso_archivado_no_pide_nada():
    decision = decide_first_use(FirstUseInputs(rest_s=None), RULE)
    assert decision.offset_c is None
    assert decision.reason is FirstUseReason.REST_UNKNOWN


def test_un_hueco_en_la_telemetria_no_pide_nada():
    decision = decide_first_use(FirstUseInputs(rest_s=40 * 60, telemetry_gap=True), RULE)
    assert decision.offset_c is None
    assert decision.reason is FirstUseReason.TELEMETRY_GAP


def test_durante_un_uso_mantiene_lo_ultimo_pedido():
    decision = decide_first_use(FirstUseInputs(rest_s=0, in_use=True, previous_offset_c=3.7), RULE)
    assert decision.offset_c == 3.7
    assert decision.reason is FirstUseReason.IN_USE


@pytest.mark.parametrize(
    ("inputs", "reason"),
    [
        (FirstUseInputs(rest_s=3600, suspended=True), FirstUseReason.SUSPENDED),
        (FirstUseInputs(rest_s=3600, excluded=True), FirstUseReason.EXCLUDED),
        (FirstUseInputs(rest_s=3600, manual_offset_c=3.0), FirstUseReason.MANUAL_OFFSET),
        (FirstUseInputs(rest_s=3600, in_use=True, suspended=True), FirstUseReason.SUSPENDED),
    ],
)
def test_una_persona_prevalece_sobre_el_reposo(inputs, reason):
    decision = decide_first_use(inputs, RULE)
    assert decision.offset_c is None
    assert decision.reason is reason


def test_una_regla_que_pide_mas_que_el_acta_no_se_puede_crear():
    with pytest.raises(ValueError):
        FirstUseRule(version="x", points=((0.0, 0.0), (600.0, 4.5)), max_offset_c=4.0)


# --- El modelo con la pared (primer-uso-pared-v1) ---------------------------------

MODEL = WALL_FIRST_USE_V1


def test_con_la_pared_mas_fria_el_modelo_nunca_pide_menos():
    offsets = [MODEL.offset_for_wall(-x / 10) for x in range(0, 200)]
    assert offsets == sorted(offsets)
    assert max(offsets) <= MODEL.max_offset_c


def test_el_modelo_redondea_hacia_abajo():
    for x in [-3.0, -7.6, -10.0, -11.3]:
        exact = MODEL.deficit_for_wall(x) / MODEL.gain
        assert MODEL.offset_for_wall(x) <= max(0.0, exact)


def test_con_la_pared_decide_el_modelo_y_lo_declara():
    decision = decide_first_use(FirstUseInputs(rest_s=1200.0, relative_wall_c=-7.6), RULE, MODEL)
    assert decision.reason is FirstUseReason.MODEL_WALL
    assert decision.rule_version == MODEL.version
    assert decision.offset_c == MODEL.offset_for_wall(-7.6)


def test_sin_frio_el_modelo_no_pide_nada():
    decision = decide_first_use(FirstUseInputs(rest_s=600.0, relative_wall_c=-1.0), RULE, MODEL)
    assert decision.offset_c is None
    assert decision.reason is FirstUseReason.MODEL_NO_DEFICIT


def test_sin_pared_decide_la_regla():
    decision = decide_first_use(FirstUseInputs(rest_s=1800.0), RULE, MODEL)
    assert decision.reason is FirstUseReason.COMPENSATED
    assert decision.rule_version == RULE.version


def test_una_persona_prevalece_tambien_sobre_el_modelo():
    for inputs in (FirstUseInputs(relative_wall_c=-11.0, suspended=True),
                   FirstUseInputs(relative_wall_c=-11.0, excluded=True),
                   FirstUseInputs(relative_wall_c=-11.0, manual_offset_c=2.0)):
        assert decide_first_use(inputs, RULE, MODEL).offset_c is None


def test_en_uso_el_modelo_mantiene_lo_pedido():
    inputs = FirstUseInputs(relative_wall_c=-11.0, in_use=True, previous_offset_c=2.5)
    assert decide_first_use(inputs, RULE, MODEL).offset_c == 2.5



# --- primer-uso-pared-v2: el refuerzo de la prealimentación (enmienda del 2026-10-27)

V2 = WALL_FIRST_USE_V2


def _wall_for_deficit(deficit_c):
    return (deficit_c - V2.intercept_c) / V2.slope


def test_con_deficit_el_v2_pide_el_refuerzo_y_no_la_subida():
    decision = decide_first_use(FirstUseInputs(relative_wall_c=_wall_for_deficit(2.4)), RULE, V2)
    assert decision.reason is FirstUseReason.MODEL_FEEDFORWARD
    assert decision.feedforward_gain == 1.25
    assert decision.offset_c is None
    assert decision.as_document()["feedforwardGain"] == 1.25


def test_bajo_el_umbral_el_v2_no_pide_nada():
    decision = decide_first_use(FirstUseInputs(relative_wall_c=_wall_for_deficit(1.9)), RULE, V2)
    assert decision.reason is FirstUseReason.MODEL_NO_DEFICIT
    assert decision.feedforward_gain is None
    assert decision.offset_c is None


def test_tras_un_arranque_en_frio_el_v2_sigue_con_la_subida():
    inputs = FirstUseInputs(relative_wall_c=-28.7, cold_start=True)
    decision = decide_first_use(inputs, RULE, V2)
    assert decision.reason is FirstUseReason.MODEL_WALL
    assert decision.offset_c == 4.0
    assert decision.feedforward_gain is None


def test_en_uso_el_v2_mantiene_el_refuerzo():
    inputs = FirstUseInputs(relative_wall_c=0.0, in_use=True, previous_feedforward_gain=1.25)
    decision = decide_first_use(inputs, RULE, V2)
    assert decision.reason is FirstUseReason.IN_USE
    assert decision.feedforward_gain == 1.25


def test_una_persona_prevalece_tambien_sobre_el_refuerzo():
    wall = _wall_for_deficit(3.0)
    for inputs in (FirstUseInputs(relative_wall_c=wall, suspended=True),
                   FirstUseInputs(relative_wall_c=wall, excluded=True),
                   FirstUseInputs(relative_wall_c=wall, manual_offset_c=2.0)):
        decision = decide_first_use(inputs, RULE, V2)
        assert decision.feedforward_gain is None and decision.offset_c is None


def test_el_v1_no_pide_refuerzo():
    decision = decide_first_use(
        FirstUseInputs(relative_wall_c=_wall_for_deficit(3.0)), RULE, WALL_FIRST_USE_V1
    )
    assert decision.feedforward_gain is None
    assert decision.reason is FirstUseReason.MODEL_WALL


@pytest.mark.parametrize("gain, threshold", [(1.25, None), (None, 2.0), (0.0, 2.0)])
def test_un_refuerzo_a_medias_no_se_puede_crear(gain, threshold):
    with pytest.raises(ValueError):
        WallFirstUseModel(
            version="x", intercept_c=0.0, slope=-0.4, gain=0.66, max_offset_c=4.0,
            feedforward_gain=gain, feedforward_min_deficit_c=threshold,
        )


def test_el_v3_pide_el_refuerzo_y_la_subida_juntos():
    from espresso_sustainment.first_use import WALL_FIRST_USE_V3

    wall = _wall_for_deficit(2.04)
    decision = decide_first_use(FirstUseInputs(relative_wall_c=wall), RULE, WALL_FIRST_USE_V3)
    assert decision.reason is FirstUseReason.MODEL_FEEDFORWARD
    assert decision.feedforward_gain == 1.25
    assert decision.offset_c == WALL_FIRST_USE_V3.offset_for_wall(wall) == 3.0
    assert WALL_FIRST_USE_V3.version == "primer-uso-pared-v3"
    # Bajo el umbral, nada, como la v2.
    below = decide_first_use(
        FirstUseInputs(relative_wall_c=_wall_for_deficit(1.9)), RULE, WALL_FIRST_USE_V3
    )
    assert below.offset_c is None and below.feedforward_gain is None


def test_el_v4_pide_la_subida_del_v3_menos_lo_que_aporta_el_refuerzo():
    from espresso_sustainment.first_use import WALL_FIRST_USE_V3, WALL_FIRST_USE_V4

    wall = -9.22  # el reposo de 25 min del 28-oct con la v12 (sim-00000478)
    v3 = decide_first_use(FirstUseInputs(relative_wall_c=wall), RULE, WALL_FIRST_USE_V3)
    v4 = decide_first_use(FirstUseInputs(relative_wall_c=wall), RULE, WALL_FIRST_USE_V4)
    assert (v3.offset_c, v4.offset_c) == (3.5, 2.0)
    assert v4.reason is FirstUseReason.MODEL_FEEDFORWARD
    assert v4.feedforward_gain == WALL_FIRST_USE_V3.feedforward_gain
    # En el umbral del refuerzo (2,0 °C, subida de la v1 de 3,0) quedan 1,5; con
    # una reducción mayor que la subida, ninguna, y el refuerzo solo.
    small = decide_first_use(
        FirstUseInputs(relative_wall_c=_wall_for_deficit(2.0)), RULE, WALL_FIRST_USE_V4
    )
    assert small.offset_c == 1.5 and small.feedforward_gain is not None
    from dataclasses import replace

    larger = replace(WALL_FIRST_USE_V4, feedforward_offset_reduction_c=5.0)
    none = decide_first_use(FirstUseInputs(relative_wall_c=wall), RULE, larger)
    assert none.offset_c is None and none.feedforward_gain is not None
    # Sin refuerzo (tras un arranque en frío) la subida es la de siempre.
    cold = decide_first_use(
        FirstUseInputs(relative_wall_c=wall, cold_start=True), RULE, WALL_FIRST_USE_V4
    )
    assert cold.offset_c == WALL_FIRST_USE_V4.offset_for_wall(wall)
    assert WALL_FIRST_USE_V4.as_document()["feedforwardOffsetReductionC"] == 1.5


def test_el_v5_reduce_la_subida_con_la_ganancia_de_la_recta_v12():
    from espresso_sustainment.first_use import WALL_FIRST_USE_V4, WALL_FIRST_USE_V5

    wall = -9.22  # el mismo reposo de 25 min (sim-00000478)
    v5 = decide_first_use(FirstUseInputs(relative_wall_c=wall), RULE, WALL_FIRST_USE_V5)
    assert v5.offset_c == 2.7
    assert v5.reason is FirstUseReason.MODEL_FEEDFORWARD
    assert v5.feedforward_gain == WALL_FIRST_USE_V4.feedforward_gain
    assert v5.rule_version == "primer-uso-pared-v5"
    # Solo cambia la reducción: la pared, la ganancia, el umbral y el tope son los de la v4.
    v4_doc, v5_doc = WALL_FIRST_USE_V4.as_document(), WALL_FIRST_USE_V5.as_document()
    assert v5_doc.pop("feedforwardOffsetReductionC") == 0.8
    v4_doc.pop("feedforwardOffsetReductionC")
    assert {**v5_doc, "version": None} == {**v4_doc, "version": None}
    # La v4 queda como estaba: decidió cafés archivados.
    assert WALL_FIRST_USE_V4.feedforward_offset_reduction_c == 1.5

def test_una_reduccion_negativa_no_se_puede_crear():
    from dataclasses import replace

    from espresso_sustainment.first_use import WALL_FIRST_USE_V4

    with pytest.raises(ValueError):
        replace(WALL_FIRST_USE_V4, feedforward_offset_reduction_c=-0.5)
