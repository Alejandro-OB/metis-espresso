"""Sostenimiento del objetivo en la plataforma (ADR-018).

Lo que actúa en la máquina es la **subida del primer uso** (§ 12): el café frío
de la máquina real es el primero tras un reposo, con la caldera marcando bien.
`first_use` decide antes de ese café y la sincronización baja la subida con la
configuración que rige. Desde el 27-oct decide el modelo: cuánto más fría está
la pared del recorrido, según el gemelo, que al pulsar los tres últimos cafés
(`primer-uso-pared-v1`). Tras un arranque en frío, sin tres cafés con que
comparar, la pared se compara con la típica de los cafés recuperados. Si la
plataforma no puede calcularla —un hueco en la telemetría con la máquina
caliente, o menos de tres cafés sin arranque en frío—, decide la regla medida
con el reposo, como antes. Cada cambio queda guardado con su motivo y
la versión de la regla. Aquí viven también la exclusión del barista y la
suspensión del protocolo, que la dejan en cero.

**Qué no hace.** No corrige después de cada café: esa corrección, la de la § 4,
se retiró el 2026-10-22 (§ 13) porque en la máquina real no podía corregir
nunca. El agua estimada sale de la misma recta que usa la placa para fijar la
caldera y da el objetivo por construcción, y el café que sí sale frío, el
primero tras un reposo, ya lo cubre la subida. Tampoco toca el objetivo ni el
tiempo máximo, no cambia lo pedido durante un uso y no salta la validación de
la placa, que acota la subida con el máximo de su acta.
"""

from __future__ import annotations

from datetime import timedelta
from itertools import pairwise
from statistics import fmean
from typing import TYPE_CHECKING, Any
from uuid import uuid4

from espresso_simulation.water_after_rest import wall_temperatures
from espresso_sustainment.first_use import (
    FIRST_USE_RULE_V1,
    WALL_FIRST_USE_V1,
    WALL_FIRST_USE_V5,
    FirstUseDecision,
    FirstUseInputs,
    FirstUseReason,
    FirstUseRule,
    WallFirstUseModel,
    decide_first_use,
)

from aplicacion_web.domain.errors import ConflictError, ValidationError
from aplicacion_web.domain.models import Identity, iso_now, parse_instant, utc_instant_text

from .room_temperature import assumed_room_c

if TYPE_CHECKING:
    from .services import ApplicationService

#: Estados de la placa en que pasa agua por el grupo: terminan el reposo.
_USE_STATES = ("extraction", "manual-purge")

#: Separación máxima entre dos muestras de telemetría para dar por vista toda la
#: espera. Fuera de una preparación la placa envía una cada 15 s
#: (`CAFE_TELEMETRY_IDLE_INTERVAL_MS`); con 60 s caben tres perdidas, y un uso
#: dura más que eso.
_TELEMETRY_GAP_S = 60.0

#: Historia con que se calcula la pared. Con 2 h la pared sale igual que con la
#: del día anterior (`agua-portafiltro.md` § 10.6), y leerla cuesta la mitad que
#: las 4 h del gemelo de cada café, en una sincronización que la placa corta a los 2 s.
_WALL_HISTORY = timedelta(hours=2)

#: El modelo que decide: desde el 29-oct de 2026, la pared con el refuerzo de la
#: prealimentación y la subida juntos, con la subida reducida por lo que aporta el
#: refuerzo con la recta v12 (estudio, § 10.33; antes, la v4 con la reducción mal
#: calculada y la v3 de la § 10.28). Se lee en cada llamada para que una prueba
#: pueda fijar otro.
FIRST_USE_MODEL: WallFirstUseModel | None = WALL_FIRST_USE_V5
_CURRENT_MODEL = object()
#: La pared se recalcula a lo sumo una vez por minuto y por máquina. Cuesta una
#: simulación de 2 h, y la placa corta la sincronización a los 2 s; en un minuto
#: la pared cambia poco, y la subida va en pasos de 0,5 °C.
_WALL_EVERY_S = 60.0
_wall_cache: dict[str, tuple[float, str | None, dict[str, Any] | None]] = {}


class SustainmentService:
    """Exclusión, suspensión y subida del primer uso."""

    def __init__(self, service: ApplicationService) -> None:
        self.service = service
        self.repository = service.repository

    # --- Exclusión y suspensión ---------------------------------------------

    def exclude(self, profile_id: str, identity: Identity, reason: str | None) -> dict[str, Any]:
        """El barista excluye una configuración del sostenimiento (§ 4.3)."""
        self.service.get("profile", profile_id)
        record = {
            "id": profile_id,
            "profileId": profile_id,
            "status": "active",
            "reason": reason,
            "excludedBy": identity.subject,
            "excludedAt": iso_now(),
        }
        return self.repository.upsert_document("sustainment-exclusion", profile_id, record)

    def exclusion(self, profile_id: str) -> dict[str, Any]:
        self.service.get("profile", profile_id)
        record = self.repository.get_document("sustainment-exclusion", profile_id)
        excluded = bool(record) and record.get("status") == "active"
        return {"profileId": profile_id, "excluded": excluded, "record": record}

    def include(self, profile_id: str, identity: Identity) -> dict[str, Any]:
        """Retira la exclusión. Queda registrada, no se borra."""
        record = self.repository.get_document("sustainment-exclusion", profile_id)
        if record is None or record.get("status") != "active":
            raise ConflictError("esa configuración no está excluida")
        return self.repository.upsert_document(
            "sustainment-exclusion",
            profile_id,
            {**record, "status": "withdrawn", "withdrawnBy": identity.subject,
             "withdrawnAt": iso_now()},
        )

    def suspend(self, identity: Identity, reason: str) -> dict[str, Any]:
        """El protocolo suspende el sostenimiento mientras dure una serie comparable."""
        if not reason or not reason.strip():
            raise ValidationError("una suspensión debe decir qué serie la motiva")
        if self._active_suspension() is not None:
            raise ConflictError("el sostenimiento ya está suspendido")
        identifier = f"suspension-{uuid4().hex[:12]}"
        return self.repository.add_document(
            "sustainment-suspension",
            identifier,
            {"id": identifier, "status": "active", "reason": reason.strip(),
             "suspendedBy": identity.subject, "suspendedAt": iso_now()},
        )

    def resume(self, identity: Identity) -> dict[str, Any]:
        active = self._active_suspension()
        if active is None:
            raise ConflictError("el sostenimiento no está suspendido")
        return self.repository.upsert_document(
            "sustainment-suspension",
            str(active["id"]),
            {**active, "status": "lifted", "liftedBy": identity.subject, "liftedAt": iso_now()},
        )

    def _active_suspension(self) -> dict[str, Any] | None:
        items = self.repository.list_documents(
            "sustainment-suspension", page_size=100, filters={"status": "active"}
        ).items
        return items[0] if items else None

    def _excluded(self, profile_id: str) -> bool:
        record = self.repository.get_document("sustainment-exclusion", profile_id)
        return bool(record) and record.get("status") == "active"

    def _ruling_profile(self) -> dict[str, Any] | None:
        items = self.repository.list_documents(
            "profile", page_size=8, filters={"status": "accepted"}
        ).items
        return items[0] if items else None

    # --- Subida del primer uso (§ 12) ----------------------------------------

    def first_use(
        self,
        device_id: str,
        rule: FirstUseRule = FIRST_USE_RULE_V1,
        model: WallFirstUseModel | None | object = _CURRENT_MODEL,
    ) -> FirstUseDecision:
        """Cuánto pedir a la placa para el próximo café, guardado si cambió.

        El reposo se cuenta desde la última muestra de telemetría en que pasaba
        agua por el grupo hasta la última recibida, las dos con el reloj de la
        placa: el instante del servidor no dice nada de cuándo dejó de pasar
        agua. Fuera de lo que la telemetría vio no se supone nada; un hueco en
        la espera deja la petición en cero.
        """
        if model is _CURRENT_MODEL:
            model = FIRST_USE_MODEL
        if model is not None and not isinstance(model, WallFirstUseModel):
            raise TypeError("model tiene que ser un WallFirstUseModel o None")
        latest = self.repository.latest_machine_telemetry()
        if latest is not None and latest.get("deviceId") != device_id:
            latest = None
        previous = self._latest_first_use(device_id)
        ruling = self._ruling_profile() or {}
        rest_s: float | None = None
        anchor: str | None = None
        gap = False
        if latest is not None and latest.get("occurredAtUtc"):
            rest_s, anchor, gap = self._rest(
                device_id,
                parse_instant(latest["occurredAtUtc"], "occurredAtUtc"),
                rule.measured_up_to_s,
            )
        wall = None
        if model is not None and latest is not None and latest.get("occurredAtUtc"):
            wall = self._relative_wall(
                device_id, parse_instant(latest["occurredAtUtc"], "occurredAtUtc"), anchor
            )
        inputs = FirstUseInputs(
            relative_wall_c=None if wall is None else wall["relativeWallC"],
            cold_start=bool(wall and wall.get("coldStart")),
            previous_feedforward_gain=(previous or {}).get("feedforwardGain"),
            rest_s=rest_s,
            telemetry_gap=gap,
            in_use=latest is not None and latest.get("state") in _USE_STATES,
            previous_offset_c=(previous or {}).get("offsetC"),
            suspended=self._active_suspension() is not None,
            excluded=bool(ruling.get("id")) and self._excluded(str(ruling["id"])),
            manual_offset_c=ruling.get("boilerOffsetC"),
        )
        decision = decide_first_use(inputs, rule, model)
        # Con el modelo, la subida no baja mientras no haya un café: subirla calienta
        # el metal y con él la pared, que haría pedir menos justo cuando ya se pidió
        # para ese café. El reposo solo enfría, así que lo pedido sigue valiendo.
        if (
            decision.reason is FirstUseReason.MODEL_WALL
            and previous is not None
            and previous.get("reason") == FirstUseReason.MODEL_WALL.value
            and previous.get("lastUseAt") == anchor
            and (previous.get("offsetC") or 0.0) > (decision.offset_c or 0.0)
        ):
            decision = FirstUseDecision(
                offset_c=previous["offsetC"], reason=decision.reason,
                rule_version=decision.rule_version, rest_s=decision.rest_s,
            )
        # El refuerzo tampoco se retira antes del café una vez pedido: la pared
        # solo se enfría en reposo, y el refuerzo que se pidió sigue haciendo
        # falta aunque la estimación oscile en el umbral.
        if (
            decision.reason is FirstUseReason.MODEL_NO_DEFICIT
            and previous is not None
            and previous.get("reason") == FirstUseReason.MODEL_FEEDFORWARD.value
            and previous.get("lastUseAt") == anchor
        ):
            decision = FirstUseDecision(
                offset_c=None, reason=FirstUseReason.MODEL_FEEDFORWARD,
                rule_version=decision.rule_version, rest_s=decision.rest_s,
                feedforward_gain=previous.get("feedforwardGain"),
            )
        # Se guarda cada cambio, no cada sincronización: con una por segundo
        # durante un café, guardar todas taparía las que importan.
        if previous is None or (
            previous.get("offsetC"), previous.get("reason"), previous.get("feedforwardGain")
        ) != (decision.offset_c, decision.reason.value, decision.feedforward_gain):
            identifier = f"first-use-{uuid4().hex[:12]}"
            self.repository.add_document(
                "first-use-decision",
                identifier,
                {
                    "id": identifier,
                    "deviceId": device_id,
                    "decidedAt": iso_now(),
                    "lastUseAt": anchor,
                    "rulingProfileId": ruling.get("id"),
                    "rule": rule.as_document(),
                    "model": None if model is None else model.as_document(),
                    "wall": wall,
                    **decision.as_document(),
                },
            )
        return decision

    def _relative_wall(
        self, device_id: str, now: Any, anchor: str | None
    ) -> dict[str, Any] | None:
        """Pared del gemelo ahora menos la de los tres últimos cafés al pulsar, o None.

        Se guarda un minuto por máquina y se recalcula antes si hubo un café
        nuevo. None si en las últimas 2 h no hay tres cafés medidos o si la
        telemetría tiene un hueco desde 30 min antes del primero de ellos: con un
        uso que la plataforma no vio, la pared saldría de una historia falsa.
        """
        # El minuto se cuenta con el reloj de la placa, no con el del servidor: una
        # tanda de telemetría atrasada que llega de golpe avanza la historia sin
        # que pase el tiempo del servidor.
        at = now.timestamp()
        cached = _wall_cache.get(device_id)
        if cached is not None and cached[1] == anchor and 0.0 <= at - cached[0] < _WALL_EVERY_S:
            return cached[2]
        result = self._compute_relative_wall(device_id, now)
        _wall_cache[device_id] = (at, anchor, result)
        return result

    def _compute_relative_wall(
        self, device_id: str, now: Any, model: WallFirstUseModel | None = WALL_FIRST_USE_V1
    ) -> dict[str, Any] | None:
        begin = now - _WALL_HISTORY
        history = self.service._twin_history(device_id, begin, now, now)
        metal = sorted(history["metal"])
        if len(metal) < 2:
            return None
        coffees = [use for use in history["uses"] if use.reference != "purgado"]
        now_s = now.timestamp() - history["origin"]
        if len(coffees) < 3:
            return self._cold_start_wall(history, metal, now_s, now, model)
        last_three = sorted(coffees, key=lambda use: use.start_s)[-3:]
        watched_from = max(metal[0][0], last_three[0].start_s - 1800.0)
        seen = [at for at, _ in metal if at >= watched_from]
        if not seen or any(
            later - earlier > _TELEMETRY_GAP_S for earlier, later in pairwise([*seen, now_s])
        ):
            return None
        # La última lectura llega hasta 15 s antes de ahora; el metal se sostiene
        # hasta ahora para que la pared se pueda leer en este instante.
        if metal[-1][0] < now_s:
            metal.append((now_s, metal[-1][1]))
        ambient = history["latestRoom"]
        assumed = None
        if ambient is None:
            ambient, assumed = assumed_room_c(now)
        walls = wall_temperatures(
            metal, history["uses"], float(ambient),
            [now_s, *(use.start_s for use in last_three)],
        )
        if any(value is None for value in walls):
            return None
        reference = fmean(float(v) for v in walls[1:])  # type: ignore[arg-type]
        return {
            "relativeWallC": round(float(walls[0]) - reference, 2),  # type: ignore[arg-type]
            "wallNowC": round(float(walls[0]), 2),  # type: ignore[arg-type]
            "wallReferenceC": round(reference, 2),
            "referenceUses": [use.reference for use in last_three],
            "ambientC": float(ambient),
            "ambientAssumed": assumed,
        }

    def _cold_start_wall(
        self, history: dict[str, Any], metal: list[tuple[float, float]], now_s: float,
        now: Any, model: WallFirstUseModel | None,
    ) -> dict[str, Any] | None:
        """La pared contra la típica de los cafés recuperados, tras encender en frío.

        Solo si tras el último hueco de la telemetría la primera lectura del metal
        está bajo 50 °C: la máquina se encendió fría dentro de la ventana, y la pared
        arranca en su equilibrio con ese metal, que es lo que es (`intento-14-v8.txt`).
        Con un hueco y el metal caliente no se sabe qué pasó en él, y decide la regla.
        """
        if model is None or model.cold_start_reference_wall_c is None:
            return None
        times = [at for at, _ in metal]
        starts = [0, *(k + 1 for k, (a, b) in enumerate(pairwise([*times, now_s]))
                       if b - a > _TELEMETRY_GAP_S)]
        start = starts[-1]
        if start >= len(metal) or metal[start][1] >= 50.0:
            return None
        since = metal[start:]
        uses = [use for use in history["uses"] if use.start_s >= since[0][0]]
        if since[-1][0] < now_s:
            since = [*since, (now_s, since[-1][1])]
        ambient = history["latestRoom"]
        assumed = None
        if ambient is None:
            ambient, assumed = assumed_room_c(now)
        (wall_now,) = wall_temperatures(since, uses, float(ambient), [now_s])
        if wall_now is None:
            return None
        reference = model.cold_start_reference_wall_c
        return {
            "relativeWallC": round(wall_now - reference, 2),
            "wallNowC": round(wall_now, 2),
            "wallReferenceC": reference,
            "referenceUses": [],
            "coldStart": True,
            "ambientC": float(ambient),
            "ambientAssumed": assumed,
        }

    def latest_first_use(self, device_id: str) -> dict[str, Any] | None:
        """La última decisión del primer uso guardada, sin decidir otra."""
        return self._latest_first_use(device_id)

    def _latest_first_use(self, device_id: str) -> dict[str, Any] | None:
        items = self.repository.list_documents(
            "first-use-decision", page_size=1, filters={"deviceId": device_id}
        ).items
        return items[0] if items else None

    def _rest(
        self, device_id: str, now: Any, window_s: float
    ) -> tuple[float | None, str | None, bool]:
        """El reposo, el último uso visto y si faltó telemetría en la espera.

        Solo se mira la ventana que la regla distingue: más allá de su último
        punto la subida ya no cambia, así que un uso anterior no la acortaría.
        Leer solo esa ventana usa el índice por dispositivo e instante; buscar
        el último uso en toda la telemetría archivada tardaba más de 3 s con
        160 000 muestras, y la placa corta la sincronización a los 2 s.

        El fin de un uso sale de dos sitios porque ninguno basta solo: el
        firmware de operación no envía telemetría durante el café (desde el
        19-oct solo llega el resumen, con su `finishedAt`), y un purgado no deja
        resumen, solo muestras en `manual-purge` si la telemetría corre.
        """
        start = now - timedelta(seconds=window_s)
        samples = sorted(
            (parse_instant(item["occurredAtUtc"], "occurredAtUtc"), item.get("state"))
            for item in self.repository.device_samples_between(
                device_id, utc_instant_text(start), utc_instant_text(now)
            )
            if item.get("state") and item.get("occurredAtUtc")
        )
        uses = [instant for instant, state in samples if state in _USE_STATES]
        # Solo una preparación medida dice algo del grupo: una simulada no pasó
        # agua por ninguna parte.
        uses += [
            parse_instant(item["finishedAt"], "finishedAt")
            for item in self.repository.list_documents(
                "extraction", page_size=1, filters={"source": "measured"}
            ).items
            if item.get("finishedAt")
        ]
        anchor = max(uses) if uses else None
        seen_from = start if anchor is None or anchor < start else anchor
        points = [seen_from, *(instant for instant, _ in samples if instant >= seen_from), now]
        gap = any(
            (later - earlier).total_seconds() > _TELEMETRY_GAP_S
            for earlier, later in pairwise(points)
        )
        if anchor is None:
            # Sin ningún uso archivado, una ventana entera vista sin uso basta
            # para saber que el reposo es al menos la ventana.
            return window_s, None, gap
        return max(0.0, (now - anchor).total_seconds()), utc_instant_text(anchor), gap

    def status(self) -> dict[str, Any]:
        return {
            "suspension": self._active_suspension(),
            "firstUseRule": FIRST_USE_RULE_V1.as_document(),
            "firstUse": (
                self.repository.list_documents("first-use-decision", page_size=1).items or [None]
            )[0],
        }


__all__ = ["SustainmentService"]
