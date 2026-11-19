"""Agua sobre el café estimada por el gemelo, con el reposo y los usos seguidos.

Módulo puro: no conoce la aplicación, el almacenamiento ni el reloj. Es la
variante «Pe» del modelo del agua tras un reposo, versión 7
(`models/modelica/Documentacion/agua-portafiltro-reposo.md`; ajuste en
`models/modelica/agua_portafiltro/ajustar_reposo.py`, parámetros en
`parametros-reposo-v7-Pe.json`), con la caldera en el modo con que se ajustó:
los dos nodos de agua de la recalibración 10.2 integrados de forma continua con
el metal medido como frontera (`modelo.py`, `_agua_con_metal_medido`).

**Por qué hace falta.** La recta del acta v6 da el agua desde el metal al
pulsar, y en el primer café tras un reposo el metal marca bien: el que se enfrió
es el recorrido por el grupo y el portafiltro. Pe lleva ese recorrido como una
pared con masa que el agua calienta y el reposo enfría:

    dT_g/dt = ṁ·c_p·ε·(T_w1 − T_g)/C_g + (α·T_m + (1 − α)·T_a − T_g)/τ_g
    T_out   = min(T_g + (T_w1 − T_g)·(1 − ε), T_eb),   ε = 1 − exp(−UA/(ṁ·c_p))

con T_w1 el agua del nodo superior de la caldera y T_eb el hervor local.

**Lo que acierta y lo que no** (comprobaciones ciegas del 2026-10-19, acta
`temperatura-cierre-2026-10-17.md`): el primer uso tras 20 min, 1 h 30 min y
3 h 13 min de reposo con −0,56 a +0,17 °C de error; el segundo uso lo da de 1 a
1,7 °C más frío de lo que sale, porque la máquina se recupera en un uso y Pe en
dos. Cada estimación lo declara con `useIndex`, para que quien la lea sepa cuál
de los dos errores le toca.

**Lo que devuelve es la lectura del termistor dentro del portafiltro**, el
NTC-3 con que se midió, ajustó y comprobó el modelo (constante de 1,05 s), no el
agua a la salida del grupo: es la magnitud cuyos errores constan. Arranca en la
temperatura de la pared al pulsar; en el ajuste arrancaba en su propia lectura,
que en el uso normal no existe.

**Supuestos que el uso normal obliga a declarar.** El ajuste usó una sonda al
aire como temperatura del cuarto y del depósito; en el uso normal no hay sonda,
y la recibe quien llama (la anotada en el contexto de la preparación). El caudal
de cada uso es su media (masa de la bebida entre duración); el ajuste usó la
pendiente de la báscula, y la masa en la taza llega un poco después del agua que
sale del grupo.

**Qué no hace.** No simula el metal: lo recibe medido. Sin metal no hay
estimación, y un hueco no se rellena más allá de la interpolación lineal entre
dos lecturas. No corrige nada ni decide: devuelve el agua estimada de cada uso.
"""

from __future__ import annotations

import math
from collections.abc import Sequence
from dataclasses import dataclass

CP = 4186.0  # J/(kg·K), agua (Bergman, tabla A.6)
#: Agua útil de la caldera, 81 ± 3 mL (puesta en servicio, 0.4).
WATER_HEAT_CAPACITY = 0.081 * CP
#: Recalibración 10.2, partición A, estructura 3Sn (`parametros-3Sn-A.json`).
G_FORCED = 83.69869301410273
G_NATURAL = 2.693202049526016
DT = 0.5
SUB = 4
#: La media del chorro que se mide sobre el café: de 5 a 19 s desde el pulsado.
MEAN_WINDOW_S = (5.0, 19.0)
MODEL_VERSION = "agua-reposo-Pe-v7"
#: Caudal de un uso cuando no consta su masa: la mediana de los 92 usos de agua
#: sola del 18 y 19 de octubre con que se ajustó Pe (3,25 g/s, de 2,32 a 3,72).
#: Provisional: con café el caudal es otro, y por eso quien llama lo declara.
DEFAULT_FLOW_KG_S = 3.25e-3
#: Caudal de un purgado, que no deja masa: el inferido de su caída en la 10.2.
PURGE_FLOW_KG_S = 5.5e-3


@dataclass(frozen=True)
class WallParameters:
    heat_capacity: float  # C_g, J/K
    time_constant_s: float  # τ_g
    alpha: float  # α
    conductance: float  # UA, W/K
    boiling_c: float  # T_eb
    probe_time_constant_s: float  # τ_n del NTC-3 con que se ajustó y se comprobó


#: `parametros-reposo-v7-Pe.json` (2026-10-19). T_eb = 94,2 °C, hervor local en
#: Popayán medido en la 0.7.
PE_V7 = WallParameters(
    heat_capacity=125.83119488581669,
    time_constant_s=582.8410978924518,
    alpha=0.5059263797236716,
    conductance=3.7207750197265947,
    boiling_c=94.2,
    probe_time_constant_s=1.0522587913259747,
)


@dataclass(frozen=True)
class Use:
    """Un paso de agua por el grupo: café o purgado."""

    start_s: float
    duration_s: float
    flow_kg_s: float
    reference: str | None = None


@dataclass(frozen=True)
class UseEstimate:
    reference: str | None
    start_s: float
    #: 1 para el primer uso tras un reposo, 2 para el segundo…
    use_index: int
    rest_s: float | None
    mean_water_c: float | None
    model_version: str = MODEL_VERSION

    def as_document(self) -> dict[str, object]:
        return {
            "reference": self.reference,
            "useIndex": self.use_index,
            "restS": self.rest_s,
            "meanWaterC": self.mean_water_c,
            "window": list(MEAN_WINDOW_S),
            "modelVersion": self.model_version,
        }


#: Un uso cuenta como «primero tras un reposo» si el anterior terminó hace más
#: de esto. Tras 10 min el primer uso ya no sale frío (ensayo del 2026-10-19).
REST_THRESHOLD_S = 600.0


def _metal_at(metal: Sequence[tuple[float, float]], t: float, cursor: list[int]) -> float:
    i = cursor[0]
    while i + 1 < len(metal) and metal[i + 1][0] <= t:
        i += 1
    cursor[0] = i
    if i + 1 >= len(metal) or t <= metal[i][0]:
        return metal[i][1]
    (t0, y0), (t1, y1) = metal[i], metal[i + 1]
    return y0 + (y1 - y0) * (t - t0) / (t1 - t0)


def _simulate(
    metal: Sequence[tuple[float, float]],
    uses: Sequence[Use],
    ambient_c: float,
    wall: WallParameters,
    initial_wall_c: float | None,
    record_s: Sequence[float] = (),
) -> tuple[list[Use], list[list[float]], list[float | None]]:
    """La simulación de Pe: usos ordenados, sumas de la media y la pared en `record_s`.

    La pared se guarda en el primer paso en o después de cada instante pedido,
    antes de avanzarla: es el estado de ese instante, como la columna «pared»
    del ajuste.
    """
    ordered = sorted(uses, key=lambda use: use.start_s)
    t = metal[0][0]
    end = max([metal[-1][0], *(u.start_s + u.duration_s for u in ordered)])
    cursor = [0]
    t_m = _metal_at(metal, t, cursor)
    top = bottom = t_m
    wall_t = (
        initial_wall_c
        if initial_wall_c is not None
        else wall.alpha * t_m + (1.0 - wall.alpha) * ambient_c
    )
    sums = [[0.0, 0] for _ in ordered]
    h = DT / SUB
    cw1 = cw2 = 0.5 * WATER_HEAT_CAPACITY
    use_index = 0
    probe: float | None = None
    pending = sorted((at, k) for k, at in enumerate(record_s))
    recorded: list[float | None] = [None] * len(record_s)
    while t <= end + 1e-9:
        while pending and pending[0][0] <= t + 1e-9:
            recorded[pending.pop(0)[1]] = wall_t
        while use_index + 1 < len(ordered) and t >= ordered[use_index + 1].start_s - DT / 2:
            use_index += 1
        current = None
        if ordered and use_index < len(ordered):
            candidate = ordered[use_index]
            if candidate.start_s - DT / 2 <= t < candidate.start_s + candidate.duration_s - DT / 2:
                current = candidate
        t_m = _metal_at(metal, t, cursor)
        flow = current.flow_kg_s if current else 0.0
        mc = flow * CP
        # Agua de la caldera con el metal medido como frontera (Heun, 4 subpasos).
        g = 0.5 * (G_FORCED if current else G_NATURAL)
        top_now = top
        for _ in range(SUB):
            a1 = (g * (t_m - top) + mc * (bottom - top)) / cw1
            a2 = (g * (t_m - bottom) + mc * (ambient_c - bottom)) / cw2
            p1, p2 = top + h * a1, bottom + h * a2
            b1 = (g * (t_m - p1) + mc * (p2 - p1)) / cw1
            b2 = (g * (t_m - p2) + mc * (ambient_c - p2)) / cw2
            top += 0.5 * h * (a1 + b1)
            bottom += 0.5 * h * (a2 + b2)
        # Pared: lineal en su estado con entradas fijas en el paso, solución exacta.
        eps = 1.0 - math.exp(-wall.conductance / mc) if mc > 0 else 0.0
        reference = wall.alpha * t_m + (1.0 - wall.alpha) * ambient_c
        if current is not None:
            out = min(wall_t + (top_now - wall_t) * (1.0 - eps), wall.boiling_c)
            if probe is None:
                probe = wall_t
            elapsed = t - current.start_s
            if MEAN_WINDOW_S[0] <= elapsed <= MEAN_WINDOW_S[1]:
                sums[use_index][0] += probe
                sums[use_index][1] += 1
            probe = out + (probe - out) * math.exp(-DT / wall.probe_time_constant_s)
        else:
            probe = None
        rate = mc * eps / wall.heat_capacity + 1.0 / wall.time_constant_s
        equilibrium = (mc * eps / wall.heat_capacity * top_now + reference / wall.time_constant_s) / rate
        wall_t = equilibrium + (wall_t - equilibrium) * math.exp(-rate * DT)
        t += DT
    # Un instante entre el último paso y el final cae en el paso que ya no se dio
    # (la rejilla arranca en la primera lectura, no en cero): es el estado final.
    for at, k in pending:
        if at <= end + DT:
            recorded[k] = wall_t
    return ordered, sums, recorded


def estimate_uses(
    metal: Sequence[tuple[float, float]],
    uses: Sequence[Use],
    ambient_c: float,
    wall: WallParameters = PE_V7,
    initial_wall_c: float | None = None,
) -> list[UseEstimate]:
    """El agua media de 5 a 19 s de cada uso, simulando desde la primera lectura.

    `metal` son pares (s, °C) ordenados; los usos, en la misma escala de tiempo.
    Sin `initial_wall_c` la pared arranca en su equilibrio con el metal y el
    cuarto, que es lo que vale tras un reposo largo o con la máquina recién
    encendida.
    """
    metal = sorted(metal)
    if len(metal) < 2:
        return []
    ordered, sums, _ = _simulate(metal, uses, ambient_c, wall, initial_wall_c)
    estimates = []
    sequence = 0
    previous_end: float | None = None
    for use, (total, count) in zip(ordered, sums, strict=True):
        rest = None if previous_end is None else use.start_s - previous_end
        sequence = 1 if rest is None or rest > REST_THRESHOLD_S else sequence + 1
        estimates.append(
            UseEstimate(
                reference=use.reference,
                start_s=use.start_s,
                use_index=sequence,
                rest_s=rest,
                mean_water_c=round(total / count, 2) if count else None,
            )
        )
        previous_end = use.start_s + use.duration_s
    return estimates


def wall_temperatures(
    metal: Sequence[tuple[float, float]],
    uses: Sequence[Use],
    ambient_c: float,
    times_s: Sequence[float],
    wall: WallParameters = PE_V7,
    initial_wall_c: float | None = None,
) -> list[float | None]:
    """La pared del recorrido en cada instante pedido, con lo que ya pasó.

    La usa la subida del primer uso según la pared (`espresso_sustainment`,
    regla `primer-uso-pared-v1`): cuánto más fría está la pared ahora que al
    pulsar los últimos cafés. Un instante fuera de la simulación da None.
    """
    metal = sorted(metal)
    if len(metal) < 2:
        return [None] * len(times_s)
    end = max([metal[-1][0], *(u.start_s + u.duration_s for u in uses)])
    inside = [at if metal[0][0] <= at <= end else math.inf for at in times_s]
    _, _, recorded = _simulate(metal, uses, ambient_c, wall, initial_wall_c, inside)
    return [value if at != math.inf else None for value, at in zip(recorded, inside, strict=True)]


__all__ = [
    "DEFAULT_FLOW_KG_S",
    "PE_V7",
    "PURGE_FLOW_KG_S",
    "Use",
    "UseEstimate",
    "WallParameters",
    "estimate_uses",
    "wall_temperatures",
]
