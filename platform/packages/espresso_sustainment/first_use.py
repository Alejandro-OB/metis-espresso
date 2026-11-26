"""Subida de caldera del primer uso tras un reposo (ADR-018, enmienda del 2026-10-22).

El primer café tras un reposo sale más frío que los siguientes: con 20 min de
reposo, 1,8 °C de agua menos. La caldera marca bien en ese café, porque lo que
se enfrió es el grupo, y por eso la recta del acta v6 entre caldera y agua no lo
ve. Corregir después del café llegaría tarde: el frío solo afecta al primero.
Lo que sí lo compensa es subir la caldera **antes** de ese café (ADR-009,
enmienda del 2026-10-19). La placa acepta la subida, la aplica solo en la
preparación siguiente y la devuelve a cero al terminarla.

Este módulo decide cuánto pedir según el reposo, con una regla versionada que
sale entera de lo medido el 19 de octubre (`temperatura-cierre-2026-10-17.md`,
«Ensayo de reposo» y «Compensación del primer uso»):

| Reposo | Subida | Origen |
|---|---|---|
| 10 min | 0 | tras 10 min el primer uso no sale frío |
| 20 min | 2,7 °C | 1,8 °C de agua que faltan, a 0,66 °C de agua por °C de caldera |
| 30 min | 3,7 °C | con +2,9 °C faltaron 0,70 °C y con +4,0 °C sobraron 0,20 °C |

Entre los puntos se interpola en línea recta. El reposo se redondea **hacia
abajo** a tramos de 5 min, por dos razones: así la subida cambia pocas veces
durante una espera (cada cambio es una petición nueva para la placa) y, si se
equivoca, se queda corta en vez de pasarse.

**Por encima de 30 min se queda en 3,7 °C.** No se mide más allá: decisión del
usuario del 2026-10-22. Los déficits medidos a 40 min y a 3 h 13 min (2,8 y
2,9 °C) dicen que ahí el café saldría unos 0,5 °C corto (inferido), nunca de más.
La decisión lo declara con `beyond-measured`.

**Desde el 27 de octubre decide el modelo, y la regla queda de respaldo.** La
subida sale de cuánto más fría está la pared del recorrido, según Pe v7, que al
pulsar los tres últimos cafés (`primer-uso-pared-v1`, `agua-portafiltro.md`
§§ 10.18–10.19). Esa versión cumplió los tres criterios fijados antes: 0,35 °C de
error en los reposos del 19 frente a 0,41 °C de la regla, 0,22 °C en la
comprobación ciega del 30b y 0,81 frente a 0,88 °C en los reposos ciegos del 18.
Predecir los cafés futuros con el modelo no cumplió (§§ 10.3–10.12). Cuando la
plataforma no puede calcular la pared, decide la regla como antes.

**Qué no hace.** No conoce el reloj ni la telemetría: recibe el reposo, la pared
y lo que la plataforma sabe de ellos, y devuelve lo que decidió. No simula: la
pared la calcula `espresso_simulation.water_after_rest.wall_temperatures`. No compensa los cafés
siguientes al primero, que ya salen bien. No supera el máximo firmado en el acta
de temperatura (4,0 °C, acta v7): una regla que lo pidiera no se publica.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from enum import Enum
from itertools import pairwise
from typing import Any


class FirstUseReason(Enum):
    #: El cero es lo conservador: nadie ha decidido subir nada.
    UNDECIDED = "undecided"
    SUSPENDED = "suspended-by-protocol"
    EXCLUDED = "excluded-by-barista"
    #: El perfil ya pide una subida a mano: la del operador manda.
    MANUAL_OFFSET = "manual-offset-in-profile"
    #: Sin un uso archivado no se sabe cuánto lleva en reposo.
    REST_UNKNOWN = "rest-unknown"
    #: La telemetría tiene un hueco: pudo haber un uso que la plataforma no vio,
    #: y con él un reposo más corto que el calculado. Pedir de más pasaría el
    #: café; no pedir nada lo deja como está hoy.
    TELEMETRY_GAP = "telemetry-gap"
    #: Con una preparación o un purgado en curso se mantiene lo último pedido:
    #: cambiar la petición a mitad de un uso no la deja aplicarse a ese uso.
    IN_USE = "in-use"
    SHORT_REST = "short-rest"
    COMPENSATED = "compensated"
    BEYOND_MEASURED = "beyond-measured"
    #: Lo decidió el modelo con la pared del recorrido (`primer-uso-pared-v1`).
    MODEL_WALL = "model-wall"
    #: El modelo calculó la pared y no falta agua: no se pide subida.
    MODEL_NO_DEFICIT = "model-no-deficit"
    #: El modelo pide el refuerzo de la prealimentación durante el café en lugar
    #: de la subida (`primer-uso-pared-v2`, ADR-009, enmienda del 2026-10-27).
    MODEL_FEEDFORWARD = "model-feedforward"


@dataclass(frozen=True)
class FirstUseRule:
    version: str
    #: Pares (reposo en s, subida en °C), en orden creciente de reposo.
    points: tuple[tuple[float, float], ...]
    #: Máximo firmado en el acta de temperatura vigente.
    max_offset_c: float
    step_s: float = 300.0
    sources: tuple[str, ...] = field(default_factory=tuple)

    def __post_init__(self) -> None:
        if not self.version:
            raise ValueError("una regla sin versión no se puede citar")
        if len(self.points) < 2:
            raise ValueError("la regla necesita al menos dos puntos")
        rests = [rest for rest, _ in self.points]
        if rests != sorted(rests) or len(set(rests)) != len(rests):
            raise ValueError("los reposos de la regla deben crecer")
        if any(offset < 0.0 for _, offset in self.points):
            raise ValueError("la subida no puede ser negativa")
        if any(offset > self.max_offset_c for _, offset in self.points):
            raise ValueError("la regla pide más que el máximo del acta")
        if self.step_s <= 0.0:
            raise ValueError("el tramo debe ser positivo")

    @property
    def measured_up_to_s(self) -> float:
        return self.points[-1][0]

    def offset_for_rest(self, rest_s: float) -> float:
        """La subida para un reposo, con el reposo redondeado hacia abajo."""
        rest = max(0.0, rest_s)
        rest = (rest // self.step_s) * self.step_s
        first_rest, first_offset = self.points[0]
        if rest <= first_rest:
            return first_offset
        for (rest_a, offset_a), (rest_b, offset_b) in pairwise(self.points):
            if rest <= rest_b:
                fraction = (rest - rest_a) / (rest_b - rest_a)
                return round(offset_a + fraction * (offset_b - offset_a), 2)
        return self.points[-1][1]

    def as_document(self) -> dict[str, Any]:
        return {
            "version": self.version,
            "points": [{"restS": rest, "offsetC": offset} for rest, offset in self.points],
            "maxOffsetC": self.max_offset_c,
            "stepS": self.step_s,
            "measuredUpToS": self.measured_up_to_s,
            "sources": list(self.sources),
        }


@dataclass(frozen=True)
class WallFirstUseModel:
    """La subida según la pared del recorrido: `D` = a + b·x, subida = `D`/g.

    `x` es la pared ahora menos la media de la pared al pulsar los tres últimos
    cafés, las dos de Pe v7 con lo que ya pasó. Los números son los del ajuste con
    los cinco reposos del 19 de octubre (`intentos-11-a-13-v8.txt`); la ganancia,
    la medida en los usos 325 y 335.
    """

    version: str
    intercept_c: float
    slope: float
    gain: float
    max_offset_c: float
    #: La subida se redondea hacia abajo a este paso: cambia pocas veces durante
    #: una espera y, si se equivoca, se queda corta, como la regla.
    step_c: float = 0.5
    #: Tras un arranque en frío no hay tres cafés con que comparar: la pared se
    #: compara con la típica de los cafés recuperados del 19 (`intento-14-v8.txt`).
    cold_start_reference_wall_c: float | None = None
    #: Refuerzo de la prealimentación que se pide en lugar de la subida cuando
    #: el déficit estimado llega a `feedforward_min_deficit_c` (versión 2). Sin
    #: él, el modelo solo pide subida, como la versión 1. Tras un arranque en
    #: frío sigue pidiendo subida: el refuerzo no se midió ahí.
    feedforward_gain: float | None = None
    feedforward_min_deficit_c: float | None = None
    #: Con el refuerzo, pedir también la subida en el mismo café (versión 3): el
    #: refuerzo sube el primer café unos 2,3 °C y no quita todo el déficit.
    feedforward_with_offset: bool = False
    #: Lo que se le quita a esa subida cuando va con el refuerzo (versión 4), en °C
    #: de caldera. La v3 pedía la subida entera de la v1 como si el refuerzo no
    #: aportara, y con la caldera alta de las actas v11 y v12 el primer café salió
    #: caliente. Se resta después del paso: la subida cambia tan pocas veces como
    #: en la v3, aunque ya no caiga en pasos de 0,5.
    feedforward_offset_reduction_c: float = 0.0
    sources: tuple[str, ...] = field(default_factory=tuple)

    def __post_init__(self) -> None:
        if not self.version:
            raise ValueError("un modelo sin versión no se puede citar")
        if self.gain <= 0.0 or self.step_c <= 0.0 or self.max_offset_c < 0.0:
            raise ValueError("ganancia, paso y máximo deben ser positivos")
        if (self.feedforward_gain is None) != (self.feedforward_min_deficit_c is None):
            raise ValueError("el refuerzo necesita su ganancia y su umbral, o ninguno")
        if self.feedforward_gain is not None and self.feedforward_gain <= 0.0:
            raise ValueError("la ganancia del refuerzo debe ser positiva")
        if self.feedforward_offset_reduction_c < 0.0:
            raise ValueError("la reducción de la subida con refuerzo no puede ser negativa")

    def deficit_for_wall(self, relative_wall_c: float) -> float:
        return self.intercept_c + self.slope * relative_wall_c

    def offset_for_wall(self, relative_wall_c: float) -> float:
        raw = max(0.0, self.deficit_for_wall(relative_wall_c) / self.gain)
        stepped = (raw // self.step_c) * self.step_c
        return round(min(stepped, self.max_offset_c), 2)

    def as_document(self) -> dict[str, Any]:
        return {
            "version": self.version,
            "interceptC": self.intercept_c,
            "slope": self.slope,
            "gain": self.gain,
            "maxOffsetC": self.max_offset_c,
            "stepC": self.step_c,
            "coldStartReferenceWallC": self.cold_start_reference_wall_c,
            "feedforwardGain": self.feedforward_gain,
            "feedforwardMinDeficitC": self.feedforward_min_deficit_c,
            "feedforwardWithOffset": self.feedforward_with_offset,
            "feedforwardOffsetReductionC": self.feedforward_offset_reduction_c,
            "sources": list(self.sources),
        }


@dataclass(frozen=True)
class FirstUseInputs:
    #: Segundos desde el fin del último uso con agua; None si no consta ninguno.
    rest_s: float | None = None
    telemetry_gap: bool = False
    in_use: bool = False
    #: Lo último que se pidió, para mantenerlo durante un uso.
    previous_offset_c: float | None = None
    suspended: bool = False
    excluded: bool = False
    manual_offset_c: float | None = None
    #: Pared ahora menos la de los tres últimos cafés, de Pe; None si no se pudo
    #: calcular, y entonces decide la regla.
    relative_wall_c: float | None = None
    #: La pared se comparó con la de referencia por un arranque en frío.
    cold_start: bool = False
    #: El refuerzo pedido la última vez, para mantenerlo durante un uso.
    previous_feedforward_gain: float | None = None


@dataclass(frozen=True)
class FirstUseDecision:
    #: Lo que se pide a la placa; None es no pedir nada (la placa lo lee como 0).
    offset_c: float | None = None
    reason: FirstUseReason = FirstUseReason.UNDECIDED
    rule_version: str | None = None
    rest_s: float | None = None
    #: Ganancia de prealimentación que se pide para el próximo café; None es la
    #: del acta.
    feedforward_gain: float | None = None

    def as_document(self) -> dict[str, Any]:
        return {
            "offsetC": self.offset_c,
            "feedforwardGain": self.feedforward_gain,
            "reason": self.reason.value,
            "ruleVersion": self.rule_version,
            "restS": self.rest_s,
        }


def decide_first_use(
    inputs: FirstUseInputs, rule: FirstUseRule, model: WallFirstUseModel | None = None
) -> FirstUseDecision:
    """Cuánto subir la caldera para el próximo café, y por qué.

    Con `model` y la pared calculada decide el modelo; si no, la regla.
    """
    use_model = model is not None and inputs.relative_wall_c is not None

    def decision(
        offset: float | None, reason: FirstUseReason, gain: float | None = None
    ) -> FirstUseDecision:
        version = model.version if use_model and model is not None else rule.version
        return FirstUseDecision(
            offset_c=offset, reason=reason, rule_version=version, rest_s=inputs.rest_s,
            feedforward_gain=gain,
        )

    # Las decisiones de personas van antes que cualquier dato, como en § 4.6.
    if inputs.suspended:
        return decision(None, FirstUseReason.SUSPENDED)
    if inputs.excluded:
        return decision(None, FirstUseReason.EXCLUDED)
    if inputs.manual_offset_c is not None:
        return decision(None, FirstUseReason.MANUAL_OFFSET)
    if inputs.in_use:
        return decision(
            inputs.previous_offset_c, FirstUseReason.IN_USE, inputs.previous_feedforward_gain
        )
    if use_model and model is not None and inputs.relative_wall_c is not None:
        if (
            model.feedforward_gain is not None
            and model.feedforward_min_deficit_c is not None
            and not inputs.cold_start
        ):
            if model.deficit_for_wall(inputs.relative_wall_c) >= model.feedforward_min_deficit_c:
                # En la v2, el refuerzo solo; en la v3, con la subida de la v1; en la
                # v4, con esa subida menos lo que el refuerzo ya aporta.
                offset = (
                    max(0.0, model.offset_for_wall(inputs.relative_wall_c)
                        - model.feedforward_offset_reduction_c)
                    if model.feedforward_with_offset
                    else 0.0
                )
                return decision(
                    offset if offset > 0.0 else None,
                    FirstUseReason.MODEL_FEEDFORWARD,
                    model.feedforward_gain,
                )
            return decision(None, FirstUseReason.MODEL_NO_DEFICIT)
        offset = model.offset_for_wall(inputs.relative_wall_c)
        if offset <= 0.0:
            return decision(None, FirstUseReason.MODEL_NO_DEFICIT)
        return decision(offset, FirstUseReason.MODEL_WALL)
    if inputs.rest_s is None:
        return decision(None, FirstUseReason.REST_UNKNOWN)
    if inputs.telemetry_gap:
        return decision(None, FirstUseReason.TELEMETRY_GAP)
    offset = rule.offset_for_rest(inputs.rest_s)
    if offset <= 0.0:
        return decision(None, FirstUseReason.SHORT_REST)
    if inputs.rest_s > rule.measured_up_to_s:
        return decision(offset, FirstUseReason.BEYOND_MEASURED)
    return decision(offset, FirstUseReason.COMPENSATED)


#: Primera versión, del ensayo del 19 de octubre de 2026 y el acta v7.
FIRST_USE_RULE_V1 = FirstUseRule(
    version="primer-uso-v1",
    points=((600.0, 0.0), (1200.0, 2.7), (1800.0, 3.7)),
    max_offset_c=4.0,
    sources=(
        "docs/engineering/evidencias/temperatura-cierre-2026-10-17.md",
        "docs/engineering/actas/acta-temperatura-20261019.json",
    ),
)


#: Primera versión del modelo, de los cinco reposos del 19 de octubre de 2026
#: (`agua-portafiltro.md` § 10.19, commit 14767a5) y el tope del acta v7.
WALL_FIRST_USE_V1 = WallFirstUseModel(
    version="primer-uso-pared-v1",
    intercept_c=-1.243,
    slope=-0.390,
    gain=0.66,
    max_offset_c=4.0,
    cold_start_reference_wall_c=75.0,
    sources=(
        "model/Documentacion/agua-portafiltro.md",
        "model/agua_portafiltro/intentos-11-a-13-v8.txt",
        "model/agua_portafiltro/intento-14-v8.txt",
        "docs/engineering/evidencias/temperatura-cierre-2026-10-17.md",
    ),
)


#: Segunda versión: la misma pared, y en lugar de la subida, el refuerzo de la
#: prealimentación durante el café, medido el 27 de octubre de 2026 en bloques
#: cruzados (`agua-portafiltro.md` § 10.26, commit 654eb8c). La ganancia es la
#: del binario de ensayo; el umbral, el déficit que el modelo estimó para el
#: reposo más corto en que el refuerzo no se pasó (15 min, 2,02 °C, § 10.23).
#: Provisional hasta su validación.
WALL_FIRST_USE_V2 = WallFirstUseModel(
    version="primer-uso-pared-v2",
    intercept_c=-1.243,
    slope=-0.390,
    gain=0.66,
    max_offset_c=4.0,
    cold_start_reference_wall_c=75.0,
    feedforward_gain=1.25,
    feedforward_min_deficit_c=2.0,
    sources=(
        *WALL_FIRST_USE_V1.sources,
        "model/agua_portafiltro/sesion-27-oct-v8.txt",
    ),
)


#: Tercera versión: la de la v2 y, en el mismo café, la subida de la v1. La
#: validación de la v2 (`agua-portafiltro.md` § 10.27) mostró que el refuerzo solo
#: sube el primer café unos 2,3 °C y deja 1,9–3,5 °C de déficit. Se prueba en la
#: § 10.28; hasta entonces, provisional.
WALL_FIRST_USE_V3 = replace(
    WALL_FIRST_USE_V2,
    version="primer-uso-pared-v3",
    feedforward_with_offset=True,
)

#: Cuarta versión: la de la v3 con 1,5 °C menos de subida cuando va con el
#: refuerzo. Con las actas v11 y v12 la v3 dejó el primer café 0,88 y 0,74 °C más
#: caliente que los usos 3 a 5 tras 25 min (`agua-portafiltro.md` § 10.31 y
#: § 10.33). El 1,5 sale del bloque de la v12 (sim-00000478–482): con la ganancia
#: de 0,66 °C de agua por °C de caldera, le quita al primer café 1,0 °C y lo deja
#: unos +0,25 °C sobre los demás, en medio del criterio de −0,5 a +1,0 °C. Ajustado
#: con un solo bloque: provisional hasta su validación.
WALL_FIRST_USE_V4 = replace(
    WALL_FIRST_USE_V3,
    version="primer-uso-pared-v4",
    feedforward_offset_reduction_c=1.5,
)

#: Quinta versión: la de la v4 con 0,8 °C de reducción en lugar de 1,5. La v4 se
#: calculó con la ganancia de 0,66 °C de agua por °C de caldera del 19-oct, medida
#: con otra recta; con la v12 la subida rinde ~1,23 (§ 10.33: subida 3,5 → 92,98 °C,
#: sim-00000478; subida 2,0 → 91,14 °C, sim-00000483). Con esa ganancia, 0,8 °C le
#: quitan al primer café ~1,0 °C y lo dejan ~0,24 °C bajo los usos 3 a 5 del bloque
#: de la v3 (`D_med` ≈ +0,24, en medio del criterio de −0,5 a +1,0 °C). La ganancia
#: sale de un café por bloque: provisional hasta su validación.
WALL_FIRST_USE_V5 = replace(
    WALL_FIRST_USE_V4,
    version="primer-uso-pared-v5",
    feedforward_offset_reduction_c=0.8,
)
