from __future__ import annotations

import json
import time
from collections.abc import Callable
from contextlib import suppress
from copy import deepcopy
from datetime import UTC, datetime, timedelta
from hashlib import sha256
from math import sin
from statistics import fmean
from typing import Any
from uuid import uuid4

from espresso_profiles import profile_contract_view, profile_reference
from espresso_profiles.dispersion import tolerance_from_repetitions
from espresso_profiles.water_range import (
    CURRENT,
    CURRENT_METAL,
    WATER_MAXIMUM_C,
    WATER_MINIMUM_C,
    WaterLine,
    judged_specifications,
    line_of,
    measures_boiler,
    metal_line_of,
    water_temperature_accepted,
)
from espresso_semantics import simulation_jsonld
from espresso_semantics.extraction import adjustment_graph, extraction_graph, stable_iri
from espresso_semantics.reasoning import reason, technical_conformance
from espresso_simulation import FileSeriesStore, ModelRegistry, load_registered_controller
from espresso_simulation.water_after_rest import (
    DEFAULT_FLOW_KG_S,
    PURGE_FLOW_KG_S,
    Use,
    estimate_uses,
)
from espresso_simulation.water_after_rest import MODEL_VERSION as WATER_TWIN_VERSION
from espresso_simulation.water_temperature import PAIRING_MARGIN_MS, pair_with_body_sample
from espresso_stability.attribution import attribution, regime_pressure_bar
from espresso_stability.conformity import (
    bounds,
    not_evaluable,
    series_deviation,
    value_deviation,
)
from espresso_stability.indicators import Requested, stability
from espresso_stability.machine import (
    WATER_BAND_C,
    body_at_press,
    estimated_water_c,
    machine_stability,
    water_between_preparations,
)
from espresso_stability.repeatability import spread
from espresso_sustainment.first_use import FirstUseDecision
from rdflib import Graph

from aplicacion_web.adapters.reasoner import adjustment_conclusions, conformance_conclusions
from aplicacion_web.adapters.stubs import SemanticServiceUnavailable
from aplicacion_web.domain.errors import ConflictError, NotFoundError, ValidationError
from aplicacion_web.domain.models import (
    Identity,
    Page,
    extraction_order_instant,
    interrupted_by_power_loss,
    iso_now,
    parse_instant,
    require_text,
    utc_instant_text,
    utc_now,
    validate_envelope,
)
from aplicacion_web.semantic.export import validated_jsonld
from aplicacion_web.semantic.references import sensory_references

from .clock import clock_is_synchronised
from .observations import boiler_at_press, conformity_window, preparation_observations
from .ports import ExplanationPort, RecommendationPort, Repository, SemanticPort
from .reachability import answers_ping
from .revisions import apply_revisions, revisions_by_resource, revisions_of, with_revisions
from .room_temperature import assumed_room_c

#: Evaluaciones de conformidad guardadas, una por cambio.
CONFORMITY_KIND = "conformity-assessment"
#: Sin telemetría en este tiempo la señal se da por perdida. La placa envía una
#: por segundo y la sincroniza en tandas; medio minuto deja margen a una tanda
#: lenta sin mostrar como viva una máquina apagada.
MACHINE_SIGNAL_TIMEOUT_S = 30
#: Versión de EKO cuyo razonador decide la conformidad y cuyo vocabulario se
#: cita en las respuestas.
EKO_VERSION = "3.5.0"
#: Lo que dice cada veredicto del razonador, en las palabras de siempre.
CONFORMITY_EXPLANATIONS = {
    "non-conformant": (
        "Una observación fuera del intervalo configurado basta para indicar "
        "no conformidad técnica."
    ),
    "insufficient-information": (
        "Alguna propiedad del perfil no tiene observaciones, no declara tolerancia "
        "o no tiene el tramo en que debe juzgarse."
    ),
    "conformant": (
        "Todas las propiedades configuradas tienen observaciones y ninguna "
        "supera sus límites."
    ),
}

#: Cadencia nominal de la serie de una preparación, en milisegundos.
LIVE_SAMPLE_PERIOD_MS = 500

#: Tope de muestras por preparación, coherente con la capacidad del registrador
#: del dispositivo.
LIVE_SAMPLE_LIMIT = 1200

#: Unidades admitidas por canal en las especificaciones de un perfil.
PROFILE_UNITS: dict[str, set[str]] = {
    "temperatureC": {"Cel", "degC", "°C"},
    "pressureBar": {"bar"},
}

#: Lectura en lenguaje natural del veredicto de una comparación físico-virtual.
TRACE_VERDICTS = {
    "within-model-tolerance": "El modelo reprodujo la preparación dentro de la tolerancia",
    "outside-model-tolerance": "El modelo se apartó de lo medido más allá de la tolerancia",
    "insufficient-coverage": "Sin lecturas suficientes para juzgar el modelo",
    "not-evaluable": "Ningún canal pudo evaluarse",
}

#: Paso de comunicación FMI del modelo canónico, en segundos.
SIMULATION_STEP_SECONDS = 0.1

#: Parámetros con que cada versión del contrato FMU recibe el estado de la
#: preparación que se replica. La v1 separa núcleo y sensor; la v2 los arranca
#: juntos con `T_inicial` («metal, agua y sensor al empezar», FmuAdapterV2.mo).
#: Controlador con que se replica una preparación en un modelo cuyo canal de
#: temperatura es la caldera (FMU v2): el de la placa, porque comparar la serie
#: medida con una gobernada por otro control mezcla el error del modelo con el
#: de un controlador que no existe (sim-00000342: 7,9 °C y 1,5 bar de RMSE).
#: La 2.0.0 cita el acta de temperatura v12, la que corre la placa desde el
#: 28-oct; la 1.0.0 citaba la v7 (9-nov-2026). La recta y la ganancia de la
#: compensación salen además de cada preparación (`_replicated_extraction`).
BOARD_REPLICA_CONTROLLER = {"id": "board-replica", "version": "2.0.0"}
#: El de la planta virtual, para las simulaciones libres y los modelos que no
#: exponen la caldera (v1).
PROFILE_TRACKING_CONTROLLER = {"id": "profile-tracking", "version": "1.0.0"}

REPLICA_PARAMETERS: dict[str, dict[str, Any]] = {
    "1.0": {
        "initial": ("planta.caldera.T_inicialNucleo", "planta.caldera.T_inicialSensor"),
        "ambient": "planta.caldera.T_ambiente",
    },
    "2.0": {"initial": ("T_inicial",), "ambient": "T_ambiente"},
}

#: Desplazamiento entre grados Celsius y kelvin, unidad en que la FMU declara
#: sus parámetros de temperatura inicial.
ABSOLUTE_ZERO_OFFSET_C = 273.15


# Lo que el dispositivo necesita para gobernar la máquina. Sin cualquiera de las
# tres no puede conducir una preparación: no sabría a qué temperatura, a qué
# presión o hasta cuándo.
_GOVERNING_FIELDS = {
    "temperatureSetpointC": "la referencia de temperatura",
    "pressureSetpointBar": "la referencia de presión",
    "maximumTimeMs": "el tiempo máximo de extracción",
}


_ADJUSTED_SPECIFICATION_PROPERTIES = {
    "temperatureSetpointC": {"temperature", "water-temperature"},
    "pressureSetpointBar": {"pressure", "extraction-pressure"},
    "maximumTimeMs": {"extraction-time", "time"},
}


def _specifications_with_adjustment(
    specifications: list[dict[str, Any]], field: str, value: Any
) -> list[dict[str, Any]]:
    """Las especificaciones de la base con la magnitud ajustada al valor nuevo.

    El campo plano que lee el dispositivo y la especificación que lee la
    ontología describen la misma magnitud. Si solo cambiara el campo plano, el
    perfil derivado le diría a la máquina 20 s y a la ontología 25 s. Una
    especificación objetivo cambia su valor; un intervalo se desplaza con su
    anchura, porque el ajuste mueve la referencia y no la tolerancia.
    """
    properties = _ADJUSTED_SPECIFICATION_PROPERTIES[field]
    result = []
    for specification in specifications:
        updated = deepcopy(specification)
        if updated.get("property") in properties:
            if field == "maximumTimeMs":
                unit = str(updated.get("unit") or "").lower()
                updated["maximum"] = (
                    value / 1000 if unit in {"s", "seg", "segundos"} else value
                )
            elif updated.get("strategy") == "target":
                updated["target"] = value
            elif updated.get("minimum") is not None and updated.get("maximum") is not None:
                half = (float(updated["maximum"]) - float(updated["minimum"])) / 2
                updated["minimum"], updated["maximum"] = value - half, value + half
        result.append(updated)
    return result


def _require_governable(profile: dict[str, Any]) -> None:
    """Rechaza una configuración que el dispositivo no podría aplicar.

    No duplica la validación del dispositivo, que sigue siendo la autoridad
    sobre sus propios límites y conserva su derecho de veto (ADR-017 § 4.1).
    Comprueba otra cosa y en otro sitio: que la configuración esté **completa**,
    en el único momento en que el barista puede corregirlo, que es mientras la
    activa.

    Sin esto la activación se daba por buena, el dispositivo rechazaba la
    configuración con causa y esa causa aparecía solo en su consola de
    diagnóstico, que en la instalación definitiva nadie mira: el barista creía
    haber activado algo que nunca llegó a regir (ADR-017 § 4.5).
    """
    view = profile_contract_view(profile)
    missing = [label for field, label in _GOVERNING_FIELDS.items() if view.get(field) is None]
    if missing:
        raise ValidationError(
            "la configuración no puede gobernar la máquina: le falta "
            + ", ".join(missing)
        )


def _water_bound(body: Any, line: WaterLine = CURRENT_METAL) -> dict[str, Any] | None:
    """Si la caldera está fuera del rango de la recta, el lado y el borde del agua."""
    if not isinstance(body, int | float) or estimated_water_c(float(body), line) is not None:
        return None
    if body > line.boiler_maximum_c:
        return {"side": "above", "limitC": line.water_maximum_c}
    return {"side": "below", "limitC": line.water_minimum_c}


def _boiler_as_water(
    objective: dict[str, Any],
    press_c: float | None,
    deviation: dict[str, Any],
    offset: Any,
    line: WaterLine,
    metal_line: WaterLine,
) -> dict[str, Any]:
    """La caldera al pulsar dicha en agua sobre el café, para quien la lee.

    El barista pide el agua y la caldera no le dice nada (26-oct-2026). Es la
    misma comparación pasada por la recta con que la placa hizo ese café
    (`line_of`), sin la subida del primer uso: el veredicto no cambia, solo las
    unidades. El objetivo vuelve a ser el del agua que se pidió y la diferencia
    se convierte con la ganancia. El agua estimada desde el metal va con la
    recta del metal (`metal_line_of`): desde la v12 la de conversión lleva
    dentro el repunte de la caldera tras el café y daría el agua alta.
    """
    raise_c = float(offset) if isinstance(offset, int | float) else 0.0

    def water(boiler: Any) -> float | None:
        return round(line.gain * (float(boiler) - raise_c) + line.offset_c, 2) \
            if isinstance(boiler, int | float) else None

    tolerance = objective.get("tolerance")
    amount = deviation.get("amount")
    return {
        "method": line.method,
        "boilerOffsetC": raise_c,
        "objective": {
            "target": water(objective.get("target")),
            "tolerance": round(float(tolerance) * line.gain, 2)
            if isinstance(tolerance, int | float) else tolerance,
            "minimum": water(objective.get("minimum")),
            "maximum": water(objective.get("maximum")),
        },
        "estimatedC": estimated_water_c(press_c - raise_c, metal_line)
        if press_c is not None else None,
        "bound": _water_bound(press_c - raise_c, metal_line) if press_c is not None else None,
        "deviationAmountC": round(float(amount) * line.gain, 2)
        if isinstance(amount, int | float) else None,
    }


class ApplicationService:
    def __init__(
        self,
        repository: Repository,
        semantics: SemanticPort,
        recommender: RecommendationPort,
        explainer: ExplanationPort,
        model_registry: ModelRegistry | None = None,
        series_store: FileSeriesStore | None = None,
        simulation_max_duration_seconds: float = 900.0,
    ) -> None:
        self.repository = repository
        self.semantics = semantics
        self.recommender = recommender
        self.explainer = explainer
        self.model_registry = model_registry
        self.series_store = series_store
        self.simulation_max_duration_seconds = simulation_max_duration_seconds
        # Si la máquina contesta en la red cuando no manda lecturas: la prueba
        # se cambia en las pruebas y se repite a lo sumo cada pocos segundos,
        # porque cada pantalla abierta pregunta el estado cada 2 s.
        self.ping: Callable[[str], bool | None] = answers_ping
        self._reachability: tuple[str, float, bool | None] | None = None

    @property
    def sustainment(self) -> Any:
        from .sustainment import SustainmentService

        return SustainmentService(self)

    def simulation_models(self) -> list[dict[str, Any]]:
        if self.model_registry is None:
            return []
        return self.model_registry.public_models()

    @staticmethod
    def _simulation_profile_snapshot(profile: dict[str, Any]) -> dict[str, Any]:
        if profile.get("status") not in {"approved", "accepted", "effective"}:
            raise ValidationError("selecciona un perfil aprobado")
        aliases = {
            "temperature": "temperatureC",
            "water-temperature": "temperatureC",
            "pressure": "pressureBar",
            "extraction-pressure": "pressureBar",
        }
        expected_units = PROFILE_UNITS
        targets: dict[str, float] = {}
        tolerances: dict[str, float] = {}
        resolution: dict[str, str] = {}
        used: list[dict[str, Any]] = []
        for specification in profile.get("specifications", []):
            channel = aliases.get(specification.get("property"))
            if channel is None:
                continue
            if specification.get("unit") not in expected_units[channel]:
                raise ValidationError(f"unidad no compatible para {specification.get('property')}")
            if specification.get("strategy") == "target":
                targets[channel] = float(specification["target"])
                # Sin tolerancia declarada no se escribe ninguna: un cero
                # afirmaría un margen nulo que nadie decidió.
                if specification.get("tolerance") is not None:
                    tolerances[channel] = float(specification["tolerance"])
                method = "target-direct"
            elif specification.get("strategy") == "range":
                minimum = float(specification["minimum"])
                maximum = float(specification["maximum"])
                targets[channel] = (minimum + maximum) / 2
                tolerances[channel] = (maximum - minimum) / 2
                method = "range-midpoint"
            else:
                raise ValidationError("estrategia de perfil no soportada")
            resolution["temperature" if channel == "temperatureC" else "pressure"] = method
            used.append(deepcopy(specification))
        missing = {"temperatureC", "pressureBar"}.difference(targets)
        if missing:
            raise ValidationError(
                "el perfil no es simulable; faltan especificaciones de "
                + ", ".join(sorted(missing))
            )
        return {
            "schemaVersion": "1.0",
            "profileId": profile["id"],
            "profileVersion": int(profile["version"]),
            "profileStatus": profile["status"],
            "targets": targets,
            "tolerances": tolerances,
            "resolution": resolution,
            "sourceSpecifications": used,
        }

    def create_simulation(
        self, payload: dict[str, Any], identity: Identity, idempotency_key: str
    ) -> tuple[dict[str, Any], bool]:
        if self.model_registry is None:
            raise ValidationError("el registro de modelos no está configurado")
        if not idempotency_key or len(idempotency_key) > 120:
            raise ValidationError("Idempotency-Key es obligatoria y admite máximo 120 caracteres")
        model_ref = payload["model"]
        try:
            model = self.model_registry.get(model_ref["id"], model_ref["version"])
            model.verify()
        except KeyError as error:
            raise NotFoundError(str(error)) from error
        except (PermissionError, FileNotFoundError, ValueError) as error:
            raise ValidationError(str(error)) from error
        replicates = payload.get("replicates")
        replicated = self._replicated_extraction(replicates, model) if replicates else None
        if replicated is not None:
            duration = replicated["durationSeconds"]
        elif payload.get("durationSeconds") is not None:
            duration = float(payload["durationSeconds"])
        else:
            raise ValidationError("indica durationSeconds o la preparación que se replica")
        if duration > self.simulation_max_duration_seconds:
            raise ValidationError(
                f"durationSeconds no puede superar {self.simulation_max_duration_seconds:g}"
            )
        if replicated is not None:
            snapshot = replicated["profileSnapshot"]
        else:
            if not payload.get("profileId"):
                raise ValidationError("indica profileId o la preparación que se replica")
            snapshot = self._simulation_profile_snapshot(self.get("profile", payload["profileId"]))
        try:
            controller = load_registered_controller(
                model.root, payload["controller"]["id"], payload["controller"]["version"]
            )
        except (KeyError, ValueError) as error:
            raise ValidationError("controlador de simulación no registrado") from error
        if controller["controllerId"] == BOARD_REPLICA_CONTROLLER["id"]:
            # La réplica de la placa arranca en el pulsado con la extracción en
            # curso y regula el metal: sin preparación que replicar no tiene
            # estado de partida, y sin canal de caldera no tiene qué regular.
            if replicated is None:
                raise ValidationError("board-replica solo replica una preparación")
            if model.channel_quantity("temperatureC") != "boiler-body-temperature":
                raise ValidationError(
                    "board-replica exige un modelo cuyo temperatureC sea la caldera"
                )
        step = float(model.data["capabilities"]["communicationStepSeconds"])
        total_steps = round(duration / step)
        if total_steps < 1 or abs(total_steps * step - duration) > 1e-9:
            raise ValidationError(f"durationSeconds debe ser múltiplo de {step:g}")
        normalized = json.dumps(payload, sort_keys=True, separators=(",", ":"))
        created_at = iso_now()
        document = {
            "schemaVersion": "1.0",
            "simulationRunId": f"sim-{uuid4().hex[:16]}",
            "status": "queued",
            "semanticStatus": "pending",
            "progress": {"completedSteps": 0, "totalSteps": total_steps, "percent": 0.0},
            "requestedBy": identity.subject,
            "createdAt": created_at,
            "startedAt": None,
            "finishedAt": None,
            "model": {
                "id": model.identifier,
                "version": model.version,
                "sha256": model.artifact["sha256"],
                "contractVersion": model.data["contract"]["version"],
                "channels": deepcopy(model.data["capabilities"]["channels"]),
            },
            "profileSnapshot": snapshot,
            "controller": {
                "id": payload["controller"]["id"],
                "version": payload["controller"]["version"],
                "configurationSha256": controller["configurationSha256"],
                # Las actas de las que salen los números del control replicado,
                # para que la comparación diga contra qué puesta en servicio se hizo.
                **(
                    {
                        "commissioning": [
                            controller["configuration"][loop]["identifier"]
                            for loop in ("temperature", "pressure")
                        ]
                    }
                    if controller["controllerId"] == BOARD_REPLICA_CONTROLLER["id"]
                    else {}
                ),
            },
            "durationSeconds": duration,
            "initialConditions": replicated["initialConditions"] if replicated else None,
            "replicates": replicated["replicates"] if replicated else None,
            "contractVersion": "1.0",
            "summary": None,
            "artifact": None,
            "cancelRequested": False,
            "error": None,
        }
        return self.repository.create_simulation(
            document, idempotency_key, sha256(normalized.encode()).hexdigest()
        )

    def _replicated_extraction(self, replicates: dict[str, Any], model: Any) -> dict[str, Any]:
        """Deriva de una preparación cerrada las condiciones con las que replicarla.

        El perfil se toma de la instantánea conservada en la propia preparación y
        no del perfil vigente: una simulación que reproduce el pasado debe usar la
        configuración que rigió entonces, aunque el perfil se haya editado después.
        """
        extraction_id = require_text(replicates.get("extractionId"), "extractionId", 80)
        extraction = self.get("extraction", extraction_id)
        if interrupted_by_power_loss(extraction):
            raise ValidationError(
                "una preparación interrumpida por un corte de energía no puede replicarse: "
                "no tiene serie ni duración"
            )
        # La que abre la plataforma lleva `status`; la que llega de la placa no,
        # porque solo se archiva al terminar (`extraction.completed`) y su final
        # consta en `finishedAt`. Exigir `status` dejaba sin réplica todas las
        # preparaciones reales.
        finished = extraction.get("status") == "closed" or (
            extraction.get("status") is None and bool(extraction.get("finishedAt"))
        )
        if not finished:
            raise ValidationError("solo puede replicarse una preparación finalizada")
        duration_ms = int(extraction.get("durationMs") or 0)
        if duration_ms <= 0:
            raise ValidationError("la preparación no tiene duración registrada")
        step = SIMULATION_STEP_SECONDS
        duration = max(step, round(duration_ms / 1000 / step) * step)
        samples = self._series_samples(extraction_id)
        valid = [
            sample
            for sample in samples
            if sample.get("quality") == "valid" and sample.get("temperatureC") is not None
        ]
        if not valid:
            raise ValidationError("la preparación no tiene mediciones válidas para replicarla")
        first = valid[0]
        ambient = (extraction.get("environment") or {}).get("temperatureC")
        channels = {
            "temperatureC": float(first["temperatureC"]),
            "pressureBar": float(first.get("pressureBar") or 0.0),
        }
        kelvin = channels["temperatureC"] + ABSOLUTE_ZERO_OFFSET_C
        names = REPLICA_PARAMETERS.get(str(model.data["contract"]["version"]))
        if names is None:
            raise ValidationError(
                "el contrato de este modelo no declara cómo fijar su estado inicial"
            )
        parameters = {name: kelvin for name in names["initial"]}
        if isinstance(ambient, int | float):
            parameters[names["ambient"]] = float(ambient) + ABSOLUTE_ZERO_OFFSET_C
        # Con qué se preparó: el modelo que lo admite se carga con la curva y el
        # agua retenida de esa configuración (FMU 2.1, agua-retenida-2.1.md). Si
        # la preparación no lo declara, rige el valor del modelo (café) y la
        # ejecución lo dice: replicar agua sola como café enfría la mitad.
        configuration: dict[str, Any] | None = None
        option = (model.data["capabilities"].get("replicaParameters") or {}).get(
            "portafilterConfiguration"
        )
        if option:
            declared = extraction.get("portafilterConfiguration")
            # Declarada pero sin curva en el modelo (la canastilla no
            # presurizada): replicarla con la del café presurizado sería
            # presentar como réplica una carga hidráulica que no fue.
            if declared is not None and declared not in option["values"]:
                raise ValidationError(
                    "el modelo no tiene la carga hidráulica de esta configuración "
                    f"({declared}); la preparación no puede replicarse con él"
                )
            value = declared if declared in option["values"] else option["modelDefault"]
            parameters[option["parameter"]] = float(option["values"][value])
            configuration = {
                "value": value,
                "source": "declared" if declared in option["values"] else "model-default",
            }
        # Lo que el control de la placa necesita de la preparación además de la
        # medida: el ambiente con que se estima la potencia de sostenimiento y la
        # subida del primer uso, que la placa suma a la referencia de caldera.
        # Sin declarar, ninguno se supone: el primero cae al del acta y el
        # segundo a cero, que es lo que la placa aplica sin petición.
        board_state: dict[str, Any] = {}
        if isinstance(ambient, int | float):
            board_state["ambientC"] = float(ambient)
        offset = extraction.get("boilerOffsetAppliedC")
        if isinstance(offset, int | float):
            board_state["boilerOffsetAppliedC"] = float(offset)
        # La ganancia de la compensación durante la extracción con que la placa
        # hizo el café: la subida del primer uso la cambia para esa preparación.
        gain = extraction.get("firstUseFeedforwardGainApplied")
        if isinstance(gain, int | float) and not isinstance(gain, bool):
            board_state["feedforwardGainApplied"] = float(gain)
        # La recta con que la placa hizo el café, para que la réplica busque la
        # misma caldera; sin recta que conste, la réplica no tiene referencia.
        line = line_of(extraction)
        board_state["waterReference"] = None if line is None else {
            "plant": line.method, "gain": line.gain, "offsetC": line.offset_c,
            "boilerMinimumC": line.boiler_minimum_c, "boilerMaximumC": line.boiler_maximum_c,
        }
        profile = deepcopy(extraction.get("profile") or {})
        if not profile.get("id"):
            raise ValidationError("la preparación no conserva el perfil con el que se ejecutó")
        return {
            "durationSeconds": duration,
            "profileSnapshot": self._simulation_profile_snapshot(
                self.get("profile", str(profile["id"]))
            ),
            "initialConditions": {
                "channels": channels,
                "fmuParameters": parameters,
                "unit": "K",
                "source": f"extraction:{extraction_id}",
                "portafilterConfiguration": configuration,
                **board_state,
            },
            "replicates": {
                "extractionId": extraction_id,
                "modelTolerances": {
                    key: value
                    for key, value in (replicates.get("modelTolerances") or {}).items()
                    if value is not None
                },
                "minimumCoverageRatio": replicates.get("minimumCoverageRatio"),
            },
        }

    def request_twin_comparison(
        self, extraction_id: str, payload: dict[str, Any], identity: Identity, idempotency_key: str
    ) -> tuple[dict[str, Any], bool]:
        """Encola la simulación que replica una preparación para poder compararla."""
        extraction = self.get("extraction", extraction_id)
        profile = extraction.get("profile") or {}
        model = payload.get("model")
        if not model:
            canonical = (
                self.model_registry.canonical("espresso-detailed") if self.model_registry else None
            )
            if canonical is None:
                raise ValidationError("no hay una versión vigente del modelo para replicar")
            model = {"id": canonical.identifier, "version": canonical.version}
        controller = payload.get("controller")
        if not controller:
            controller = deepcopy(PROFILE_TRACKING_CONTROLLER)
            registered = self._registered_model(model)
            if (
                registered is not None
                and registered.channel_quantity("temperatureC") == "boiler-body-temperature"
            ):
                controller = deepcopy(BOARD_REPLICA_CONTROLLER)
        request = {
            "schemaVersion": "1.0",
            "model": model,
            "profileId": profile.get("id"),
            "controller": controller,
            "replicates": {
                "extractionId": extraction_id,
                "modelTolerances": payload.get("modelTolerances") or {},
                "minimumCoverageRatio": payload.get("minimumCoverageRatio"),
            },
        }
        return self.create_simulation(request, identity, idempotency_key)

    def _registered_model(self, reference: dict[str, Any]) -> Any:
        """El modelo pedido, o None si no está registrado: lo rechaza `create_simulation`."""
        if self.model_registry is None:
            return None
        try:
            return self.model_registry.get(reference["id"], reference["version"])
        except (KeyError, PermissionError):
            return None

    def twin_comparisons(self, extraction_id: str) -> dict[str, Any]:
        """Comparaciones de una preparación, de la más reciente a la más antigua."""
        self.get("extraction", extraction_id)
        items = self.repository.list_documents(
            "twin-comparison", page=1, page_size=100, filters={"extractionId": extraction_id}
        ).items
        ordered = sorted(items, key=lambda item: item.get("createdAt") or "", reverse=True)
        return {
            "extractionId": extraction_id,
            "latest": ordered[0] if ordered else None,
            "items": ordered,
            "total": len(ordered),
        }

    def simulation(self, identifier: str) -> dict[str, Any]:
        result = self.repository.get_simulation(identifier)
        if result is None:
            raise NotFoundError(f"simulación {identifier} no encontrada")
        return result

    def simulations(self, page: int, page_size: int) -> dict[str, Any]:
        return self.repository.list_simulations(page, page_size).as_dict()

    def cancel_simulation(self, identifier: str, identity: Identity) -> dict[str, Any]:
        current = self.simulation(identifier)
        if current["requestedBy"] != identity.subject and not identity.can_admin():
            raise ValidationError("solo el solicitante o administración puede cancelar")
        if current["status"] in {"succeeded", "failed", "cancelled"}:
            raise ConflictError("la simulación ya finalizó")
        result = self.repository.request_simulation_cancellation(identifier)
        if result is None:
            raise NotFoundError(f"simulación {identifier} no encontrada")
        if result["status"] == "running":
            result = dict(result)
            result["cancelRequested"] = True
        return result

    def simulation_series(self, identifier: str) -> dict[str, Any]:
        self.simulation(identifier)
        manifest = self.repository.get_simulation_artifact(identifier)
        if manifest is None or self.series_store is None:
            raise NotFoundError("la serie de simulación todavía no está disponible")
        return {"manifest": manifest, "samples": self.series_store.read(manifest)}

    def simulation_semantic(self, identifier: str) -> dict[str, Any]:
        run = self.simulation(identifier)
        publication = self.repository.get_semantic_publication(identifier)
        if publication is None:
            return {
                "simulationRunId": identifier,
                "status": run["semanticStatus"],
                "publication": None,
                "document": None,
            }
        document = simulation_jsonld(run) if publication["status"] == "published" else None
        return {
            "simulationRunId": identifier,
            "status": publication["status"],
            "publication": publication,
            "document": document,
        }

    def rebuild_semantics(self, identifier: str | None = None) -> dict[str, Any]:
        if identifier is not None:
            self.simulation(identifier)
        count = self.repository.request_semantic_rebuild(identifier)
        return {"status": "queued", "requested": count, "simulationRunId": identifier}

    def list(
        self,
        kind: str,
        page: int,
        page_size: int,
        query: str | None = None,
        filters: dict[str, str] | None = None,
    ) -> dict[str, Any]:
        result = self.repository.list_documents(
            kind, page=page, page_size=page_size, query=query, filters=filters
        ).as_dict()
        if kind in {"extraction", "profile"}:
            result["items"] = [self._project_resource(kind, item) for item in result["items"]]
        return result

    def visible_profiles(
        self, identity: Identity, shared_profile_id: str, page: int, page_size: int
    ) -> dict[str, Any]:
        """Los perfiles que puede ver quien pregunta.

        Un barista ve los suyos, el perfil común y el que rige en la máquina; la
        administración y la consulta, todos. El que rige se ve aunque sea de
        otro barista: la máquina es una, y Preparar café lo necesita para decir
        con qué saldría el café. Ver no es usar: quien lo vea y cambie algo crea
        una versión suya.

        Solo filtra la lista. Un perfil se sigue pudiendo leer por su
        identificador, porque los cafés de otros baristas lo citan; esconderlo
        ahí dejaría el historial y las comparaciones con huecos.
        """
        if identity.role != "barista":
            return self.list("profile", page, page_size)
        everything: list[dict[str, Any]] = []
        current = 1
        while True:
            batch = self.list("profile", current, 500)
            everything.extend(batch["items"])
            if current >= batch["pages"]:
                break
            current += 1
        mine = [
            item
            for item in everything
            if (item.get("provenance") or {}).get("actor") == identity.subject
            or item.get("id") == shared_profile_id
            or item.get("status") == "accepted"
        ]
        start = (page - 1) * page_size
        return Page(mine[start : start + page_size], page, page_size, len(mine)).as_dict()

    def _revisions(self, kind: str, identifier: str) -> list[dict[str, Any]]:
        return revisions_of(self.repository, kind, identifier)

    def _project_resource(self, kind: str, document: dict[str, Any]) -> dict[str, Any]:
        return with_revisions(self.repository, kind, document)

    def _append_revision(
        self,
        kind: str,
        identifier: str,
        category: str,
        changes: dict[str, Any],
        identity: Identity,
        idempotency_key: str | None = None,
    ) -> dict[str, Any]:
        self.get(kind, identifier)
        revisions = self._revisions(kind, identifier)
        suffix = (
            sha256(idempotency_key.encode()).hexdigest()[:20]
            if idempotency_key
            else uuid4().hex[:20]
        )
        revision_id = f"{kind}-revision-{suffix}"
        existing = self.repository.get_document(f"{kind}-revision", revision_id)
        if existing is not None:
            if existing.get("resourceId") != identifier or existing.get("category") != category:
                raise ConflictError("la clave de reintento ya pertenece a otra acción")
            return self.get(kind, identifier)
        revision = {
            "id": revision_id,
            "resourceId": identifier,
            "category": category,
            "previousRecordId": revisions[-1]["id"] if revisions else identifier,
            "recordedBy": identity.subject,
            "recordedAt": iso_now(),
            "changes": deepcopy(changes),
        }
        self.repository.add_document(f"{kind}-revision", revision_id, revision)
        return self.get(kind, identifier)

    def extraction_history(
        self,
        page: int,
        page_size: int,
        query: str | None,
        filters: dict[str, str],
    ) -> dict[str, Any]:
        """Return the user-facing history with joined, immutable context."""
        if not query and not filters:
            result = self.repository.list_documents("extraction", page=page, page_size=page_size)
            items = [
                self._with_history_context(self._project_resource("extraction", item))
                for item in result.items
            ]
            items.sort(key=extraction_order_instant, reverse=True)
            return {
                "items": items,
                "page": page,
                "pageSize": page_size,
                "total": result.total,
                "pages": max(1, (result.total + page_size - 1) // page_size),
            }

        raw_items: list[dict[str, Any]] = []
        repository_page = 1
        while True:
            batch = self.repository.list_documents(
                "extraction", page=repository_page, page_size=250
            )
            raw_items.extend(batch.items)
            batch_pages = max(1, (batch.total + batch.page_size - 1) // batch.page_size)
            if repository_page >= batch_pages:
                break
            repository_page += 1
        # Lo que cada preparación consultaba por su cuenta —sus revisiones, su
        # café y su última evaluación— se carga una vez para todas: con 297
        # eran casi 1 200 consultas por cada página filtrada del historial.
        revisions = revisions_by_resource(self.repository, "extraction")
        coffees = {
            str(item["id"]): item
            for item in self.repository.list_documents("coffee", page=1, page_size=100_000).items
        }
        assessments: dict[str, dict[str, Any]] = {}
        for item in self.repository.list_documents(
            CONFORMITY_KIND, page=1, page_size=1_000_000
        ).items:
            key = str(item.get("extractionId"))
            latest = int((assessments.get(key) or {}).get("sequence") or 0)
            if key not in assessments or int(item.get("sequence") or 0) > latest:
                assessments[key] = item
        items = [apply_revisions(item, revisions.get(str(item["id"]), [])) for item in raw_items]
        enriched = [
            self._with_history_context(item, coffees=coffees, assessments=assessments)
            for item in items
        ]
        if query:
            needle = query.casefold()
            enriched = [item for item in enriched if needle in str(item).casefold()]

        def matches(item: dict[str, Any]) -> bool:
            history = item["history"]
            ordered_at = extraction_order_instant(item)
            # Una preparación sin final cae en el intervalo por el mismo instante
            # que la ordena; sin ninguno, no cae en ninguno: incluirla afirmaría
            # una fecha que no consta.
            if not ordered_at and {"finishedAtFrom", "finishedAtBefore"} & filters.keys():
                return False
            finished_at = parse_instant(ordered_at, "finishedAt") if ordered_at else None
            for field, expected in filters.items():
                # Un filtro de exclusión, no de igualdad: el historial del
                # barista deja fuera las pasadas de agua archivadas.
                if field == "purposeNot":
                    if item.get("purpose") == expected:
                        return False
                    continue
                if field == "finishedAtFrom" and finished_at < parse_instant(expected, field):
                    return False
                if field == "finishedAtBefore" and finished_at > parse_instant(expected, field):
                    return False
                if field in {"finishedAtFrom", "finishedAtBefore"}:
                    continue
                actual = history.get(field)
                if field == "defect":
                    if expected.casefold() not in {str(value).casefold() for value in actual or []}:
                        return False
                elif str(actual or "").casefold() != expected.casefold():
                    return False
            return True

        enriched = [item for item in enriched if matches(item)]
        enriched.sort(key=extraction_order_instant, reverse=True)
        total = len(enriched)
        start = (page - 1) * page_size
        return {
            "items": enriched[start : start + page_size],
            "page": page,
            "pageSize": page_size,
            "total": total,
            "pages": max(1, (total + page_size - 1) // page_size),
        }

    def _with_history_context(
        self,
        extraction: dict[str, Any],
        *,
        coffees: dict[str, dict[str, Any]] | None = None,
        assessments: dict[str, dict[str, Any]] | None = None,
    ) -> dict[str, Any]:
        """La preparación con los campos por los que se filtra el historial.

        `coffees` y `assessments`, si llegan, son lo ya cargado para un listado;
        lo que no esté en ellos se busca como siempre.
        """
        item = self._with_manual_completion(deepcopy(extraction))
        coffee = item.get("coffeeSnapshot")
        if coffee is None and item.get("coffeeId"):
            coffee_id = str(item["coffeeId"])
            coffee = (coffees or {}).get(coffee_id) or self.repository.get_document(
                "coffee", coffee_id
            )
        # Solo los dos veredictos que muestra el historial: el resultado entero
        # trae además la comparación con el gemelo y la estabilidad de la
        # máquina, que aquí no se ven y multiplicaban el tiempo por preparación.
        technical = (
            "insufficient-information"
            if interrupted_by_power_loss(item)
            else (
                (assessments or {}).get(str(item["id"])) or self._stored_conformity(item)
            )["technicalConformance"]["status"]
        )
        sensory = item.get("sensoryEvaluation") or {}
        item["history"] = {
            "barista": item.get("barista"),
            "coffeeId": item.get("coffeeId"),
            "variety": (coffee or {}).get("variety"),
            "process": (coffee or {}).get("postHarvestProcess"),
            "roast": (coffee or {}).get("roastLevel"),
            "profileId": (item.get("profile") or {}).get("id"),
            "profileVersion": (item.get("profile") or {}).get("version"),
            "machineId": item.get("machineId") or item.get("deviceId"),
            "grinderId": item.get("grinderId"),
            "calibrationId": item.get("calibrationId"),
            "technicalConformance": technical,
            "sensoryAcceptance": (sensory.get("acceptance") or {}).get("status", "not-recorded"),
            "defect": sensory.get("defects") or [],
            "termination": item.get("termination"),
            "source": item.get("source"),
        }
        return item

    def claimable_extractions(self, subject: str, limit: int) -> list[dict[str, Any]]:
        """Los cafés medidos más recientes sin barista o del barista que prepara.

        Lee solo la primera página sin filtros, como lo hacía Preparar café antes
        de que el historial se limitara al barista, y filtra aquí: es lo que
        se consulta cada pocos segundos mientras se espera un café.
        """
        recent = self.extraction_history(1, limit, None, {})["items"]
        return [
            item
            for item in recent
            if item.get("source") == "measured" and item.get("barista") in (None, "", subject)
            and item.get("purpose") != "water-pass"
        ]

    def extraction_history_record(self, extraction_id: str) -> dict[str, Any]:
        extraction = self._with_history_context(self.get("extraction", extraction_id))
        episodes = self.repository.list_documents("episode", page=1, page_size=1000).items
        linked_episodes = [
            episode
            for episode in episodes
            if extraction_id
            in {episode.get("initialExtractionId"), episode.get("followUpExtractionId")}
        ]
        related_ids = sorted(
            {
                str(identifier)
                for episode in linked_episodes
                for identifier in (
                    episode.get("initialExtractionId"),
                    episode.get("followUpExtractionId"),
                )
                if identifier and identifier != extraction_id
            }
        )
        recommendations = self.repository.list_documents(
            "recommendation", page=1, page_size=1000, filters={"extractionId": extraction_id}
        ).items
        calibrations = self.repository.list_documents("calibration", page=1, page_size=1000).items
        linked_calibrations = [
            calibration
            for calibration in calibrations
            if extraction_id in calibration.get("testExtractionIds", [])
            or calibration.get("id") == extraction.get("calibrationId")
        ]
        coffee = extraction.get("coffeeSnapshot")
        if coffee is None and extraction.get("coffeeId"):
            coffee = self.repository.get_document("coffee", str(extraction["coffeeId"]))
        return {
            "extraction": extraction,
            "coffee": coffee,
            "result": self.extraction_result(extraction_id),
            "series": self.extraction_series(extraction_id),
            "recommendations": recommendations,
            "episodes": linked_episodes,
            "calibrations": linked_calibrations,
            "relatedExtractionIds": related_ids,
        }

    def semantic_extraction(self, extraction_id: str) -> dict[str, Any]:
        extraction = self.get("extraction", extraction_id)
        profile_id = str((extraction.get("profile") or {}).get("id") or "")
        try:
            profile = self.get("profile", profile_id)
        except NotFoundError:
            raise ValidationError(
                "la preparación no tiene un perfil semántico disponible"
            ) from None
        return validated_jsonld(
            extraction, profile, sensory_references(self.repository, extraction)
        )

    def get(self, kind: str, identifier: str) -> dict[str, Any]:
        item = self.repository.get_document(kind, identifier)
        if item is None:
            raise NotFoundError(f"{kind} {identifier} no existe")
        if kind in {"extraction", "profile"}:
            item = self._project_resource(kind, item)
        if kind == "recommendation":
            decision = self.repository.get_document("recommendation-decision", identifier)
            if decision is not None:
                item = {**item, "decision": decision}
        if kind == "extraction":
            item = self._with_manual_completion(item)
        return item

    def dashboard(self) -> dict[str, Any]:
        extractions = [
            self._project_resource("extraction", item)
            for item in self.repository.list_documents("extraction", page_size=100).items
        ]
        ordered = sorted(extractions, key=extraction_order_instant, reverse=True)
        fallback = ordered[0] if ordered else None
        contextual = next((item for item in ordered if item.get("coffeeId")), fallback)
        active_profile = deepcopy(contextual.get("profile")) if contextual else None
        if active_profile and not active_profile.get("name"):
            identifier = str(active_profile.get("id") or "Perfil de preparación")
            active_profile["name"] = identifier.replace("-", " ").capitalize()
        coffee = None
        if contextual and contextual.get("coffeeId"):
            coffee = self.repository.get_document("coffee", str(contextual["coffeeId"]))

        calibrations = self.repository.list_documents("calibration", page_size=1).items
        pending: list[dict[str, str]] = []
        for extraction in ordered:
            extraction_id = str(extraction.get("id", ""))
            reference = str(extraction.get("coffeeReference") or "Preparación sin café")
            if not extraction.get("coffeeId"):
                pending.append(
                    {
                        "kind": "coffee",
                        "label": f"Asociar café a {reference}",
                        "extractionId": extraction_id,
                    }
                )
            manual = extraction.get("manualMeasurements") or {}
            if any(
                manual.get(field) is None
                for field in (
                    "coffeeDoseG",
                    "beverageMassG",
                    "tdsPercent",
                )
            ):
                pending.append(
                    {
                        "kind": "measurements",
                        "label": f"Completar mediciones de {reference}",
                        "extractionId": extraction_id,
                    }
                )
            if not extraction.get("sensoryEvaluation"):
                pending.append(
                    {
                        "kind": "sensory",
                        "label": f"Evaluar {reference}",
                        "extractionId": extraction_id,
                    }
                )
            if len(pending) >= (4 if not calibrations else 5):
                break
        if not calibrations:
            pending.append(
                {
                    "kind": "calibration",
                    "label": "Realizar la primera calibración",
                    "extractionId": "",
                }
            )

        telemetry = self.repository.latest_telemetry() or {}
        # ``quality`` resume la muestra completa para el comparador; la calidad de
        # cada canal, que es la que interesa al panel, vive en ``channelQuality``.
        quality = telemetry.get("channelQuality") or {}
        measurements = telemetry.get("measurements") or {}
        sensors = [
            {
                "id": "temperature",
                "label": "Temperatura",
                "status": "available"
                if quality.get("temperature") in {"valid", "simulated"}
                and measurements.get("temperatureC") is not None
                else "unavailable",
                "value": measurements.get("temperatureC"),
                "unit": "°C",
            },
            {
                "id": "pressure",
                "label": "Presión",
                "status": "available"
                if quality.get("pressure") in {"valid", "simulated"}
                and measurements.get("pressureBar") is not None
                else "unavailable",
                "value": measurements.get("pressureBar"),
                "unit": "bar",
            },
        ]
        return {
            "activeProfile": active_profile,
            "activeCoffee": coffee,
            "activeEquipment": {
                "machineId": contextual.get("machineId") if contextual else None,
                "grinderId": contextual.get("grinderId") if contextual else None,
                "calibrationId": contextual.get("calibrationId") if contextual else None,
            },
            "sensors": sensors,
            "lastReadingAt": telemetry.get("occurredAt") if telemetry else None,
            # De qué máquina es la última lectura: lo necesita quien registra la
            # temperatura del agua para emparejarla con su cuerpo (ADR-019).
            "lastReadingDeviceId": telemetry.get("deviceId") if telemetry else None,
            "lastCalibration": calibrations[0] if calibrations else None,
            "pendingActions": pending[:5],
            "recentExtractions": ordered[:3],
        }

    def create(self, kind: str, payload: dict[str, Any], identity: Identity) -> dict[str, Any]:
        document = deepcopy(payload)
        if kind == "coffee":
            identifier_labels = {
                "commercialReference": "referencia comercial",
                "lotIdentifier": "lote",
                "bagIdentifier": "bolsa",
            }
            for field in ("commercialReference", "lotIdentifier", "bagIdentifier"):
                value = document.get(field)
                if value and self.repository.coffee_identifier_exists(field, value):
                    raise ConflictError(f"ya existe un café con esa {identifier_labels[field]}")
            document["id"] = document.get("id") or f"coffee-{uuid4().hex[:12]}"
        identifier = require_text(document.get("id"), "id", 80)
        document.setdefault("createdAt", iso_now())
        document["provenance"] = {
            "actor": identity.subject,
            "recordedAt": iso_now(),
            "source": "manual",
        }
        return self.repository.add_document(kind, identifier, document)

    @staticmethod
    def _validate_specification_units(specifications: list[dict[str, Any]]) -> None:
        """Rechaza unidades no reconocidas al crear el perfil.

        La comprobación existía solo aguas abajo, al preparar una simulación, de
        modo que un perfil con una unidad inválida podía aprobarse y usarse en
        una preparación para resultar insimulable después.
        """
        accepted = {
            "temperature": PROFILE_UNITS["temperatureC"],
            "water-temperature": PROFILE_UNITS["temperatureC"],
            "pressure": PROFILE_UNITS["pressureBar"],
            "extraction-pressure": PROFILE_UNITS["pressureBar"],
        }
        for specification in specifications or []:
            expected = accepted.get(specification.get("property"))
            if expected is None:
                continue
            if specification.get("unit") not in expected:
                raise ValidationError(
                    f"unidad no compatible para {specification['property']}; "
                    f"se admite {' o '.join(sorted(expected))}"
                )

    @staticmethod
    def _validate_water_temperature(specifications: list[dict[str, Any]]) -> None:
        """Rechaza, al escribir el perfil, un agua que la placa no aceptaría.

        La placa sigue siendo la autoridad y vuelve a comprobarlo al activar
        (ADR-017 § 4.1); esto solo adelanta el aviso al momento en que el
        barista escribe el número, en vez de enterarse al activar.
        """
        water, _ = profile_reference({"specifications": specifications}, "temperature")
        if water is not None and not water_temperature_accepted(water):
            raise ValidationError(
                f"la temperatura del agua debe estar entre {WATER_MINIMUM_C} y "
                f"{WATER_MAXIMUM_C} °C, que es lo que la máquina alcanza (acta v12)"
            )

    def create_profile(self, payload: dict[str, Any], identity: Identity) -> dict[str, Any]:
        self._validate_profile_coffees(payload["coffeeIds"])
        self._validate_specification_units(payload.get("specifications") or [])
        self._validate_water_temperature(payload.get("specifications") or [])
        requested_id = payload.pop("id", None)
        family_id = requested_id or f"profile-{uuid4().hex[:12]}"
        identifier = requested_id or f"{family_id}-v1"
        document = {
            **deepcopy(payload),
            "id": identifier,
            "familyId": family_id,
            "version": 1,
            "status": "draft",
            "previousVersionId": None,
            "createdAt": iso_now(),
            "provenance": {
                "actor": identity.subject,
                "recordedAt": iso_now(),
                "source": "manual",
            },
        }
        return self.repository.add_document("profile", identifier, document)

    def compare_profiles(self, identifiers: list[str]) -> dict[str, Any]:
        if not 2 <= len(identifiers) <= 4 or len(set(identifiers)) != len(identifiers):
            raise ValidationError("selecciona entre dos y cuatro perfiles distintos")
        profiles = [self.get("profile", identifier) for identifier in identifiers]
        coffees = {
            item["id"]: item
            for item in self.repository.list_documents("coffee", page=1, page_size=1000).items
        }
        calibrations = self.repository.list_documents("calibration", page=1, page_size=1000).items
        extractions = [
            self._project_resource("extraction", item)
            for item in self.repository.list_documents("extraction", page=1, page_size=1000).items
        ]

        def coffee_summary(coffee_id: str) -> dict[str, Any]:
            coffee = coffees.get(coffee_id) or {"id": coffee_id}
            return {
                "id": coffee_id,
                "name": coffee.get("commercialReference")
                or coffee.get("lotIdentifier")
                or coffee.get("bagIdentifier")
                or coffee_id,
            }

        items: list[dict[str, Any]] = []
        for profile in profiles:
            profile_id = str(profile["id"])
            validation = [
                {
                    "id": calibration["id"],
                    "result": calibration.get("result"),
                    "rationale": calibration.get("rationale"),
                    "finishedAt": calibration.get("finishedAt"),
                    "testExtractionIds": calibration.get("testExtractionIds") or [],
                }
                for calibration in calibrations
                if profile_id in {calibration.get("profileId"), calibration.get("resultProfileId")}
            ]
            supporting = [
                {
                    "id": extraction["id"],
                    "finishedAt": extraction.get("finishedAt"),
                    "coffeeReference": extraction.get("coffeeReference"),
                }
                for extraction in extractions
                if (extraction.get("profile") or {}).get("id") == profile_id
            ]
            items.append(
                {
                    **deepcopy(profile),
                    "author": (profile.get("provenance") or {}).get("actor")
                    or profile.get("updatedBy"),
                    "applicableCoffees": [
                        coffee_summary(str(coffee_id))
                        for coffee_id in profile.get("coffeeIds") or []
                    ],
                    "validationEvidence": validation,
                    "supportingExtractions": supporting,
                }
            )

        def comparison(
            key: str, label: str, values: list[Any], unit: str | None = None
        ) -> dict[str, Any]:
            present = [value for value in values if value is not None]
            if len(present) != len(values):
                comparison_status = "missing"
            elif all(value == present[0] for value in present):
                comparison_status = "equal"
            else:
                comparison_status = "different"
            return {
                "key": key,
                "label": label,
                "unit": unit,
                "values": values,
                "status": comparison_status,
            }

        changes = [
            comparison("name", "Nombre", [profile.get("name") for profile in profiles]),
            comparison("status", "Estado", [profile.get("status") for profile in profiles]),
            comparison(
                "coffeeIds",
                "Cafés aplicables",
                [sorted(profile.get("coffeeIds") or []) for profile in profiles],
            ),
        ]
        properties = sorted(
            {
                str(specification.get("property"))
                for profile in profiles
                for specification in profile.get("specifications") or []
            }
        )
        for property_name in properties:
            specifications = [
                next(
                    (
                        specification
                        for specification in profile.get("specifications") or []
                        if specification.get("property") == property_name
                    ),
                    None,
                )
                for profile in profiles
            ]
            unit = next(
                (
                    str(specification.get("unit"))
                    for specification in specifications
                    if specification
                ),
                None,
            )
            for field, label in (
                ("target", "Objetivo"),
                ("tolerance", "Tolerancia"),
                ("minimum", "Mínimo"),
                ("maximum", "Máximo"),
            ):
                values = [
                    specification.get(field) if specification else None
                    for specification in specifications
                ]
                if any(value is not None for value in values):
                    changes.append(
                        comparison(
                            f"{property_name}.{field}",
                            f"{property_name} · {label}",
                            values,
                            unit,
                        )
                    )
        changes.extend(
            [
                comparison(
                    "sensoryTarget.descriptors",
                    "Descriptores deseados",
                    [
                        sorted((profile.get("sensoryTarget") or {}).get("descriptors") or [])
                        for profile in profiles
                    ],
                ),
                comparison(
                    "sensoryTarget.desiredChanges",
                    "Cambios sensoriales deseados",
                    [
                        (profile.get("sensoryTarget") or {}).get("desiredChanges")
                        for profile in profiles
                    ],
                ),
            ]
        )
        return {
            "items": items,
            "changes": changes,
            "changedKeys": [item["key"] for item in changes if item["status"] != "equal"],
        }

    def update_profile(
        self, profile_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        profile = self.get("profile", profile_id)
        if profile.get("status") != "draft":
            raise ConflictError("solo se puede editar un perfil en borrador")
        self._validate_profile_coffees(payload["coffeeIds"])
        self._validate_specification_units(payload.get("specifications") or [])
        self._validate_water_temperature(payload.get("specifications") or [])
        target = profile.get("sensoryTarget")
        if target and target.get("coffeeId") not in payload["coffeeIds"]:
            raise ValidationError("el café del objetivo sensorial debe seguir asociado al perfil")
        updated = {
            **profile,
            **deepcopy(payload),
            "updatedAt": iso_now(),
            "updatedBy": identity.subject,
        }
        return self.repository.upsert_document("profile", profile_id, updated)

    def create_profile_version(
        self, profile_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        self._validate_specification_units(payload.get("specifications") or [])
        self._validate_water_temperature(payload.get("specifications") or [])
        previous = self.get("profile", profile_id)
        if previous.get("status") not in {"approved", "accepted", "effective", "retired"}:
            raise ConflictError("la nueva versión debe partir de un perfil aprobado o retirado")
        self._validate_profile_coffees(payload["coffeeIds"])
        family_id = str(previous.get("familyId") or previous["id"])
        family = self.repository.list_documents(
            "profile", page=1, page_size=1000, filters={"familyId": family_id}
        ).items
        versions = [int(item.get("version", 0)) for item in family]
        version = max(versions + [int(previous.get("version", 1))]) + 1
        identifier = f"{family_id}-v{version}"
        document = {
            **deepcopy(payload),
            "id": identifier,
            "familyId": family_id,
            "version": version,
            "status": "draft",
            "previousVersionId": profile_id,
            "createdAt": iso_now(),
            "provenance": {
                "actor": identity.subject,
                "recordedAt": iso_now(),
                "source": "manual",
            },
        }
        previous_target = previous.get("sensoryTarget")
        if previous_target and previous_target.get("coffeeId") in payload["coffeeIds"]:
            document["sensoryTarget"] = {
                **deepcopy(previous_target),
                "id": f"target-{identifier}",
                "inheritedFrom": previous_target.get("id"),
                "definedBy": identity.subject,
                "recordedAt": iso_now(),
            }
        return self.repository.add_document("profile", identifier, document)

    def approve_profile(self, profile_id: str, identity: Identity) -> dict[str, Any]:
        profile = self.get("profile", profile_id)
        if profile.get("status") != "draft":
            raise ConflictError("solo se puede aprobar un perfil en borrador")
        approved = {
            **profile,
            "status": "approved",
            "approvedAt": iso_now(),
            "approvedBy": identity.subject,
        }
        return self.repository.upsert_document("profile", profile_id, approved)

    def activate_profile(
        self,
        profile_id: str,
        identity: Identity,
        payload: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Pone una configuración a regir en la máquina.

        Cubre las tres vías de ADR-017 § 4.1 —selección, edición y recomendación
        aceptada—, que producen el mismo resultado y se distinguen por su
        procedencia. No inicia ninguna preparación —eso sigue siendo del mando
        físico— y no es una orden: el dispositivo la valida contra sus límites y
        puede rechazarla con causa.
        """
        payload = payload or {}
        via = payload.get("via") or "selection"
        if via not in {"selection", "edition", "recommendation"}:
            raise ValidationError(
                "la vía de activación debe ser selection, edition o recommendation"
            )

        profile = self.get("profile", profile_id)
        if profile.get("status") not in {"approved", "accepted", "effective"}:
            raise ConflictError("solo puede regir una configuración aprobada")
        _require_governable(profile)

        # Una activación por recomendación se comprueba, no se declara: sin una
        # decisión registrada detrás, la cadena de autoridad quedaría rota
        # justamente en el eslabón que ADR-004 § 4.11 exige conservar.
        recommendation_id = payload.get("recommendationId")
        decision_id = None
        if via == "recommendation":
            if not recommendation_id:
                raise ValidationError("una activación por recomendación debe identificarla")
            self.get("recommendation", str(recommendation_id))
            decision = self.repository.get_document(
                "recommendation-decision", str(recommendation_id)
            )
            if not decision or decision.get("status") not in {"accepted", "modified"}:
                raise ValidationError(
                    "la recomendación debe haber sido aceptada o modificada por el barista"
                )
            decision_id = decision.get("id") or recommendation_id
        elif recommendation_id:
            raise ValidationError(
                "solo una activación por recomendación puede declarar una recomendación"
            )

        # Rige la última aceptada (ADR-017 § 4.3). Las anteriores vuelven a estar
        # disponibles sin regir, de modo que en un instante dado solo una baja al
        # dispositivo y nunca hay duda sobre cuál gobierna la máquina.
        for vigente in self.repository.list_documents(
            "profile", page_size=64, filters={"status": "accepted"}
        ).items:
            if vigente.get("id") == profile_id:
                continue
            self.repository.upsert_document(
                "profile", str(vigente["id"]), {**vigente, "status": "approved"}
            )

        activated = {
            **profile,
            "status": "accepted",
            # La procedencia distingue las tres vías: sin ella, una configuración
            # vigente sería indistinguible de otra y no podría reconstruirse por
            # qué la máquina estaba ajustada así (ADR-017 § 4.2).
            "activation": {
                "via": via,
                "activatedBy": identity.subject,
                "activatedAt": iso_now(),
                "recommendationId": recommendation_id,
                "decisionId": decision_id,
            },
        }
        return self.repository.upsert_document("profile", profile_id, activated)

    def tolerance_from_extractions(
        self,
        profile_id: str,
        extraction_ids: list[str],
        identity: Identity,
        apply: bool = False,
    ) -> dict[str, Any]:
        """La tolerancia que dan los cafés repetidos con un perfil.

        Sin `apply` solo propone. Con él crea la versión, la aprueba y la pone
        a regir, que es el camino de «Guardar y usar en la máquina»: la receta
        de la catación se fija justo antes de seguir preparando con ella.
        Solo valen cafés medidos y hechos con este mismo perfil; una simulación
        o un café de otra versión no dicen cuánto varía esta receta.
        """
        profile = self.get("profile", profile_id)
        if profile.get("status") not in {"approved", "accepted", "effective"}:
            raise ConflictError("la tolerancia se calcula sobre un perfil aprobado")
        identifiers = list(dict.fromkeys(extraction_ids))
        if len(identifiers) < 2:
            raise ValidationError("hacen falta al menos dos cafés distintos")
        values: dict[str, list[float | None]] = {
            "coffee-dose": [], "beverage-mass": [], "extraction-time": [], "tds": []
        }
        for identifier in identifiers:
            extraction = self.get("extraction", identifier)
            if (extraction.get("profile") or {}).get("id") != profile_id:
                raise ValidationError(f"el café {identifier} no se preparó con este perfil")
            if extraction.get("source") != "measured":
                raise ValidationError(f"el café {identifier} no es una medición")
            manual = extraction.get("manualMeasurements") or {}
            duration = extraction.get("durationMs")
            values["coffee-dose"].append(manual.get("coffeeDoseG"))
            values["beverage-mass"].append(manual.get("beverageMassG"))
            values["extraction-time"].append(
                None if duration is None else float(duration) / 1000
            )
            values["tds"].append(manual.get("tdsPercent"))
        proposal = tolerance_from_repetitions(profile.get("specifications") or [], values)
        result: dict[str, Any] = {
            "profileId": profile_id,
            "extractionIds": identifiers,
            **proposal,
        }
        if not apply:
            return result
        # Una versión que no cambia nada solo ensuciaría la trazabilidad.
        if not any(item["status"] in {"updated", "added"} for item in proposal["details"]):
            raise ValidationError("a esos cafés les faltan los valores: no hay nada que fijar")
        payload: dict[str, Any] = {
            "name": profile["name"],
            "coffeeIds": profile.get("coffeeIds") or [],
            "specifications": proposal["specifications"],
        }
        if profile.get("purgeMaximumTimeMs") is not None:
            payload["purgeMaximumTimeMs"] = profile["purgeMaximumTimeMs"]
        version = self.create_profile_version(profile_id, payload, identity)
        # De dónde salió la banda queda en la versión: sin esto, una tolerancia
        # calculada sería indistinguible de una escrita a mano.
        version = self.repository.upsert_document(
            "profile",
            str(version["id"]),
            {
                **version,
                "toleranceDerivation": {
                    "method": "maximum-deviation-from-objective",
                    "extractionIds": identifiers,
                    "details": proposal["details"],
                    "computedBy": identity.subject,
                    "computedAt": iso_now(),
                },
            },
        )
        self.approve_profile(str(version["id"]), identity)
        result["profile"] = self.activate_profile(
            str(version["id"]), identity, {"via": "edition"}
        )
        return result

    def retire_profile(self, profile_id: str, identity: Identity) -> dict[str, Any]:
        profile = self.get("profile", profile_id)
        if profile.get("status") not in {"approved", "accepted", "effective"}:
            raise ConflictError("solo se puede retirar un perfil aprobado")
        retired = {
            **profile,
            "status": "retired",
            "retiredAt": iso_now(),
            "retiredBy": identity.subject,
        }
        return self._append_revision(
            "profile",
            profile_id,
            "profile-retired",
            {
                "status": retired["status"],
                "retiredAt": retired["retiredAt"],
                "retiredBy": retired["retiredBy"],
            },
            identity,
        )

    def _validate_profile_coffees(self, coffee_ids: list[str]) -> None:
        for coffee_id in coffee_ids:
            self.get("coffee", coffee_id)

    # Los campos del café que se pueden corregir después de registrarlo.
    _COFFEE_FIELDS = (
        "commercialReference", "lotIdentifier", "bagIdentifier", "variety",
        "postHarvestProcess", "roastLevel", "roastDate", "region", "altitudeM",
        "soilCharacteristics", "mineralComposition", "mineralSource", "notApplicableFields",
    )

    def update_coffee(
        self, coffee_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        """Corrige los datos de un café ya registrado.

        Las preparaciones hechas guardan su propia copia del café, así que una
        corrección no cambia lo que se registró entonces. Lo que había antes
        queda en `corrections`: sin eso, un dato corregido sería
        indistinguible de uno que siempre fue así.
        """
        coffee = self.get("coffee", coffee_id)
        def current(field: str) -> Any:
            # Un café registrado sin la lista guarda null: es la lista vacía.
            value = coffee.get(field)
            return (value or []) if field == "notApplicableFields" else value

        changed = {
            field: current(field)
            for field in self._COFFEE_FIELDS
            if current(field) != payload.get(field)
        }
        if not changed:
            return coffee
        updated = {
            **coffee,
            **{field: deepcopy(payload.get(field)) for field in self._COFFEE_FIELDS},
            "corrections": [
                *(coffee.get("corrections") or []),
                {"correctedBy": identity.subject, "correctedAt": iso_now(),
                 "previous": changed},
            ],
        }
        return self.repository.upsert_document("coffee", coffee_id, updated)

    def set_declared_sensory_profile(
        self, coffee_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        coffee = self.get("coffee", coffee_id)
        existing = coffee.get("declaredSensoryProfile") or {}
        coffee["declaredSensoryProfile"] = {
            **deepcopy(payload),
            "id": existing.get("id") or f"declared-{uuid4().hex[:12]}",
            "recordedBy": identity.subject,
            "recordedAt": iso_now(),
        }
        return self.repository.upsert_document("coffee", coffee_id, coffee)

    def set_profile_sensory_target(
        self, profile_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        profile = self.get("profile", profile_id)
        if profile.get("status") != "draft":
            raise ConflictError("el objetivo sensorial solo puede editarse en un borrador")
        coffee_id = payload["coffeeId"]
        if coffee_id not in profile.get("coffeeIds", []):
            raise ValidationError("el objetivo sensorial debe usar un café aplicable al perfil")
        profile["sensoryTarget"] = {
            **deepcopy(payload),
            "id": profile.get("sensoryTarget", {}).get("id") or f"target-{profile_id}",
            "definedBy": identity.subject,
            "recordedAt": iso_now(),
        }
        return self.repository.upsert_document("profile", profile_id, profile)

    def sync(self, payload: dict[str, Any], source_address: str | None = None) -> dict[str, Any]:
        self._remember_machine_address(source_address)
        messages = payload.get("messages")
        if not isinstance(messages, list) or len(messages) > 64:
            raise ValidationError("messages debe ser una lista de máximo 64 elementos")
        inserted = 0
        acknowledged: list[str] = []
        completed: list[str] = []
        series_received: list[str] = []
        for raw in messages:
            message = validate_envelope(raw)
            acknowledged.append(message["messageId"])
            fresh = self.repository.ingest_message(message)
            inserted += int(fresh)
            if fresh and message.get("messageType") == "extraction.completed":
                completed.append(str((message.get("payload") or {}).get("id")))
            if fresh and message.get("messageType") == "extraction.series":
                series_received.append(str((message.get("payload") or {}).get("preparationId")))
        # Después de archivar, nunca antes: la evaluación y la decisión se apoyan
        # en lo guardado. La serie puede llegar después del resumen, y con ella
        # cambia lo que se puede juzgar; por eso también evalúa.
        for extraction_id in completed:
            self._record_conformity_after(extraction_id, "extraction-synced")
        for extraction_id in dict.fromkeys(series_received):
            self._record_conformity_after(extraction_id, "series-received")
        # La respuesta baja únicamente las configuraciones que el barista aceptó:
        # devolver el catálogo entero convertiría cada sincronización en un envío
        # de perfiles que nadie propuso. El dispositivo las valida antes de
        # admitirlas y puede rechazarlas con causa (ADR-001 § 6).
        first_use = self._first_use_for(str(payload.get("deviceId") or ""))
        proposals = []
        for item in self.repository.list_documents(
            "profile", page_size=8, filters={"status": "accepted"}
        ).items:
            # El catálogo describe las referencias como especificaciones y el
            # dispositivo solo sabe leer campos planos: la traducción vive en un
            # único sitio para que no existan dos definiciones de lo mismo.
            view = profile_contract_view(item)
            # La subida del primer uso viaja con la configuración que rige, sin
            # crear una versión nueva del perfil por cada tramo de reposo. La
            # placa la cuenta como petición nueva al cambiar el valor y la
            # devuelve a cero al terminar el café (ADR-009, enmienda del
            # 2026-10-19). La que declare el perfil a mano no se toca.
            if first_use is not None and first_use.offset_c is not None and (
                "boilerOffsetC" not in view
            ):
                view["boilerOffsetC"] = first_use.offset_c
            # El refuerzo de la prealimentación viaja igual (ADR-009, enmienda
            # del 2026-10-27); la placa lo juzga contra el máximo del acta.
            if first_use is not None and first_use.feedforward_gain is not None and (
                "firstUseFeedforwardGain" not in view
            ):
                view["firstUseFeedforwardGain"] = first_use.feedforward_gain
            proposals.append(view)
        response: dict[str, Any] = {
            "schemaVersion": "1.0",
            "acknowledged": acknowledged,
            "inserted": inserted,
            "duplicates": len(acknowledged) - inserted,
            "profileProposals": proposals,
        }
        # El dispositivo no tiene reloj con respaldo: esta marca le sirve de
        # origen de hora cuando no dispone de otro (ADR-013 § 4.4). Solo con el
        # reloj de la plataforma sincronizado; si no, se omite y el dispositivo
        # la vuelve a pedir (clock.clock_is_synchronised).
        if clock_is_synchronised():
            response["serverTime"] = iso_now()
        return response

    def _first_use_for(self, device_id: str) -> FirstUseDecision | None:
        """La decisión del primer uso que rige ahora, o None si ninguna.

        Igual que el sostenimiento, un fallo aquí no puede impedir que la
        sincronización archive lo que trajo: queda en la auditoría y no se pide
        nada, que la placa lee como cero.
        """
        if not device_id:
            return None
        try:
            return self.sustainment.first_use(device_id)
        except Exception as error:  # noqa: BLE001
            self.repository.audit(
                "system:sustainment", "sustainment.first-use", f"device:{device_id}",
                f"failed: {type(error).__name__}: {error}"[:200], device_id,
            )
            return None

    def start_live_extraction(self, payload: dict[str, Any], identity: Identity) -> dict[str, Any]:
        coffee_id = require_text(payload.get("coffeeId"), "coffeeId", 80)
        profile_id = require_text(payload.get("profileId"), "profileId", 80)
        coffee = self.get("coffee", coffee_id)
        profile = self.get("profile", profile_id)
        if profile.get("status") not in {"approved", "accepted", "effective"}:
            raise ValidationError("selecciona un perfil aprobado")
        if coffee_id not in profile.get("coffeeIds", []):
            raise ValidationError("el perfil seleccionado no aplica al café")
        started_at = iso_now()
        identifier = f"extraction-{uuid4().hex[:12]}"
        snapshot = self._profile_snapshot(profile)
        document = {
            "id": identifier,
            "status": "running",
            "source": "simulation",
            "sourceLabel": "Datos simulados para pruebas",
            "deviceId": "live-simulator-v1",
            "profile": snapshot,
            "coffeeId": coffee_id,
            "coffeeSnapshot": deepcopy(coffee),
            "coffeeReference": coffee.get("commercialReference")
            or coffee.get("lotIdentifier")
            or coffee.get("bagIdentifier"),
            "barista": identity.subject,
            "startedAt": started_at,
            "lastSampleAt": None,
            "sampleCount": 0,
            "seriesRef": f"series://{identifier}",
            "environment": {
                "temperatureC": payload.get("ambientTemperatureC"),
                "humidityPercent": payload.get("ambientHumidityPercent"),
            },
            "machineId": payload.get("machineId"),
            "grinderId": payload.get("grinderId"),
            "calibrationId": payload.get("calibrationId"),
            "missingIntervals": [],
            "provenance": {
                "actor": identity.subject,
                "recordedAt": started_at,
                "source": "simulation",
            },
        }
        return self.repository.add_document("live-extraction", identifier, document)

    def extraction_alert_preferences(self, identity: Identity) -> dict[str, Any]:
        stored = self.repository.get_document("extraction-alert-preference", identity.subject)
        if stored:
            return stored
        return {
            "temperature": True,
            "pressure": True,
            "acquisition": True,
            "updatedBy": identity.subject,
            "updatedAt": None,
        }

    def update_extraction_alert_preferences(
        self, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        document = {
            "id": identity.subject,
            "temperature": bool(payload["temperature"]),
            "pressure": bool(payload["pressure"]),
            "acquisition": bool(payload["acquisition"]),
            "updatedBy": identity.subject,
            "updatedAt": iso_now(),
        }
        return self.repository.upsert_document(
            "extraction-alert-preference", identity.subject, document
        )

    def live_extraction(
        self, extraction_id: str, identity: Identity, elapsed_ms: int | None = None
    ) -> dict[str, Any]:
        """Estado de una preparación en curso, con las muestras que corresponden al tiempo.

        El número de muestras es función del tiempo transcurrido desde el inicio,
        no de cuántas veces se consulte esta vista: dos clientes abiertos ven lo
        mismo y no duplican la cadencia. `elapsed_ms` permite fijar ese tiempo en
        las pruebas.
        """
        live = self.get("live-extraction", extraction_id)
        if live.get("status") == "running":
            if elapsed_ms is None:
                started = parse_instant(live["startedAt"], "startedAt")
                elapsed_ms = max(
                    0, int((datetime.now(UTC) - started).total_seconds() * 1000)
                )
            expected = min(
                LIVE_SAMPLE_LIMIT, elapsed_ms // LIVE_SAMPLE_PERIOD_MS + 1
            )
            produced = False
            while int(live.get("sampleCount", 0)) < expected:
                self._append_simulated_sample(live)
                produced = True
            if produced:
                self.repository.upsert_document("live-extraction", extraction_id, live)
        samples = self._series_samples(extraction_id)
        preferences = self.extraction_alert_preferences(identity)
        return {
            **live,
            "samples": samples,
            "observations": self._live_observations(live, samples, preferences),
        }

    def extraction_series(self, extraction_id: str) -> dict[str, Any]:
        if self.repository.get_document("live-extraction", extraction_id) is None:
            self.get("extraction", extraction_id)
        return {
            "extractionId": extraction_id,
            "seriesRef": f"series://{extraction_id}",
            "samples": self._series_samples(extraction_id),
        }

    def extraction_result(self, extraction_id: str) -> dict[str, Any]:
        extraction = self.get("extraction", extraction_id)
        if interrupted_by_power_loss(extraction):
            return self._interrupted_result(extraction)
        assessment = self._stored_conformity(extraction)
        # La caldera al pulsar se lee en agua. Se añade al leer y no al guardar:
        # las evaluaciones guardadas son la evidencia de lo que se juzgó
        # y las anteriores al 26-oct-2026 también tienen que verse así.
        # Sin recta que conste no se traduce: no se sabe qué agua daba esa caldera.
        line, metal_line = line_of(extraction), metal_line_of(extraction)
        properties = [
            {**item, "label": "Temperatura media estimada del agua",
             "water": _boiler_as_water(item["objective"], (item.get("observed") or {}).get("value"),
                                       item["deviation"], extraction.get("boilerOffsetAppliedC"),
                                       line, metal_line)}
            if item.get("property") == "boiler-temperature" and measures_boiler(extraction)
            and line is not None and metal_line is not None
            else item
            for item in assessment["properties"]
        ]
        recommendations = self.repository.list_documents(
            "recommendation",
            page=1,
            page_size=100,
            filters={"extractionId": extraction_id},
        ).items
        sensory = extraction.get("sensoryEvaluation", {}).get("acceptance")
        evidence = [
            {
                "id": item["evidenceId"],
                "label": item["label"],
                "value": item["observed"],
                "source": item["source"],
            }
            for item in properties
            if item["observed"] is not None
        ]
        comparisons = self.twin_comparisons(extraction_id)
        return {
            "extractionId": extraction_id,
            "digitalTwin": comparisons["latest"],
            "machineStability": self._cached_machine_stability(extraction),
            "assessment": {
                "id": assessment.get("id"),
                "sequence": assessment.get("sequence"),
                "assessedAt": assessment.get("assessedAt"),
                "trigger": assessment.get("trigger"),
                "rule": assessment["rule"],
                "reasoning": assessment.get("reasoning"),
                "window": assessment["window"],
                "provenance": assessment["provenance"],
                "recorded": assessment.get("recorded", True),
            },
            "technicalConformance": assessment["technicalConformance"],
            "sensoryAcceptance": sensory or {"status": "not-recorded", "rationale": None},
            "properties": properties,
            "diagnosis": {
                "conclusion": assessment["conclusion"],
                "explanation": assessment["explanation"],
                "evidenceIds": [item["id"] for item in evidence],
                "knowledgeSource": {
                    "name": "Criterio de conformidad del perfil",
                    "version": assessment["rule"],
                },
                # Las evaluaciones anteriores al razonador no traen `reasoning`:
                # las hizo la regla de la plataforma y así se dice.
                "generatedBy": {
                    "name": "Razonador de EKO"
                    if assessment.get("reasoning")
                    else "Evaluador de conformidad",
                    "version": assessment["rule"],
                },
                "generatedAt": assessment.get("assessedAt") or extraction.get("finishedAt"),
            },
            "evidence": evidence,
            "recommendations": [
                {
                    "recommendationId": item["recommendationId"],
                    "status": item["status"],
                    "summary": item["summary"],
                    "createdAt": item["createdAt"],
                }
                for item in recommendations
            ],
        }

    def _interrupted_result(self, extraction: dict[str, Any]) -> dict[str, Any]:
        """Resultado de una preparación que un corte de energía dejó abierta.

        Conserva la forma de cualquier resultado para que la pantalla no tenga
        que adivinar, pero no evalúa nada: no hay serie, ni indicadores, ni
        duración. La conformidad queda «sin información suficiente», que es lo
        que significa, y la conclusión dice por qué.
        """
        extraction_id = str(extraction["id"])
        comparisons = self.twin_comparisons(extraction_id)
        sensory = extraction.get("sensoryEvaluation", {}).get("acceptance")
        profile = extraction.get("profile") or {}
        conclusion = "La preparación se interrumpió por un corte de energía."
        explanation = (
            "El dispositivo la cerró al volver a arrancar y no vio el final: no hay "
            "serie, indicadores ni duración con que evaluarla."
        )
        return {
            "extractionId": extraction_id,
            "digitalTwin": comparisons["latest"],
            "assessment": {
                "id": None,
                "sequence": None,
                "assessedAt": None,
                "trigger": None,
                "rule": "power-loss-not-evaluable",
                "window": (extraction.get("statistics") or {}).get("window"),
                "provenance": {
                    "extractionSource": extraction.get("source"),
                    "profileId": profile.get("id"),
                    "profileVersion": profile.get("version"),
                    "calibration": extraction.get("calibration"),
                    "firmwareVersion": extraction.get("firmwareVersion"),
                    "seriesSampleCount": 0,
                    "manualMeasurementsRecordedAt": None,
                },
                "recorded": False,
            },
            "technicalConformance": {
                "status": "insufficient-information",
                "profileId": profile.get("id"),
                "profileVersion": profile.get("version"),
                "assessedProperties": 0,
                "totalProperties": 0,
                "missingData": [],
            },
            "sensoryAcceptance": sensory or {"status": "not-recorded", "rationale": None},
            "properties": [],
            "diagnosis": {
                "conclusion": conclusion,
                "explanation": explanation,
                "evidenceIds": [],
                "knowledgeSource": {
                    "name": "Criterio de conformidad del perfil",
                    "version": "power-loss-not-evaluable",
                },
                "generatedBy": {
                    "name": "Evaluador de conformidad",
                    "version": "power-loss-not-evaluable",
                },
                "generatedAt": None,
            },
            "evidence": [],
            "recommendations": [],
        }

    def conformity_assessments(self, extraction_id: str) -> list[dict[str, Any]]:
        """Evaluaciones guardadas de una preparación, de la más antigua a la última."""
        items = self.repository.list_documents(
            CONFORMITY_KIND, page=1, page_size=1000, filters={"extractionId": extraction_id}
        ).items
        return sorted(items, key=lambda item: int(item.get("sequence") or 0))

    def _latest_conformity(self, extraction_id: str) -> dict[str, Any] | None:
        items = self.conformity_assessments(extraction_id)
        return items[-1] if items else None

    def record_conformity(self, extraction_id: str, trigger: str) -> dict[str, Any] | None:
        """Guarda la evaluación de la preparación si cambió.

        Una evaluación nueva **no sustituye** a la anterior: se añade con el
        número siguiente, para que se vea qué se concluía antes de, por ejemplo,
        registrar el TDS. Si el resultado es idéntico al último guardado no se
        repite: el registro existe para conservar cambios, no consultas.
        """
        stored = self.repository.get_document("extraction", extraction_id)
        if stored is None:
            # La serie puede llegar antes que el resumen: se evaluará con él.
            return None
        if interrupted_by_power_loss(stored):
            # Un corte de energía no deja nada que juzgar. Guardar una evaluación
            # «sin información suficiente» la pondría al lado de las demás como
            # si se hubiera intentado, cuando no hay de qué.
            return None
        # Con sus revisiones aplicadas: las medidas a mano viven en ellas.
        evaluated = self._evaluate_conformity(self.get("extraction", extraction_id))
        latest = self._latest_conformity(extraction_id)
        keys = ("rule", "window", "technicalConformance", "properties")
        if latest is not None and all(latest.get(key) == evaluated[key] for key in keys):
            # La misma evaluación, guardada antes de que llevara su grafo de
            # conclusiones (EKO 3.5.0): se le añade para que se publique, sin
            # contarla como evaluación nueva.
            if not latest.get("conclusionGraph") and evaluated.get("conclusionGraph"):
                latest = self.repository.upsert_document(CONFORMITY_KIND, str(latest["id"]), {
                    **latest,
                    "conclusionGraph": evaluated["conclusionGraph"],
                    "conclusionValidation": evaluated["conclusionValidation"],
                })
            return latest
        sequence = int((latest or {}).get("sequence") or 0) + 1
        identifier = f"{extraction_id}:{sequence:04d}"
        document = {
            "id": identifier,
            "extractionId": extraction_id,
            "sequence": sequence,
            "assessedAt": iso_now(),
            "trigger": trigger,
            "recorded": True,
            **evaluated,
        }
        self.repository.add_document(CONFORMITY_KIND, identifier, document)
        return document

    def _stored_conformity(self, extraction: dict[str, Any]) -> dict[str, Any]:
        """La última evaluación guardada; si no hay, se evalúa y se guarda.

        Las preparaciones anteriores a que se guardaran las evaluaciones se
        evaluaban al vuelo en cada consulta, y el historial filtrado las pasaba
        todas por el razonador cada vez (8 de sus 12 s con 119 de ellas,
        19-oct-2026). Se guardan la primera vez que se consultan, con el
        disparador «reconstruction» para que conste que no se hicieron al
        cerrar. El veredicto es el mismo que se calculaba: misma regla, mismo
        razonador.

        Lo que no se puede guardar —una preparación cortada por la energía, que
        no se juzga— sigue calculándose al vuelo y marcado como no registrado.
        """
        extraction_id = str(extraction["id"])
        assessment = self._latest_conformity(extraction_id)
        if assessment is not None:
            return assessment
        try:
            assessment = self.record_conformity(extraction_id, "reconstruction")
        except ConflictError:
            # Otra consulta la guardó a la vez: vale la suya.
            assessment = self._latest_conformity(extraction_id)
        return assessment or {**self._evaluate_conformity(extraction), "recorded": False}

    def _record_conformity_after(self, extraction_id: str, trigger: str) -> None:
        """Un fallo aquí no puede impedir archivar la preparación: va a la auditoría."""
        try:
            self.record_conformity(extraction_id, trigger)
        except Exception as error:  # noqa: BLE001
            self.repository.audit(
                "system:conformity", "conformity.record", f"extraction:{extraction_id}",
                f"failed: {type(error).__name__}: {error}"[:200], extraction_id,
            )

    def _evaluate_conformity(self, extraction: dict[str, Any]) -> dict[str, Any]:
        extraction_id = str(extraction["id"])
        samples = self._series_samples(extraction_id)
        with_pre_start = self._series_samples(extraction_id, include_pre_start=True)
        press_c = boiler_at_press(with_pre_start)
        profile = extraction.get("profile") or {}
        window = conformity_window(extraction, samples)
        properties = [
            self._assess_result_property(extraction, samples, specification, window, press_c)
            for specification in self._displayed_specifications(profile, extraction)
        ]
        unjudged = [
            item for item in properties if item["deviation"]["status"] not in {"within", "outside"}
        ]
        # El veredicto es del razonador de EKO, no de esta clase (ADR-004
        # § 11.3.4): aquí solo se le dan las observaciones de la preparación.
        # Las propiedades de arriba describen dónde y cuánto se apartó cada
        # serie, que es medida y no regla; el «conforme» sale de la ontología.
        verdict = self._reasoned_conformance(extraction, with_pre_start, window)
        published = verdict.pop("published")
        conformity = verdict["status"]
        conclusion = verdict["description"]
        explanation = CONFORMITY_EXPLANATIONS[conformity]
        manual = extraction.get("manualMeasurements") or {}
        return {
            # La regla es la de EKO: cambiar de versión es cambiar de regla, y
            # eso abre una evaluación nueva en vez de pisar la anterior.
            "rule": f"eko-{EKO_VERSION}:technical-conformance",
            "window": window,
            "provenance": {
                "extractionSource": extraction.get("source"),
                "profileId": profile.get("id"),
                "profileVersion": profile.get("version"),
                "calibration": extraction.get("calibration"),
                "firmwareVersion": extraction.get("firmwareVersion"),
                "seriesSampleCount": len(samples),
                "manualMeasurementsRecordedAt": manual.get("recordedAt"),
            },
            "reasoning": {
                "agent": "EvidenceBasedReasoner",
                "ontology": {"name": "EKO", "version": EKO_VERSION},
                "assessment": verdict["assessment"],
            },
            "technicalConformance": {
                "status": conformity,
                "profileId": profile.get("id"),
                "profileVersion": profile.get("version"),
                "assessedProperties": len(properties) - len(unjudged),
                "totalProperties": len(properties),
                "missingData": [item["label"] for item in unjudged],
            },
            "properties": properties,
            "conclusion": conclusion,
            "explanation": explanation,
            **published,
        }

    def _reasoned_conformance(
        self,
        extraction: dict[str, Any],
        samples: list[dict[str, Any]],
        window: dict[str, Any],
    ) -> dict[str, Any]:
        preparation = self._preparation_graph(extraction, samples, window)
        graph = reason(preparation)
        extraction_iri = stable_iri("extraction", str(extraction["id"]))
        verdict = technical_conformance(graph, extraction_iri)
        if verdict is None:
            raise ConflictError("el razonador no evaluó la preparación")
        # Lo concluido viaja con la evaluación para que el trabajador semántico
        # lo publique (PC04, PC22): antes solo se publicaba lo de los cafés en
        # que alguien pidió una recomendación.
        return {**verdict, "published": conformance_conclusions(graph, preparation, extraction_iri)}

    def _preparation_graph(
        self,
        extraction: dict[str, Any],
        samples: list[dict[str, Any]] | None = None,
        window: dict[str, Any] | None = None,
        water_estimate: dict[str, Any] | None = None,
    ) -> Graph:
        """La preparación como la ve el razonador: perfil, observaciones y catación.

        El agua estimada solo entra si se da: estimarla con el gemelo simula
        horas de telemetría, y la conformidad se calcula al sincronizar, que la
        placa corta a los 2 s. La pide el análisis que lanza el barista.
        """
        if samples is None:
            samples = self._series_samples(str(extraction["id"]), include_pre_start=True)
        if window is None:
            window = conformity_window(
                extraction, [s for s in samples if int(s.get("elapsedMs", 0)) >= 0]
            )
        profile = extraction.get("profile") or {}
        graph_profile = {
            **profile,
            "id": profile.get("id") or "unknown",
            "specifications": self._result_specifications(profile, extraction),
        }
        observations = preparation_observations(extraction, samples, window, water_estimate)
        return extraction_graph(
            extraction, graph_profile, observations,
            sensory_references(self.repository, extraction),
        )

    def _water_estimate(self, extraction: dict[str, Any]) -> dict[str, Any] | None:
        """El agua sobre el café que estima el gemelo, para el razonador, o None.

        Solo la del gemelo (modelo Pe), que ve el reposo y los usos seguidos. La
        recta del acta no se usa aquí: da el objetivo por construcción, y con
        la subida del primer uso daría el agua más caliente justo en el café que
        sale más frío. Sin estimación del gemelo no se publica ninguna.
        """
        try:
            twin = self._twin_water(extraction)
        except Exception as error:  # noqa: BLE001
            self.repository.audit(
                "system:reasoning", "water-estimate", f"extraction:{extraction.get('id')}",
                f"failed: {type(error).__name__}: {error}"[:200], str(extraction.get("id")),
            )
            return None
        if twin.get("status") != "estimated" or twin.get("estimatedC") is None:
            return None
        return {
            "valueC": float(twin["estimatedC"]),
            "method": f"urn:method:water-after-rest:{twin['method']}",
            "note": "Agua sobre el café estimada por el gemelo con el reposo y los usos previos; "
                    "no es una medida.",
        }

    def _reasoning_graph(self, extraction: dict[str, Any]) -> Graph:
        """La preparación y las del mismo día, para que el razonador compare.

        Cuáles son comparables lo decide la regla `comparable` del razonador
        (mismo perfil, mismo café, mismo día); aquí solo se acota el día, para
        no darle la base entera.
        """
        graph = self._preparation_graph(
            extraction, water_estimate=self._water_estimate(extraction)
        )
        day = str(extraction.get("startedAt") or "")[:10]
        if not day:
            return graph
        for item in self.repository.list_documents("extraction", page=1, page_size=1000).items:
            if item["id"] == extraction["id"] or str(item.get("startedAt") or "")[:10] != day:
                continue
            other = self.get("extraction", str(item["id"]))
            if interrupted_by_power_loss(other) or other.get("source") != "measured":
                continue
            graph += self._preparation_graph(other)
        return graph

    def machine_state(self) -> dict[str, Any]:
        """El estado de la máquina ahora, desde cuándo y por qué.

        El estado sale de la última telemetría; el «desde cuándo» y el motivo,
        del último cambio de estado que la placa registró hacia ese mismo
        estado. Si no coinciden (se perdió el aviso del cambio), el tiempo en
        el estado queda sin saber en vez de tomarlo de un cambio anterior, y la
        señal se da por perdida si la última telemetría tiene más de
        `MACHINE_SIGNAL_TIMEOUT_S`, para no mostrar «lista» de una máquina que
        se apagó hace una hora.
        """
        telemetry = self.repository.latest_machine_telemetry()
        if telemetry is None or telemetry.get("source") != "measured":
            return {"connected": False, "lastTelemetryAt": None, "state": None,
                    "reachable": self._machine_reachable()}
        events = self.repository.list_documents("event", page=1, page_size=200).items
        transitions = sorted(
            (item for item in events if item.get("kind") in {"state-transition", "alarm"}),
            key=lambda item: str(item.get("occurredAt") or ""),
        )
        state = telemetry.get("state")
        last = transitions[-1] if transitions else None
        entered = last if last is not None and last.get("state") == state else None
        # La telemetría acota el cambio aunque el aviso no llegue (19-oct-2026:
        # la placa dejó de enviar eventos y el panel quedó sin «desde cuándo»).
        # Un aviso anterior a la última muestra con otro estado es de una vez
        # anterior en ese mismo estado, no de esta.
        observed = self.repository.machine_state_entered_at(str(state)) if state else None
        previous_at = (observed or {}).get("previousAt")
        if (
            entered is not None
            and previous_at
            and utc_instant_text(entered.get("occurredAt")) < utc_instant_text(previous_at)
        ):
            entered = None
        if entered is not None:
            since, since_source = entered.get("occurredAt"), "event"
        elif observed is not None and previous_at:
            since, since_source = observed["enteredAt"], "telemetry"
        else:
            since, since_source = None, None
        # La señal se juzga por cuándo llegó la lectura, con el reloj de la
        # plataforma, y no por la hora que le pone la placa: con el reloj de la
        # placa corrido (26-oct-2026, 10 h atrás) el panel decía «Sin señal»
        # con la placa enviando cada segundo. La diferencia se informa aparte.
        occurred = str(telemetry.get("occurredAtUtc") or telemetry.get("occurredAt") or "")
        received = str(telemetry.get("receivedAtUtc") or occurred)
        clock_offset_s = None
        try:
            received_at = datetime.fromisoformat(received.replace("Z", "+00:00"))
            age = datetime.now(UTC) - received_at
            connected = age.total_seconds() <= MACHINE_SIGNAL_TIMEOUT_S
            if occurred:
                clock_offset_s = round(
                    (received_at - datetime.fromisoformat(occurred.replace("Z", "+00:00")))
                    .total_seconds()
                )
        except ValueError:
            connected = False
        targets = telemetry.get("targets") or {}
        water = targets.get("temperatureC")
        body = (telemetry.get("temperature") or {}).get("value")
        first_use_c = self._requested_first_use_c(str(telemetry.get("deviceId") or ""))
        return {
            "connected": connected,
            "lastTelemetryAt": received or None,
            # Cuánto va atrasada (positivo) o adelantada la hora de la placa
            # frente a la de llegada; incluye el retraso de la red, unos ms.
            "boardClockBehindS": clock_offset_s,
            "state": state,
            "since": since,
            # Del aviso de cambio de la placa, o de la primera muestra en este
            # estado (precisión de una muestra, unos segundos).
            "sinceSource": since_source,
            "reason": {
                "code": entered.get("code"),
                "description": entered.get("description"),
                "severity": entered.get("severity"),
            } if entered else None,
            "bodyTemperatureC": body,
            # La subida del primer uso que se le pidió a la placa: con ella la
            # caldera va por encima de la referencia a propósito, para que el
            # primer café tras un reposo salga como en uso continuo (regla
            # primer-uso-v1, medida en el acta v7). El 26-oct la caldera estaba
            # en 105,5 °C con +3,7 °C y el agua salía «más de 92,5 °C», que es
            # justo lo que la subida evita.
            "firstUseOffsetC": first_use_c,
            # El agua que saldría si se pulsara ahora: la recta vigente en la
            # placa sobre la caldera sin la subida, que es el uso continuo que la
            # subida busca igualar. None fuera del rango en que se midió.
            "estimatedWaterC": estimated_water_c(float(body) - first_use_c)
            if isinstance(body, int | float) else None,
            # Fuera de ese rango no se da cifra, pero sí el lado.
            "estimatedWaterBound": _water_bound(body - first_use_c)
            if isinstance(body, int | float) else None,
            "waterTargetC": water,
            # El objetivo de la caldera incluye la subida: es el que la placa
            # persigue, y sin ella el panel decía 101,9 con la caldera en 105,5.
            "boilerReferenceC": round(CURRENT.boiler_reference_c(float(water)) + first_use_c, 2)
            if isinstance(water, int | float) else None,
            "pressureBar": (telemetry.get("pressure") or {}).get("value"),
            "heaterOn": (telemetry.get("actuators") or {}).get("heaterOn"),
            "pressureTargetBar": targets.get("pressureBar"),
            # Sin lecturas, si la máquina contesta en la red: False es apagada o
            # sin red, True es encendida sin enviar, None es que no se pudo ver.
            "reachable": None if connected else self._machine_reachable(),
        }

    def _requested_first_use_c(self, device_id: str) -> float:
        """La subida del primer uso pedida ahora a la placa; 0 si ninguna."""
        if not device_id:
            return 0.0
        decision = self.sustainment.latest_first_use(device_id) or {}
        offset = decision.get("offsetC")
        return float(offset) if isinstance(offset, int | float) else 0.0

    MACHINE_ADDRESS_KIND = "machine-address"
    REACHABILITY_CACHE_S = 5.0

    def _remember_machine_address(self, address: str | None) -> None:
        # Solo se escribe al cambiar: la placa sincroniza cada pocos segundos.
        if not address:
            return
        stored = self.repository.get_document(self.MACHINE_ADDRESS_KIND, "current") or {}
        if stored.get("address") != address:
            self.repository.upsert_document(
                self.MACHINE_ADDRESS_KIND, "current",
                {"address": address, "seenAt": iso_now()},
            )

    def _machine_reachable(self) -> bool | None:
        stored = self.repository.get_document(self.MACHINE_ADDRESS_KIND, "current") or {}
        address = stored.get("address")
        if not isinstance(address, str):
            return None
        now = time.monotonic()
        cached = self._reachability
        if cached and cached[0] == address and now - cached[1] < self.REACHABILITY_CACHE_S:
            return cached[2]
        result = self.ping(address)
        self._reachability = (address, now, result)
        return result

    def live_preparation(self) -> dict[str, Any]:
        """El café que la máquina está haciendo ahora, con lo que va midiendo.

        Durante el café la placa manda una muestra por segundo con el número de
        la preparación (`CAFE_TELEMETRY_ACTIVE_INTERVAL_MS`); la preparación en
        sí llega al terminar. Con una señal vieja no hay café en curso: sería
        mostrar como actual uno que se cortó.
        """
        state = self.machine_state()
        telemetry = self.repository.latest_machine_telemetry() or {}
        preparation_id = telemetry.get("preparationId")
        if not state["connected"] or state["state"] != "extraction" or not preparation_id:
            return {"active": False, "preparationId": None, "samples": []}
        # Las muestras son del cuerpo de la caldera: su referencia es la de
        # caldera que la placa persigue, no el agua del perfil.
        samples = []
        for item in self.repository.preparation_telemetry(str(preparation_id)):
            elapsed = item.get("elapsedMs")
            if not isinstance(elapsed, int | float):
                continue
            samples.append({
                "id": item.get("id"),
                "sequence": item.get("sequence"),
                "occurredAt": item.get("occurredAtUtc") or item.get("occurredAt"),
                "elapsedMs": elapsed,
                "temperatureC": item.get(
                    "temperatureC", (item.get("temperature") or {}).get("value")
                ),
                "pressureBar": item.get("pressureBar", (item.get("pressure") or {}).get("value")),
                "quality": item.get("quality"),
            })
        samples.sort(key=lambda sample: sample["elapsedMs"])
        return {
            "active": True,
            "preparationId": preparation_id,
            "boilerReferenceC": state["boilerReferenceC"],
            "pressureTargetBar": state["pressureTargetBar"],
            "samples": samples,
        }

    def _stability_fingerprint(self, extraction: dict[str, Any]) -> str:
        """Lo que decide la estabilidad y el agua estimada de un café, resumido.

        Son las lecturas de la placa en las 4 h previas (cuántas hay), la serie
        del café, sus datos anotados y los de los cafés de esa ventana, que el
        gemelo usa como usos y como cuarto. Si nada de eso cambió, el cálculo
        guardado vale.
        """
        started = extraction.get("startedAt")
        device = str(extraction.get("deviceId") or "")
        own = {
            key: extraction.get(key)
            for key in ("startedAt", "durationMs", "boilerOffsetAppliedC", "environment",
                        "manualMeasurements", "profile", "source")
        }
        window: list[Any] = []
        count = 0
        if started and device:
            press = parse_instant(started, "startedAt")
            begin = press - self.TWIN_WATER_HISTORY
            end = press + timedelta(milliseconds=int(extraction.get("durationMs") or 0))
            count = self.repository.count_device_samples_between(
                device, utc_instant_text(begin), utc_instant_text(end)
            )
            for item in self.repository.list_documents(
                "extraction", page_size=50, filters={"source": "measured"}
            ).items:
                if item.get("startedAt") and begin <= parse_instant(
                    item["startedAt"], "startedAt"
                ) <= press:
                    window.append([item.get("id"), item.get("durationMs"),
                                   (item.get("manualMeasurements") or {}).get("beverageMassG"),
                                   item.get("environment")])
        previous = self._previous_measured(extraction)
        material = {
            "own": own,
            "series": len(self._series_samples(str(extraction["id"]), include_pre_start=True)),
            "device": count,
            "window": sorted(window, key=str),
            "previous": previous.get("id") if previous else None,
            "twin": WATER_TWIN_VERSION,
        }
        return sha256(json.dumps(material, sort_keys=True, default=str).encode()).hexdigest()

    def _cached_machine_stability(self, extraction: dict[str, Any]) -> dict[str, Any]:
        """La estabilidad del café, guardada mientras sus datos no cambien.

        Calcularla relee horas de telemetría de la placa (tres consultas sobre
        miles de muestras) y simula el agua de las últimas 4 h: en la Raspberry
        eran varios segundos en cada visita a la página del café (26-oct-2026),
        y el resultado no cambia una vez llega la serie. Se guarda con la huella
        de lo que la decide y se rehace cuando esa huella cambia: al llegar la
        serie, al anotar el cuarto o la taza, o con un café nuevo en la ventana.
        """
        identifier = str(extraction["id"])
        fingerprint = self._stability_fingerprint(extraction)
        cached = self.repository.get_document("machine-stability", identifier)
        if cached and cached.get("fingerprint") == fingerprint:
            return deepcopy(cached["value"])
        value = self._machine_stability(extraction)
        self.repository.upsert_document(
            "machine-stability", identifier,
            {"id": identifier, "fingerprint": fingerprint, "value": value},
        )
        return value

    def _machine_stability(self, extraction: dict[str, Any]) -> dict[str, Any]:
        """Estabilidad de la máquina (§ 4.4), con las bandas medidas y no las del perfil.

        La referencia térmica es la de caldera que fija la placa para el agua
        del perfil (con la recta de ese café, `line_of`), más la subida del
        primer uso si la placa la aplicó en este café: la estabilidad se juzga
        sobre el metal y contra lo que la placa perseguía.
        """
        profile = extraction.get("profile") or {}
        water, _ = profile_reference(profile, "temperature")
        pressure, _ = profile_reference(profile, "pressure")
        offset = extraction.get("boilerOffsetAppliedC")
        line = line_of(extraction)
        reference = None
        if water is not None and line is not None:
            reference = line.boiler_reference_c(water) + (
                float(offset) if isinstance(offset, int | float) else 0.0
            )
        readings, start = self._wait_readings(extraction, bool(offset))
        series = self._series_samples(str(extraction["id"]), include_pre_start=True)
        result = machine_stability(series, reference, pressure, readings, start)
        previous = self._previous_measured(extraction)
        previous_body = None
        if previous is not None:
            previous_readings, _ = self._wait_readings(previous, False)
            previous_body = body_at_press(
                self._series_samples(str(previous["id"]), include_pre_start=True),
                previous_readings,
            )
        result["water"] = water_between_preparations(
            body_at_press(series, readings) if extraction.get("source") == "measured" else None,
            previous_body,
            metal_line_of(extraction),
            metal_line_of(previous) if previous is not None else None,
        )
        if previous is not None:
            result["water"]["previousExtractionId"] = previous.get("id")
        result["water"]["twin"] = self._twin_water(extraction)
        return result

    #: Historia que el modelo del agua tras un reposo simula antes del pulsado. La
    #: pared tiene τ_g ≈ 580 s: en 4 h olvida de sobra cómo empezó.
    TWIN_WATER_HISTORY = timedelta(hours=4)

    def _twin_history(
        self, device: str, begin: datetime, samples_until: datetime, uses_until: datetime
    ) -> dict[str, Any]:
        """Lo que el gemelo necesita de la historia: metal, usos y el último cuarto.

        El metal sale de la telemetría y de las series de las preparaciones medidas;
        los usos, de las preparaciones (masa anotada entre duración, o el caudal
        provisional) y de los purgados (muestras en `manual-purge`). Los tiempos van
        en segundos desde `begin`. Lo comparten el agua de un café hecho y la
        subida del primer uso según la pared.
        """
        origin = begin.timestamp()
        metal: list[tuple[float, float]] = []
        purges: list[float] = []
        for item in self.repository.device_samples_between(
            device, utc_instant_text(begin), utc_instant_text(samples_until)
        ):
            if not item.get("state") or not item.get("occurredAtUtc"):
                continue
            at = parse_instant(item["occurredAtUtc"], "occurredAtUtc").timestamp() - origin
            if item.get("state") == "manual-purge":
                purges.append(at)
            quality = (item.get("channelQuality") or {}).get("temperature", item.get("quality"))
            if item.get("temperatureC") is not None and quality == "valid":
                metal.append((at, float(item["temperatureC"])))
        uses: list[Use] = []
        latest_room: float | None = None
        assumed_flow: list[str] = []
        window = [
            self.get("extraction", str(item["id"]))
            for item in self.repository.list_documents(
                "extraction", page_size=50, filters={"source": "measured"}
            ).items
            if item.get("startedAt")
            and begin <= parse_instant(item["startedAt"], "startedAt") <= uses_until
        ]
        window.sort(key=lambda item: str(item["startedAt"]))
        for item in window:
            at = parse_instant(item["startedAt"], "startedAt").timestamp() - origin
            duration = int(item.get("durationMs") or 0) / 1000.0
            if duration <= 0:
                continue
            for sample in self._series_samples(str(item["id"])):
                if sample.get("temperatureC") is not None and sample.get("quality") == "valid":
                    offset = int(sample["elapsedMs"]) / 1000.0
                    metal.append((at + offset, float(sample["temperatureC"])))
            mass = (item.get("manualMeasurements") or {}).get("beverageMassG")
            if isinstance(mass, int | float) and mass > 0:
                flow = float(mass) / 1000.0 / duration
            else:
                flow = DEFAULT_FLOW_KG_S
                assumed_flow.append(str(item["id"]))
            uses.append(Use(at, duration, flow, str(item["id"])))
            room = (item.get("environment") or {}).get("temperatureC")
            if isinstance(room, int | float):
                latest_room = float(room)
        # Cada tramo seguido de muestras en `manual-purge` es un purgado.
        purges.sort()
        run: list[float] = []
        for at in [*purges, float("inf")]:
            if run and at - run[-1] > 60.0:
                uses.append(Use(run[0], max(run[-1] - run[0], 1.0), PURGE_FLOW_KG_S, "purgado"))
                run = []
            run.append(at)
        return {"origin": origin, "metal": metal, "uses": uses, "latestRoom": latest_room,
                "purges": purges, "assumedFlow": assumed_flow}

    def _twin_water(self, extraction: dict[str, Any]) -> dict[str, Any]:
        """El agua sobre el café que estima el gemelo, con el reposo y los usos seguidos.

        Simula desde 4 h antes del pulsado con el metal de la telemetría y de las
        series de las preparaciones medidas, y con cada uso que se vio: las
        preparaciones (masa anotada entre duración, o el caudal provisional) y
        los purgados (muestras en `manual-purge`). La temperatura del cuarto es la
        anotada en el contexto de esta preparación o de la más reciente de la
        ventana; sin ninguna no se estima, porque la pared se enfría hacia ella.
        """
        result: dict[str, Any] = {"method": WATER_TWIN_VERSION, "status": "no-data",
                                  "estimatedC": None, "useIndex": None, "restS": None,
                                  "previousEstimatedC": None, "differenceC": None,
                                  "within": None, "assumptions": []}
        started, device = extraction.get("startedAt"), extraction.get("deviceId")
        if extraction.get("source") != "measured" or not started or not device:
            return result
        press = parse_instant(started, "startedAt")
        begin = press - self.TWIN_WATER_HISTORY
        end = press + timedelta(milliseconds=int(extraction.get("durationMs") or 0))
        history = self._twin_history(str(device), begin, end, press)
        metal, uses, latest_room = history["metal"], history["uses"], history["latestRoom"]
        purges, assumed_flow = history["purges"], history["assumedFlow"]
        ambient = (extraction.get("environment") or {}).get("temperatureC")
        # La de esta preparación manda; si no se anotó, la más reciente de la
        # ventana; si no hay ninguna, el promedio medido de la franja horaria
        # (room_temperature), declarado como supuesto.
        if not isinstance(ambient, int | float):
            ambient = latest_room
        assumed_slot = None
        if ambient is None:
            ambient, assumed_slot = assumed_room_c(press)
        estimates = estimate_uses(metal, uses, float(ambient))
        by_id = {estimate.reference: estimate for estimate in estimates}
        own = by_id.get(str(extraction["id"]))
        if own is None or own.mean_water_c is None:
            return result
        if assumed_flow:
            result["assumptions"].append(
                {"flowAssumedFor": assumed_flow, "flowGs": DEFAULT_FLOW_KG_S * 1000.0}
            )
        if purges:
            result["assumptions"].append({"purgeFlowGs": PURGE_FLOW_KG_S * 1000.0})
        result["assumptions"].append(
            {"ambientC": float(ambient), "ambientAssumed": assumed_slot}
            if assumed_slot else {"ambientC": float(ambient)}
        )
        result.update(status="estimated", estimatedC=own.mean_water_c,
                      useIndex=own.use_index, restS=own.rest_s)
        earlier = [e for e in estimates if e.start_s < own.start_s and e.reference != "purgado"]
        if earlier and earlier[-1].mean_water_c is not None:
            difference = round(own.mean_water_c - earlier[-1].mean_water_c, 2)
            result.update(previousEstimatedC=earlier[-1].mean_water_c, differenceC=difference,
                          within=abs(difference) <= 0.5)
        return result

    def _previous_measured(self, extraction: dict[str, Any]) -> dict[str, Any] | None:
        """La preparación anterior en las mismas condiciones, con la que se compara el agua.

        La banda térmica es la diferencia con la preparación anterior **hecha en
        las mismas condiciones** (monografía, sec:bandas-estabilidad). Hasta el
        8-nov-2026 esto tomaba la última medida, fuera lo que fuera, y en la
        catación del 4-nov la anterior de casi cada café era una pasada de agua
        sin café: la diferencia mezclaba condiciones distintas
        (`evidencias/catacion-20261104/estabilidad-termica.md`). Ahora la anterior
        es de la misma máquina, de la misma clase (café o pasada de agua, que es
        lo que comparan los ensayos del agua) y con la misma temperatura pedida.

        La marca de pasada de agua vive en las revisiones del registro, por eso
        cada candidata se proyecta antes de mirarla. Solo se buscan las pulsadas
        antes que esta, en vez de las 50 más recientes del
        almacén: con eso, una preparación vieja no se quedaba sin anterior.
        """
        started = extraction.get("startedAt")
        if not started:
            return None
        press = parse_instant(started, "startedAt")
        own = self._project_resource("extraction", extraction)
        water_pass = own.get("purpose") == "water-pass"
        requested = (own.get("profile") or {}).get("temperatureSetpointC")
        filters = {"source": "measured", "startedAtBefore": str(started)}
        if own.get("deviceId"):
            filters["deviceId"] = str(own["deviceId"])
        earlier = []
        for item in self.repository.list_documents("extraction", page_size=50,
                                                   filters=filters).items:
            if not item.get("startedAt") or item.get("id") == own.get("id"):
                continue
            instant = parse_instant(item["startedAt"], "startedAt")
            if instant >= press:
                continue
            candidate = self._project_resource("extraction", item)
            if (candidate.get("purpose") == "water-pass") != water_pass:
                continue
            if (candidate.get("profile") or {}).get("temperatureSetpointC") != requested:
                continue
            earlier.append((instant, candidate))
        return max(earlier, key=lambda pair: pair[0])[1] if earlier else None

    def _wait_readings(
        self, extraction: dict[str, Any], offset_applied: bool
    ) -> tuple[list[tuple[float, float]], float | None]:
        """Las lecturas del cuerpo en la espera en «lista» antes de pulsar.

        La espera empieza en la primera muestra en «ready» tras la última con
        otro estado, como el «desde cuándo» del panel. Con la subida del primer
        uso aplicada, empieza cuando se pidió la última subida: antes de eso la
        placa perseguía otra referencia. Solo se mira hasta 3 h atrás; una
        espera más larga queda acotada a ese tramo.
        """
        started = extraction.get("startedAt")
        device = extraction.get("deviceId")
        if not started or not device:
            return [], None
        press = parse_instant(started, "startedAt")
        samples = sorted(
            (
                (parse_instant(item["occurredAtUtc"], "occurredAtUtc"), item)
                for item in self.repository.device_samples_between(
                    str(device),
                    utc_instant_text(press - timedelta(hours=3)),
                    utc_instant_text(press),
                )
                if item.get("state") and item.get("occurredAtUtc")
            ),
            key=lambda pair: pair[0],
        )
        index = len(samples)
        while index > 0 and samples[index - 1][1].get("state") == "ready":
            index -= 1
        wait = samples[index:]
        if not wait:
            return [], None
        begin = wait[0][0]
        if offset_applied:
            requested = [
                parse_instant(item["decidedAt"], "decidedAt")
                for item in self.repository.list_documents(
                    "first-use-decision", page_size=20, filters={"deviceId": str(device)}
                ).items
                if item.get("decidedAt") and item.get("offsetC")
            ]
            requested = [instant for instant in requested if instant <= press]
            if requested:
                begin = max(begin, max(requested))
        readings = [
            ((instant - press).total_seconds(), float(item["temperatureC"]))
            for instant, item in wait
            if instant >= begin and item.get("temperatureC") is not None
            # La calidad de la muestra junta temperatura y presión; una presión
            # no apta no dice nada del cuerpo.
            and (item.get("channelQuality") or {}).get("temperature", item.get("quality"))
            == "valid"
        ]
        return readings, (begin - press).total_seconds()

    @staticmethod
    def _result_specifications(
        profile: dict[str, Any], extraction: dict[str, Any] | None = None
    ) -> list[dict[str, Any]]:
        """Lo que se juzga: las del perfil y la de caldera que sale del agua (EKO 3.4.0)."""
        offset = (extraction or {}).get("boilerOffsetAppliedC")
        boiler = measures_boiler(extraction or {})
        line = line_of(extraction) if extraction is not None else CURRENT
        specifications = deepcopy(profile.get("specifications") or [])
        if specifications:
            return judged_specifications(specifications, offset, line) if boiler else specifications
        # Sin especificaciones (el perfil que declara el dispositivo, o una copia
        # resumida) se leen la temperatura y la presión con la misma regla que
        # el dispositivo. Una tolerancia ausente queda ausente: no se juzga.
        fallback = []
        for magnitude, property_name, unit in (
            ("temperature", "water-temperature", "°C"),
            ("pressure", "pressure", "bar"),
        ):
            target, tolerance = profile_reference(profile, magnitude)
            if target is None:
                continue
            fallback.append(
                {
                    "property": property_name,
                    "unit": unit,
                    "strategy": "target",
                    "target": target,
                    "tolerance": tolerance,
                }
            )
        return judged_specifications(fallback, offset, line) if boiler else fallback

    @classmethod
    def _displayed_specifications(
        cls, profile: dict[str, Any], extraction: dict[str, Any]
    ) -> list[dict[str, Any]]:
        """Las especificaciones que se muestran, con la de caldera en lugar de la del agua.

        La del agua se juzga a través de la de caldera que sale de ella (EKO
        3.4.0): mostrar las dos repetiría la misma regla y daría por faltante una
        medida que ningún sensor puede dar. La de caldera ocupa el lugar de la
        del agua, para que el orden siga siendo el del perfil.
        """
        judged = cls._result_specifications(profile, extraction)
        derived = {item["derivedFrom"]: item for item in judged if "derivedFrom" in item}
        return [
            derived.get(index, item)
            for index, item in enumerate(judged)
            if "derivedFrom" not in item
        ]

    @classmethod
    def _assess_result_property(
        cls,
        extraction: dict[str, Any],
        samples: list[dict[str, Any]],
        specification: dict[str, Any],
        window: dict[str, Any],
        press_c: float | None = None,
    ) -> dict[str, Any]:
        property_name = str(specification["property"])
        limits = bounds(specification)
        # En un café medido, la caldera al pulsar y no durante el café, que cae
        # siempre (observations.boiler_at_press).
        at_press = property_name == "boiler-temperature" and measures_boiler(extraction)
        series_fields = {
            "boiler-temperature": "temperatureC",
            # Solo en la práctica, que simula el agua (`measures_boiler`).
            "temperature": "temperatureC",
            "water-temperature": "temperatureC",
            "pressure": "pressureBar",
            "extraction-pressure": "pressureBar",
        }
        field = None if at_press else series_fields.get(property_name)
        if at_press:
            source = "instrumental"
            judged = value_deviation(press_c, limits)
        elif field is not None:
            source = "instrumental"
            status = window.get("status")
            if status == "stabilized":
                judged = series_deviation(samples, field, limits, window.get("startMs"))
            else:
                # Sin ventana, lo observado se muestra entero, pero solo la
                # presión que nunca llegó a su rango se puede juzgar: ese es
                # justamente su fallo. La temperatura queda sin evaluar.
                judged = series_deviation(samples, field, limits, 0)
                reason = (
                    "pressure-never-reached-range"
                    if status == "never-stabilized"
                    else "no-pressure-range"
                )
                never_reached = status == "never-stabilized" and field == "pressureBar"
                if never_reached and judged["deviation"]["status"] == "outside":
                    judged["deviation"]["reason"] = reason
                elif judged["deviation"]["status"] not in {"unavailable", "no-tolerance"}:
                    judged["deviation"] = not_evaluable(reason)
        else:
            values, source = cls._result_observations(extraction, samples, property_name)
            judged = value_deviation(values[0] if values else None, limits)
        labels = {
            # Se juzga la caldera, pero se lee en agua (`water`, más abajo).
            "boiler-temperature": "Temperatura media estimada del agua"
            if at_press else "Temperatura de la caldera",
            "temperature": "Temperatura del agua",
            "water-temperature": "Temperatura del agua",
            "pressure": "Presión",
            "extraction-pressure": "Presión",
            "extraction-time": "Tiempo de extracción",
            "coffee-dose": "Dosis de café",
            "beverage-mass": "Masa de bebida",
            "brew-ratio": "Relación de preparación",
            "grind-setting": "Posición del molino",
            "preinfusion-time": "Preinfusión",
            "ambient-temperature": "Temperatura ambiente",
            "relative-humidity": "Humedad relativa",
            "tds": "TDS",
            "brix": "Brix",
            "extraction-yield": "Rendimiento",
            "thermal-stability": "Estabilidad térmica",
        }
        return {
            "property": property_name,
            "label": labels.get(property_name, property_name),
            "unit": specification["unit"],
            "objective": limits.objective,
            "observed": judged["observed"],
            "deviation": judged["deviation"],
            "source": source,
            "evidenceId": f"observation:{extraction['id']}:{property_name}",
        }

    @staticmethod
    def _result_observations(
        extraction: dict[str, Any], samples: list[dict[str, Any]], property_name: str
    ) -> tuple[list[float], str]:
        manual = extraction.get("manualMeasurements") or {}
        environment = extraction.get("environment") or {}
        temperature_values = [
            float(sample["temperatureC"])
            for sample in samples
            if sample.get("temperatureC") is not None
        ]
        mappings: dict[str, tuple[Any, str]] = {
            "extraction-time": (float(extraction["durationMs"]) / 1000, "calculated"),
            "coffee-dose": (manual.get("coffeeDoseG"), "manual"),
            "beverage-mass": (manual.get("beverageMassG"), "manual"),
            "ambient-temperature": (environment.get("temperatureC"), "manual"),
            "relative-humidity": (environment.get("humidityPercent"), "manual"),
            "tds": (manual.get("tdsPercent"), "manual"),
            "extraction-yield": (manual.get("extractionYieldPercent"), "calculated"),
            "thermal-stability": (
                max(temperature_values) - min(temperature_values) if temperature_values else None,
                "calculated",
            ),
        }
        if property_name in {"boiler-temperature", "temperature", "water-temperature"}:
            return temperature_values, "instrumental"
        if property_name == "pressure":
            return (
                [
                    float(sample["pressureBar"])
                    for sample in samples
                    if sample.get("pressureBar") is not None
                ],
                "instrumental",
            )
        if property_name == "brew-ratio":
            dose, mass = manual.get("coffeeDoseG"), manual.get("beverageMassG")
            return ([float(mass) / float(dose)] if dose and mass else []), "calculated"
        value, source = mappings.get(property_name, (None, "unavailable"))
        return ([float(value)] if value is not None else []), source

    def finish_live_extraction(
        self, extraction_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        live = self.get("live-extraction", extraction_id)
        if live.get("status") != "running":
            raise ConflictError("la preparación ya finalizó")
        if not self._series_samples(extraction_id):
            self._append_simulated_sample(live)
        samples = self._series_samples(extraction_id)
        profile = live["profile"]
        duration_ms = max((int(sample["elapsedMs"]) for sample in samples), default=0)
        finished_at = iso_now()
        statistics = stability(
            samples,
            self._requested(profile, "temperatureC"),
            self._requested(profile, "pressureBar"),
        )
        extraction = {
            **live,
            "status": "closed",
            "finishedAt": finished_at,
            "finishedBy": identity.subject,
            "durationMs": duration_ms,
            "termination": payload["cause"],
            "terminationDetail": payload.get("detail"),
            "sampleCount": len(samples),
            "statistics": statistics,
        }
        extraction.pop("lastSampleAt", None)
        self.repository.add_document("extraction", extraction_id, extraction)
        self.repository.upsert_document("live-extraction", extraction_id, extraction)
        self._record_conformity_after(extraction_id, "extraction-closed")
        return extraction

    def _append_simulated_sample(self, live: dict[str, Any]) -> None:
        sequence = int(live.get("sampleCount", 0))
        elapsed_ms = sequence * 500
        missing = sequence > 0 and sequence % 6 == 4
        profile = live["profile"]
        temperature_target = float(profile["targets"].get("temperatureC", 93.0))
        pressure_target = float(profile["targets"].get("pressureBar", 9.0))
        elapsed_seconds = elapsed_ms / 1000
        temperature = (
            None
            if missing
            else round(
                temperature_target
                - 1.8 * (2.71828 ** (-elapsed_seconds / 2.5))
                + 0.22 * sin(elapsed_seconds),
                2,
            )
        )
        pressure = (
            None
            if missing
            else round(
                min(pressure_target, pressure_target * elapsed_seconds / 4)
                + 0.18 * sin(elapsed_seconds * 1.4),
                2,
            )
        )
        sample_at = iso_now()
        sample = {
            "id": f"{live['id']}-sample-{sequence:05d}",
            "extractionId": live["id"],
            "sequence": sequence,
            "elapsedMs": elapsed_ms,
            "occurredAt": sample_at,
            "temperatureC": temperature,
            "pressureBar": pressure,
            "quality": "missing" if missing else "valid",
            "environment": deepcopy(live.get("environment") or {}),
            "source": "simulation",
        }
        # Si otra vista abierta sobre la misma preparación ya escribió esta muestra,
        # se da por buena. Su identificador y sus valores dependen de la secuencia y
        # del tiempo transcurrido, no de quién consulta, de modo que la que existe es
        # la que íbamos a crear.
        #
        # Fallar aquí dejaba sin respuesta a quien llegara segundo, y con tres
        # sesiones simultáneas eso era el 40 % de las lecturas. La cadencia sigue
        # siendo función del tiempo, como declara `live_extraction`: quien llega
        # después no produce muestras de más, solo reconoce las que ya hay.
        with suppress(ConflictError):
            self.repository.add_document("live-sample", sample["id"], sample)
        live["sampleCount"] = sequence + 1
        live["lastSampleAt"] = sample_at
        if missing:
            live.setdefault("missingIntervals", []).append(
                {"fromMs": elapsed_ms, "toMs": elapsed_ms + 500}
            )

    def _series_samples(
        self, extraction_id: str, include_pre_start: bool = False
    ) -> list[dict[str, Any]]:
        """La serie de la preparación, desde que se pulsa.

        Desde el firmware d8a8b66 la placa envía delante el minuto previo a
        pulsar, con elapsedMs negativo. Solo la estabilidad térmica de la
        máquina lo usa (`include_pre_start`); para todo lo demás —gráficas,
        conformidad, réplica, comparación— la preparación empieza al pulsar.
        """
        samples = self.repository.list_documents(
            "live-sample",
            page=1,
            page_size=1000,
            filters={"extractionId": extraction_id},
        ).items
        if not include_pre_start:
            samples = [sample for sample in samples if int(sample.get("elapsedMs", 0)) >= 0]
        return sorted(samples, key=lambda sample: int(sample.get("sequence", 0)))

    @staticmethod
    def _live_observations(
        live: dict[str, Any],
        samples: list[dict[str, Any]],
        preferences: dict[str, Any],
    ) -> list[dict[str, Any]]:
        if not samples:
            return []
        sample = samples[-1]
        observed_at = sample["occurredAt"]
        if sample.get("quality") == "missing":
            return (
                [
                    {
                        "code": "acquisition-missing",
                        "message": "No se recibió la lectura esperada.",
                        "observedAt": observed_at,
                    }
                ]
                if preferences.get("acquisition")
                else []
            )
        observations: list[dict[str, Any]] = []
        checks = (
            ("temperature", "temperatureC", "temperatura", "°C"),
            ("pressure", "pressureBar", "presión", "bar"),
        )
        for preference, field, label, unit in checks:
            value = sample.get(field)
            target = live["profile"]["targets"].get(field)
            tolerance = live["profile"]["tolerances"].get(field, 0)
            if not preferences.get(preference) or value is None or target is None:
                continue
            minimum = float(target) - float(tolerance)
            maximum = float(target) + float(tolerance)
            if float(value) < minimum:
                observations.append(
                    {
                        "code": f"{preference}-below-profile",
                        "message": f"La {label} está por debajo del mínimo del perfil.",
                        "observedAt": observed_at,
                        "observedValue": value,
                        "limit": minimum,
                        "unit": unit,
                    }
                )
            elif float(value) > maximum:
                observations.append(
                    {
                        "code": f"{preference}-above-profile",
                        "message": f"La {label} está por encima del máximo del perfil.",
                        "observedAt": observed_at,
                        "observedValue": value,
                        "limit": maximum,
                        "unit": unit,
                    }
                )
        return observations

    @staticmethod
    def _requested(profile: dict[str, Any], field: str) -> Requested:
        magnitude = "temperature" if field == "temperatureC" else "pressure"
        target, tolerance = profile_reference(profile, magnitude)
        return Requested(target=target, tolerance=tolerance)

    @staticmethod
    def _profile_snapshot(profile: dict[str, Any]) -> dict[str, Any]:
        # La misma lectura que la del dispositivo (`profile_reference`): un perfil
        # que pide `water-temperature` declara su temperatura, y antes esta copia
        # la ignoraba y la preparación se juzgaba contra 93 °C y 1 °C supuestos.
        targets: dict[str, float] = {}
        tolerances: dict[str, float] = {}
        for magnitude, field in (("temperature", "temperatureC"), ("pressure", "pressureBar")):
            target, tolerance = profile_reference(profile, magnitude)
            if target is not None:
                targets[field] = target
            if tolerance is not None:
                tolerances[field] = tolerance
        return {
            "id": profile["id"],
            "name": profile.get("name"),
            "version": profile.get("version", 1),
            "status": profile.get("status"),
            "targets": targets,
            "tolerances": tolerances,
            "specifications": deepcopy(profile.get("specifications", [])),
        }

    @staticmethod
    def _first_author(extraction: dict[str, Any], identity: Identity) -> dict[str, Any]:
        """Quién hizo la preparación, si todavía no consta.

        La placa no sabe quién pulsó el botón: la preparación real llega sin
        `barista`. La primera persona que registra algo sobre ella (contexto,
        medidas a mano o catación) es quien la preparó; queda en la misma
        revisión, así se ve cuándo y por qué se asignó. Un autor ya puesto no se
        cambia: quien corrige después no pasa a ser quien preparó. Las
        preparaciones viejas sin autor siguen sin él hasta que alguien registre
        algo; asignarles uno al leerlas afirmaría lo que no consta.
        """
        if extraction.get("barista"):
            return {}
        return {"barista": identity.subject}

    def link_context(
        self,
        extraction_id: str,
        payload: dict[str, Any],
        identity: Identity,
        idempotency_key: str | None = None,
    ) -> dict[str, Any]:
        extraction = self.get("extraction", extraction_id)
        # Solo cambia lo que llega: el barista puede poner el café ahora y la
        # máquina mañana. Cada corrección es una revisión y la anterior se
        # conserva; lo medido por la máquina no pasa por aquí.
        changes: dict[str, Any] = {"contextUpdatedBy": identity.subject}
        if payload.get("coffeeId"):
            coffee_id = require_text(payload.get("coffeeId"), "coffeeId", 80)
            coffee = self.get("coffee", coffee_id)
            changes.update(
                {
                    "coffeeId": coffee_id,
                    "coffeeSnapshot": deepcopy(coffee),
                    "coffeeReference": coffee.get("commercialReference")
                    or coffee.get("lotIdentifier")
                    or coffee.get("bagIdentifier"),
                }
            )
        # El objetivo sensorial va con el café: al cambiar de café se reemplaza,
        # y sin café nuevo solo cambia si se manda uno.
        if payload.get("coffeeId") or payload.get("targetSensoryProfile") is not None:
            target = payload.get("targetSensoryProfile")
            changes["targetSensoryProfile"] = (
                {**target, "definedBy": identity.subject} if target is not None else None
            )
        for field in ("machineId", "grinderId"):
            if payload.get(field):
                self.get("equipment", str(payload[field]))
                changes[field] = payload[field]
        for field in ("grindSetting", "notes", "portafilterConfiguration", "purpose"):
            if payload.get(field) is not None:
                changes[field] = payload[field]
        # El cuarto va donde ya lo dejaba la preparación simulada (`environment`),
        # que es de donde salen sus observaciones en el grafo. Se combina con lo
        # que hubiera: mandar la humedad no borra la temperatura.
        environment_fields = {
            "ambientTemperatureC": "temperatureC",
            "ambientHumidityPercent": "humidityPercent",
        }
        if any(payload.get(field) is not None for field in environment_fields):
            environment = dict(extraction.get("environment") or {})
            for field, stored in environment_fields.items():
                if payload.get(field) is not None:
                    environment[stored] = payload[field]
            changes["environment"] = environment
        session = {
            key: value
            for key, value in (payload.get("tastingSession") or {}).items()
            if value is not None
        }
        if session:
            changes["tastingSession"] = {**(extraction.get("tastingSession") or {}), **session}
        if len(changes) == 1:
            raise ValidationError("indica al menos un dato del contexto")
        changes.update(self._first_author(extraction, identity))
        return self._append_revision(
            "extraction",
            extraction_id,
            "coffee-context",
            changes,
            identity,
            idempotency_key,
        )

    def measurements(
        self,
        extraction_id: str,
        payload: dict[str, Any],
        identity: Identity,
        idempotency_key: str | None = None,
    ) -> dict[str, Any]:
        extraction = self.get("extraction", extraction_id)
        current = deepcopy(extraction.get("manualMeasurements") or {})
        current.pop("completion", None)
        entries = current.setdefault("entries", {})
        recorded_at = iso_now()
        units = {
            "coffeeDoseG": "g",
            "beverageMassG": "g",
            "tdsPercent": "%",
            "instrument": None,
        }
        readings = payload.get("tdsReadings")
        if readings is not None:
            if (
                payload.get("tdsPercent") is not None
                or not isinstance(readings, list)
                or not 1 <= len(readings) <= 3
                or any(
                    isinstance(item, bool)
                    or not isinstance(item, int | float)
                    or not 0 < float(item) <= 30.0
                    for item in readings
                )
            ):
                raise ValidationError("tdsReadings fuera de rango")
            # La media sale de las lecturas, no la escribe nadie. Se redondea
            # solo para quitar el ruido de la suma en coma flotante (9,5 + 9,6 +
            # 9,7 no da 28,8 exacto); seis decimales están muy por debajo de lo
            # que resuelve cualquier refractómetro.
            payload = {**payload, "tdsPercent": round(fmean(readings), 6)}
        for field, upper in (
            ("coffeeDoseG", 100.0),
            ("beverageMassG", 300.0),
            ("tdsPercent", 30.0),
        ):
            if field not in payload or payload[field] is None:
                continue
            raw = payload.get(field)
            if (
                isinstance(raw, bool)
                or not isinstance(raw, int | float)
                or not 0 < float(raw) <= upper
            ):
                raise ValidationError(f"{field} fuera de rango")
            value = float(raw)
            current[field] = value
            entries[field] = {
                "value": value,
                "unit": units[field],
                "recordedBy": identity.subject,
                "recordedAt": recorded_at,
            }
        if payload.get("tdsPercent") is not None:
            # Una media corregida sin lecturas deja viejas las que hubiera:
            # conservarlas diría que la nueva media sale de ellas.
            if readings is not None:
                current["tdsReadings"] = [float(item) for item in readings]
                entries["tdsPercent"]["readings"] = current["tdsReadings"]
            else:
                current.pop("tdsReadings", None)
        for field in ("instrument",):
            if field not in payload or payload[field] is None:
                continue
            value = payload[field]
            current[field] = value
            entries[field] = {
                "value": value,
                "unit": units[field],
                "recordedBy": identity.subject,
                "recordedAt": recorded_at,
            }
        required_values = ("coffeeDoseG", "beverageMassG", "tdsPercent")
        if all(current.get(field) is not None for field in required_values):
            current["extractionYieldPercent"] = round(
                current["tdsPercent"] * current["beverageMassG"] / current["coffeeDoseG"],
                3,
            )
        else:
            current.pop("extractionYieldPercent", None)
        # La relación café-agua obtenida no necesita el TDS: sale de las dos masas.
        # Se expone aparte porque es una condición de preparación que el barista fija
        # como objetivo, y sin calcularla nadie sabe si la preparación la cumplió.
        if current.get("coffeeDoseG") is not None and current.get("beverageMassG") is not None:
            current["brewRatio"] = round(
                current["beverageMassG"] / current["coffeeDoseG"],
                3,
            )
        else:
            current.pop("brewRatio", None)
        extraction["manualMeasurements"] = self._manual_completion(
            {
                **current,
                "entries": entries,
                "recordedBy": identity.subject,
                "recordedAt": recorded_at,
            }
        )
        revision = self._append_revision(
            "extraction",
            extraction_id,
            "manual-measurements",
            {
                "manualMeasurements": extraction["manualMeasurements"],
                **self._first_author(extraction, identity),
            },
            identity,
            idempotency_key,
        )
        self._record_conformity_after(extraction_id, "manual-measurements")
        return revision

    @staticmethod
    def _manual_completion(measurements: dict[str, Any]) -> dict[str, Any]:
        labels = {
            "coffeeDoseG": "dosis de café",
            "beverageMassG": "masa de bebida",
            "tdsPercent": "TDS",
        }
        missing = [field for field in labels if measurements.get(field) is None]
        yield_missing = [
            field
            for field in ("coffeeDoseG", "beverageMassG", "tdsPercent")
            if measurements.get(field) is None
        ]
        ratio_missing = [
            field
            for field in ("coffeeDoseG", "beverageMassG")
            if measurements.get(field) is None
        ]
        return {
            **measurements,
            "completion": {
                "missingFields": [{"field": field, "label": labels[field]} for field in missing],
                "results": {
                    "extractionYieldPercent": {
                        "status": "available" if not yield_missing else "pending",
                        "missingFields": [labels[field] for field in yield_missing],
                    },
                    "brewRatio": {
                        "status": "available" if not ratio_missing else "pending",
                        "missingFields": [labels[field] for field in ratio_missing],
                    },
                },
            },
        }

    @classmethod
    def _with_manual_completion(cls, extraction: dict[str, Any]) -> dict[str, Any]:
        measurements = extraction.get("manualMeasurements")
        if not measurements:
            measurements = {}
        return {
            **extraction,
            "manualMeasurements": cls._manual_completion(deepcopy(measurements)),
        }

    def sensory(
        self,
        extraction_id: str,
        payload: dict[str, Any],
        identity: Identity,
        idempotency_key: str | None = None,
    ) -> dict[str, Any]:
        extraction = self.get("extraction", extraction_id)
        evaluated_at = parse_instant(payload.get("evaluatedAt"), "evaluatedAt")
        finished_at = parse_instant(extraction.get("finishedAt"), "finishedAt")
        if evaluated_at < finished_at:
            raise ValidationError("evaluatedAt no puede ser anterior al fin de la extracción")
        scores = payload.get("scores")
        if not isinstance(scores, list) or not scores:
            raise ValidationError("scores debe ser una lista no vacía")
        comparisons = payload.get("comparisons") or []
        coffee = (
            self.repository.get_document("coffee", str(extraction.get("coffeeId")))
            if extraction.get("coffeeId")
            else None
        )
        profile = self.repository.get_document(
            "profile", str(extraction.get("profile", {}).get("id", ""))
        )
        if "declared-profile" in comparisons and not (
            coffee and coffee.get("declaredSensoryProfile")
        ):
            raise ValidationError("el café no tiene una descripción sensorial declarada")
        if "barista-target" in comparisons and not (profile and profile.get("sensoryTarget")):
            raise ValidationError("el perfil no tiene un objetivo sensorial del barista")
        extraction["sensoryEvaluation"] = {**payload, "evaluatedBy": identity.subject}
        return self._append_revision(
            "extraction",
            extraction_id,
            "sensory-evaluation",
            {
                "sensoryEvaluation": extraction["sensoryEvaluation"],
                **self._first_author(extraction, identity),
            },
            identity,
            idempotency_key,
        )

    def _repeatability(self, items: list[dict[str, Any]]) -> dict[str, Any]:
        """Cuánto varían la masa, el tiempo y el agua estimada en la serie comparada.

        La monografía juzga así el perfil final y las cifras salían de un cálculo
        a mano (8-nov-2026). El agua es la que la plataforma ya estima al pulsar
        (`machineStability.water`); si las preparaciones usaron rectas de actas
        distintas no se resume, por la misma razón que no se restan con la
        anterior: el cambio de acta se confundiría con el del agua. Se declara si
        comparten perfil y café, pero no se exige: lo decide quien compara.
        """
        waters = []
        for item in items:
            water = (self._cached_machine_stability(item).get("water") or {})
            waters.append((water.get("estimatedC"), water.get("method")))
        methods = {method for value, method in waters if value is not None}
        water_spread = spread([value for value, _ in waters])
        water_spread["status"] = "line-changed" if len(methods) > 1 else "estimated"
        if len(methods) > 1:
            water_spread.update(minimum=None, maximum=None, range=None, mean=None,
                                standardDeviation=None)
        manual = [item.get("manualMeasurements") or {} for item in items]
        profiles = {(item.get("profile") or {}).get("id") for item in items}
        coffees = {item.get("coffeeId") for item in items}
        return {
            "rule": "repeatability-v1",
            "sharedProfile": len(profiles) == 1 and None not in profiles,
            "sharedCoffee": len(coffees) == 1 and None not in coffees,
            "metrics": [
                {"key": "beverageMassG", "label": "Masa de bebida", "unit": "g",
                 **spread([entry.get("beverageMassG") for entry in manual])},
                {"key": "durationS", "label": "Tiempo de extracción", "unit": "s",
                 **spread([None if item.get("durationMs") is None
                           else float(item["durationMs"]) / 1000 for item in items])},
                {"key": "estimatedWaterC", "label": "Temperatura estimada del agua",
                 "unit": "°C", "bandC": WATER_BAND_C, **water_spread},
            ],
        }

    def compare(
        self, identifiers: list[str], dimensions: list[str] | None = None
    ) -> dict[str, Any]:
        if (
            len(identifiers) < 2
            or len(identifiers) > 4
            or len(set(identifiers)) != len(identifiers)
        ):
            raise ValidationError("se requieren entre 2 y 4 identificadores únicos")
        selected_dimensions = dimensions or ["preparation"]
        items = [
            self._with_history_context(self.get("extraction", identifier))
            for identifier in identifiers
        ]
        metric_definitions = (
            ("preparation", "durationMs", "Duración", "ms", lambda item: item.get("durationMs")),
            (
                "preparation",
                "temperatureMaeC",
                "Error medio de la temperatura de la caldera",
                "°C",
                lambda item: (item.get("statistics") or {}).get("temperatureMaeC"),
            ),
            (
                "preparation",
                "pressureMaeBar",
                "Error medio de la presión",
                "bar",
                lambda item: (item.get("statistics") or {}).get("pressureMaeBar"),
            ),
            (
                "preparation",
                "coffeeDoseG",
                "Dosis de café",
                "g",
                lambda item: (item.get("manualMeasurements") or {}).get("coffeeDoseG"),
            ),
            (
                "preparation",
                "beverageMassG",
                "Masa de bebida",
                "g",
                lambda item: (item.get("manualMeasurements") or {}).get("beverageMassG"),
            ),
            (
                "preparation",
                "tdsPercent",
                "TDS",
                "%",
                lambda item: (item.get("manualMeasurements") or {}).get("tdsPercent"),
            ),
            (
                "preparation",
                "extractionYieldPercent",
                "Rendimiento",
                "%",
                lambda item: (item.get("manualMeasurements") or {}).get("extractionYieldPercent"),
            ),
            (
                "environment",
                "ambientTemperatureC",
                "Temperatura ambiente",
                "°C",
                lambda item: (item.get("environment") or {}).get("temperatureC"),
            ),
            (
                "environment",
                "ambientHumidityPercent",
                "Humedad ambiente",
                "%",
                lambda item: (item.get("environment") or {}).get("humidityPercent"),
            ),
        )
        metrics: list[dict[str, Any]] = []
        for dimension, key, label, unit, getter in metric_definitions:
            if dimension not in selected_dimensions:
                continue
            values = [getter(item) for item in items]
            baseline = values[0]
            comparisons = []
            for value in values:
                if value is None or baseline is None:
                    comparisons.append(
                        {"absolute": None, "relativePercent": None, "status": "missing"}
                    )
                    continue
                absolute = abs(float(value) - float(baseline))
                relative = None if float(baseline) == 0 else absolute / abs(float(baseline)) * 100
                comparisons.append(
                    {
                        "absolute": round(absolute, 3),
                        "relativePercent": None if relative is None else round(relative, 2),
                        "status": "equal" if absolute == 0 else "different",
                    }
                )
            metrics.append(
                {
                    "dimension": dimension,
                    "key": key,
                    "label": label,
                    "unit": unit,
                    "values": values,
                    "comparisons": comparisons,
                }
            )
        series = (
            [self.extraction_series(identifier) for identifier in identifiers]
            if "curves" in selected_dimensions
            else []
        )
        calibration_ids = {item.get("calibrationId") for item in items if item.get("calibrationId")}
        calibrations = self.repository.list_documents("calibration", page=1, page_size=1000).items
        matching_calibrations = [
            calibration
            for calibration in calibrations
            if len(set(calibration.get("testExtractionIds") or []) & set(identifiers)) >= 2
        ]
        adjustment_sequence = [
            {**adjustment, "calibrationId": calibration["id"]}
            for calibration in matching_calibrations
            for adjustment in calibration.get("adjustments") or []
        ]
        if calibration_ids and len(calibration_ids) == 1 and not adjustment_sequence:
            episodes = self.repository.list_documents("episode", page=1, page_size=1000).items
            adjustment_sequence = [
                {**episode["adjustment"], "calibrationId": next(iter(calibration_ids))}
                for episode in episodes
                if {
                    episode.get("initialExtractionId"),
                    episode.get("followUpExtractionId"),
                }.issubset(set(identifiers))
            ]
        adjustment_sequence.sort(key=lambda adjustment: adjustment.get("occurredAt", ""))
        return {
            "items": items,
            "repeatability": self._repeatability(items),
            "comparedAt": iso_now(),
            "dimensions": selected_dimensions,
            "metrics": metrics,
            "series": series,
            "adjustmentSequence": adjustment_sequence,
            "sharedCalibrationId": next(iter(calibration_ids))
            if len(calibration_ids) == 1
            else None,
        }

    def create_episode(self, payload: dict[str, Any], identity: Identity) -> dict[str, Any]:
        initial = self.get(
            "extraction",
            require_text(payload.get("initialExtractionId"), "initialExtractionId", 80),
        )
        follow_up = self.get(
            "extraction",
            require_text(payload.get("followUpExtractionId"), "followUpExtractionId", 80),
        )
        coffee_id = require_text(payload.get("coffeeId"), "coffeeId", 80)
        if initial.get("coffeeId") != coffee_id or follow_up.get("coffeeId") != coffee_id:
            raise ValidationError("las extracciones deben pertenecer al coffeeId del episodio")
        if not initial.get("sensoryEvaluation") or not follow_up.get("sensoryEvaluation"):
            raise ValidationError("ambas extracciones requieren evaluación sensorial")
        adjustment = payload.get("adjustment") or {}
        recommendation_id = adjustment.get("recommendationId")
        if recommendation_id:
            recommendation = self.get("recommendation", recommendation_id)
            if recommendation.get("extractionId") != initial.get("id"):
                raise ValidationError("la recomendación debe corresponder a la extracción inicial")
            recommendation_decision = self.repository.get_document(
                "recommendation-decision", recommendation_id
            )
            if not recommendation_decision or recommendation_decision.get("status") not in {
                "accepted",
                "modified",
            }:
                raise ValidationError("el ajuste requiere una recomendación aceptada o modificada")
        outcome = payload.get("outcome") or {}
        initial_finished = parse_instant(initial.get("finishedAt"), "initial.finishedAt")
        follow_up_started = parse_instant(follow_up.get("startedAt"), "followUp.startedAt")
        follow_up_finished = parse_instant(follow_up.get("finishedAt"), "followUp.finishedAt")
        adjustment_at = parse_instant(adjustment.get("occurredAt"), "adjustment.occurredAt")
        assessed_at = parse_instant(outcome.get("assessedAt"), "outcome.assessedAt")
        if not initial_finished <= adjustment_at <= follow_up_started:
            raise ValidationError("el ajuste debe ocurrir entre las dos extracciones")
        if assessed_at < follow_up_finished:
            raise ValidationError(
                "el resultado no puede evaluarse antes de la extracción posterior"
            )
        payload = {
            **payload,
            "id": payload.get("id") or f"episode-{uuid4().hex[:12]}",
            "adjustment": {
                **adjustment,
                "id": adjustment.get("id") or f"adjustment-{uuid4().hex[:12]}",
                "performedBy": identity.subject,
            },
            "outcome": {**outcome, "assessedBy": identity.subject},
        }
        return self.create("episode", payload, identity)

    def register_adjustment(self, payload: dict[str, Any], identity: Identity) -> dict[str, Any]:
        initial = self.get(
            "extraction",
            require_text(payload.get("initialExtractionId"), "initialExtractionId", 80),
        )
        adjustment = payload.get("adjustment") or {}
        occurred_at = parse_instant(adjustment.get("occurredAt"), "adjustment.occurredAt")
        finished_at = parse_instant(initial.get("finishedAt"), "initial.finishedAt")
        if occurred_at < finished_at:
            raise ValidationError("el ajuste debe ser posterior a la preparación inicial")
        recommendation_id = adjustment.get("recommendationId")
        if recommendation_id:
            recommendation = self.get("recommendation", recommendation_id)
            decision = self.repository.get_document("recommendation-decision", recommendation_id)
            if recommendation.get("extractionId") != initial.get("id"):
                raise ValidationError("la recomendación debe corresponder a la preparación inicial")
            if not decision or decision.get("status") not in {"accepted", "modified"}:
                raise ValidationError("el ajuste requiere una recomendación aceptada o modificada")
        identifier = f"episode-{uuid4().hex[:12]}"
        registered_at = iso_now()
        episode = {
            "id": identifier,
            "status": "awaiting-verification",
            "coffeeId": initial.get("coffeeId"),
            "initialExtractionId": initial["id"],
            "followUpExtractionId": None,
            "adjustment": {
                **adjustment,
                "id": f"adjustment-{uuid4().hex[:12]}",
                "performedBy": identity.subject,
                "registeredAt": registered_at,
            },
            "targetOutcome": payload.get("targetOutcome"),
            "outcome": None,
            "createdAt": registered_at,
            "createdBy": identity.subject,
            "provenance": {
                "actor": identity.subject,
                "recordedAt": registered_at,
                "source": "barista-entry",
            },
        }
        stored = self.repository.add_document("episode", identifier, episode)
        derived = self._derive_profile_from_adjustment(
            initial, episode["adjustment"], recommendation_id, identity
        )
        if derived is not None:
            stored = self.repository.upsert_document(
                "episode", identifier, {**stored, "derivedProfileId": derived["id"]}
            )
        return stored

    # Propiedades que el dispositivo gobierna, con el campo del contrato que
    # cada una modifica. La molienda, la dosis y la proporción las ejecuta el
    # barista: derivar una configuración para ellas anotaría un cambio que la
    # máquina no puede hacer.
    _ADJUSTABLE_BY_DEVICE = {
        "water-temperature": "temperatureSetpointC",
        "pressure": "pressureSetpointBar",
        "extraction-time": "maximumTimeMs",
    }

    def _derive_profile_from_adjustment(
        self,
        initial: dict[str, Any],
        adjustment: dict[str, Any],
        recommendation_id: str | None,
        identity: Identity,
    ) -> dict[str, Any] | None:
        """Configuración derivada de un ajuste aceptado, si la máquina puede aplicarlo.

        Cierra la vía de recomendación de ADR-017 § 4.6: una decisión sobre una
        recomendación se convierte en la configuración que regirá en la siguiente
        preparación, sin que nadie tenga que rehacerla a mano.

        Devuelve ``None`` cuando no hay nada que derivar, que es lo habitual: sin
        recomendación detrás no se deriva, porque la cadena de autoridad quedaría
        sin el eslabón que la justifica, y un ajuste de molienda no produce
        configuración alguna.
        """
        if not recommendation_id:
            return None
        field = self._ADJUSTABLE_BY_DEVICE.get(str(adjustment.get("property") or ""))
        if field is None:
            return None
        new_value = adjustment.get("newValue")
        if new_value is None:
            return None

        # La base es el perfil guardado, no la copia que lleva la preparación. Esa
        # copia es la vista del contrato del dispositivo: trae las referencias y
        # el tiempo, pero no las especificaciones, la familia ni la versión
        # anterior. Derivar de ella dejó el 2026-11-04 un perfil sin
        # especificaciones que la ontología rechazó con razón (sim-00000578,
        # `default-profile-v5-ajuste-f6b5438a`). Sin el perfil guardado no se
        # deriva: completarlo con valores plausibles afirmaría lo que no consta.
        base_id = str((initial.get("profile") or {}).get("id") or "")
        base = self.repository.get_document("profile", base_id) if base_id else None
        if base is None:
            return None

        unit = str(adjustment.get("unit") or "").lower()
        in_seconds = unit in {"s", "seg", "segundos"}
        if field == "maximumTimeMs":
            # El ajuste declara el tiempo en segundos y el contrato en milisegundos.
            value: Any = (
                int(float(new_value) * 1000) if in_seconds else int(float(new_value))
            )
        else:
            value = float(new_value)

        derived_id = f"{base['id']}-ajuste-{uuid4().hex[:8]}"
        # Lo que describe cómo se activó la base no pasa a la derivada: ella
        # tiene su propia activación, que pone `activate_profile`.
        inherited = {
            key: deepcopy(item)
            for key, item in base.items()
            if key
            not in {"activation", "approvedAt", "approvedBy", "updatedAt", "updatedBy",
                    "createdAt", "provenance", "derivedFrom"}
        }
        inherited["specifications"] = _specifications_with_adjustment(
            inherited.get("specifications") or [], field, value
        )
        derived = {
            **inherited,
            "id": derived_id,
            "familyId": base.get("familyId") or base["id"],
            "version": int(base.get("version") or 1) + 1,
            "previousVersionId": base["id"],
            "createdAt": iso_now(),
            field: value,
            "status": "approved",
            "derivedFrom": {
                "profileId": base["id"],
                "extractionId": initial.get("id"),
                "adjustmentId": adjustment.get("id"),
                "property": adjustment.get("property"),
                "previousValue": adjustment.get("previousValue"),
                "newValue": new_value,
            },
            "provenance": {
                "actor": identity.subject,
                "recordedAt": iso_now(),
                "source": "recommendation-adjustment",
            },
        }
        self.repository.upsert_document("profile", derived_id, derived)
        return self.activate_profile(
            derived_id,
            identity,
            {"via": "recommendation", "recommendationId": recommendation_id},
        )

    def start_episode_verification(self, episode_id: str, identity: Identity) -> dict[str, Any]:
        episode = self.get("episode", episode_id)
        if episode.get("status") != "awaiting-verification":
            raise ConflictError("la comprobación de este ajuste ya fue iniciada")
        initial = self.get("extraction", episode["initialExtractionId"])
        live = self.start_live_extraction(
            {
                "coffeeId": episode["coffeeId"],
                "profileId": initial.get("profile", {}).get("id"),
                "ambientTemperatureC": None,
                "ambientHumidityPercent": None,
                "machineId": initial.get("machineId") or initial.get("deviceId"),
                "grinderId": initial.get("grinderId"),
                "calibrationId": initial.get("calibrationId"),
            },
            identity,
        )
        live["verificationForEpisodeId"] = episode_id
        self.repository.upsert_document("live-extraction", live["id"], live)
        episode.update(
            {
                "status": "verification-running",
                "followUpExtractionId": live["id"],
                "verificationStartedAt": live["startedAt"],
                "verificationStartedBy": identity.subject,
            }
        )
        self.repository.upsert_document("episode", episode_id, episode)
        return live

    def link_episode_follow_up(
        self, episode_id: str, extraction_id: str, identity: Identity
    ) -> dict[str, Any]:
        """Enlaza el café real que comprueba un ajuste.

        «Iniciar comprobación» abre una preparación del simulador, que no dice
        nada de la máquina: comprobar un ajuste con ella daba por comprobado lo
        que nadie midió. Aquí el barista prepara en la máquina como siempre y
        el café que llega queda como la comprobación. Tiene que ser medido, del
        mismo café que el inicial y terminado después del ajuste; un ajuste ya
        comprobado no se vuelve a enlazar.
        """
        episode = self.get("episode", episode_id)
        if episode.get("status") != "awaiting-verification":
            raise ConflictError("este ajuste ya tiene su café de comprobación")
        follow_up = self.get("extraction", extraction_id)
        if follow_up["id"] == episode.get("initialExtractionId"):
            raise ValidationError("el café de comprobación no puede ser el inicial")
        if follow_up.get("source") != "measured" or interrupted_by_power_loss(follow_up):
            raise ValidationError("la comprobación tiene que ser un café medido y completo")
        if episode.get("coffeeId") and follow_up.get("coffeeId") != episode.get("coffeeId"):
            raise ValidationError("la comprobación tiene que ser del mismo café que el inicial")
        occurred_at = parse_instant(
            (episode.get("adjustment") or {}).get("occurredAt"), "adjustment.occurredAt"
        )
        finished = follow_up.get("finishedAt")
        if not finished or parse_instant(finished, "finishedAt") < occurred_at:
            raise ValidationError("la comprobación tiene que terminar después del ajuste")
        episode.update(
            {
                "status": "verification-closed",
                "followUpExtractionId": follow_up["id"],
                "verificationStartedAt": follow_up.get("startedAt"),
                "verificationStartedBy": identity.subject,
            }
        )
        return self._with_episode_conclusions(episode)

    def _with_episode_conclusions(self, episode: dict[str, Any]) -> dict[str, Any]:
        """Guarda el ajuste con lo que el razonador concluye de él (PC21).

        El razonador compara la preparación inicial con la de comprobación a
        través del ajuste; lo que concluye se guarda con el episodio para que el
        trabajador semántico lo publique. Se rehace al enlazar la comprobación
        y al clasificar el resultado, porque la cata del café de comprobación
        suele llegar después. Un fallo del razonador no impide guardar el
        episodio: queda en la auditoría.
        """
        try:
            episode.update(self._episode_conclusions(episode))
        except Exception as error:  # noqa: BLE001
            self.repository.audit(
                "system:reasoner", "episode.conclusions", f"episode:{episode['id']}",
                f"failed: {type(error).__name__}: {error}"[:200], episode["id"],
            )
        # Aparte del razonador: que él falle no quita saber si el agua o la
        # presión cambiaron entre las dos preparaciones, ni al revés.
        try:
            episode["attribution"] = self._episode_attribution(episode)
        except Exception as error:  # noqa: BLE001
            self.repository.audit(
                "system:attribution", "episode.attribution", f"episode:{episode['id']}",
                f"failed: {type(error).__name__}: {error}"[:200], episode["id"],
            )
        return self.repository.upsert_document("episode", episode["id"], episode)

    def _episode_attribution(self, episode: dict[str, Any]) -> dict[str, Any] | None:
        """Si el resultado del ajuste se le puede atribuir (`espresso_stability.attribution`).

        Compara la preparación inicial con la de comprobación **entre sí**, no
        cada una con su anterior: en la catación del 4-nov la anterior de cada
        café era casi siempre una pasada de agua (8-nov-2026). Sin comprobación
        enlazada no hay nada que juzgar.
        """
        if not episode.get("followUpExtractionId"):
            return None
        initial = self._attribution_measures(str(episode["initialExtractionId"]))
        follow_up = self._attribution_measures(str(episode["followUpExtractionId"]))
        result = attribution(initial, follow_up,
                             (episode.get("adjustment") or {}).get("property"))
        result.update(initialExtractionId=initial["extractionId"],
                      followUpExtractionId=follow_up["extractionId"],
                      computedAt=iso_now())
        return result

    def _attribution_measures(self, extraction_id: str) -> dict[str, Any]:
        """El agua estimada, la presión de régimen, la dosis y la masa de una preparación."""
        extraction = self.get("extraction", extraction_id)
        readings, _ = self._wait_readings(
            extraction, bool(extraction.get("boilerOffsetAppliedC"))
        )
        body = (
            body_at_press(self._series_samples(extraction_id, include_pre_start=True), readings)
            if extraction.get("source") == "measured" else None
        )
        line = metal_line_of(extraction)
        manual = extraction.get("manualMeasurements") or {}
        return {
            "extractionId": extraction_id,
            "waterC": estimated_water_c(body, line) if line is not None else None,
            "waterMethod": line.method if line is not None else None,
            "pressureBar": regime_pressure_bar(
                self._series_samples(extraction_id),
                (extraction.get("profile") or {}).get("pressureSetpointBar"),
            ),
            "coffeeDoseG": manual.get("coffeeDoseG"),
            "beverageMassG": manual.get("beverageMassG"),
        }

    def _episode_conclusions(self, episode: dict[str, Any]) -> dict[str, Any]:
        adjustment = episode.get("adjustment") or {}
        action = None
        recommendation_id = adjustment.get("recommendationId")
        recommendation = (
            self.repository.get_document("recommendation", str(recommendation_id))
            if recommendation_id else None
        )
        decision = (
            self.repository.get_document("recommendation-decision", str(recommendation_id))
            if recommendation_id else None
        )
        # Las formas piden que el ajuste basado en una recomendación cambie la
        # misma propiedad que ella y que el barista la haya aceptado o
        # modificado; si no, el ajuste se publica como decisión propia.
        if (
            recommendation and recommendation.get("actionIri")
            and (decision or {}).get("status") in {"accepted", "modified"}
            and (recommendation.get("proposedAdjustment") or {}).get("property")
            == adjustment.get("property")
        ):
            action = str(recommendation["actionIri"])
        adjustment_triples = adjustment_graph(episode, action)
        preparations = Graph() + adjustment_triples
        initial = self.get("extraction", str(episode["initialExtractionId"]))
        preparations += self._preparation_graph(initial)
        if episode.get("followUpExtractionId"):
            preparations += self._preparation_graph(
                self.get("extraction", str(episode["followUpExtractionId"]))
            )
        if action and recommendation.get("conclusionGraph"):
            preparations += Graph().parse(data=recommendation["conclusionGraph"], format="turtle")
        reasoned = reason(preparations)
        adjustment_iri = stable_iri("adjustment", str(adjustment.get("id") or episode["id"]))
        concluded = adjustment_conclusions(reasoned, preparations, adjustment_iri)
        graph = adjustment_triples + Graph().parse(
            data=concluded["conclusionGraph"], format="turtle"
        )
        return {
            "conclusionGraph": graph.serialize(format="turtle"),
            "conclusionValidation": concluded["conclusionValidation"],
            "conclusionsComputedAt": iso_now(),
        }

    def assess_episode(
        self, episode_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        episode = self.get("episode", episode_id)
        if episode.get("outcome"):
            raise ConflictError("el resultado de este ajuste ya fue clasificado")
        follow_up_id = episode.get("followUpExtractionId")
        if not follow_up_id:
            raise ValidationError("inicia primero la preparación de comprobación")
        self.get("extraction", follow_up_id)
        initial_facts = self.semantics.evidence_for(episode["initialExtractionId"])
        follow_up_facts = self.semantics.evidence_for(follow_up_id)
        initial_by_predicate = {item.get("predicate"): item for item in initial_facts}
        follow_up_by_predicate = {item.get("predicate"): item for item in follow_up_facts}
        shared = sorted(set(initial_by_predicate) & set(follow_up_by_predicate) - {None})
        if not shared:
            raise ValidationError("no hay evidencia comparable para clasificar el resultado")
        assessed_at = iso_now()
        episode["status"] = "assessed"
        episode["outcome"] = {
            **payload,
            "assessedAt": assessed_at,
            "assessedBy": identity.subject,
            "comparativeEvidence": [
                {
                    "property": predicate,
                    "initialEvidenceId": initial_by_predicate[predicate]["id"],
                    "followUpEvidenceId": follow_up_by_predicate[predicate]["id"],
                }
                for predicate in shared
            ],
        }
        return self._with_episode_conclusions(episode)

    def record_water_reference(
        self, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        """Registra una lectura del agua de salida emparejada con el cuerpo (ADR-019).

        La pareja se fija al registrar y copia la temperatura del cuerpo: la
        telemetría de una preparación se sustituye después por su serie, cuyas
        muestras no tienen instante propio, y sin la copia la lectura perdería
        la evidencia con la que se emparejó.

        El instante que empareja es el de la plataforma al recibir la lectura,
        no el que declara el navegador. El dispositivo toma la hora de la
        plataforma (ADR-013), y el margen de un periodo solo absorbe el desfase
        si las dos mitades del par salen del mismo reloj (ADR-019 § 6.2): un
        móvil adelantado dos segundos dejaba toda la campaña sin pareja, o la
        emparejaba con otra muestra. El retraso de la red local queda muy por
        debajo de ese margen. La hora del navegador se guarda como referencia,
        con su desfase, para poder ver después si algún reloj iba mal.
        """
        taken_at = utc_now()
        declared = payload.get("takenAt")
        declared_at = parse_instant(declared, "takenAt") if declared else None
        device_id = require_text(payload.get("deviceId"), "deviceId", 80)
        margin = timedelta(milliseconds=PAIRING_MARGIN_MS)
        candidates = self.repository.device_samples_between(
            device_id,
            utc_instant_text(taken_at - margin),
            utc_instant_text(taken_at + margin),
        )
        pairing = pair_with_body_sample(taken_at, candidates)
        # Durante una extracción la preparación es obligatoria (ADR-019 § 4.3).
        # Si no se escribe, se toma de la muestra emparejada: es el dispositivo
        # quien sabe qué preparación corría, y teclear su identificador en mitad
        # de una extracción retrasaba la lectura, que es lo que se empareja.
        # Lo escrito prevalece, y la de la muestra queda en la pareja.
        declared_extraction = payload.get("extractionId")
        extraction_id = declared_extraction or pairing.get("sampleExtractionId")
        if payload["condition"] == "during-extraction" and not extraction_id:
            raise ValidationError(
                "una lectura durante la extracción debe indicar la preparación, y la "
                "máquina no informó ninguna en ese instante: escríbela"
            )
        recorded_at = iso_now()
        identifier = f"water-reference-{uuid4().hex[:12]}"
        document = {
            "id": identifier,
            "deviceId": device_id,
            "takenAt": utc_instant_text(taken_at),
            "takenAtSource": "platform",
            "declaredTakenAt": utc_instant_text(declared_at) if declared_at else None,
            "clockOffsetMs": (
                round((declared_at - taken_at).total_seconds() * 1000) if declared_at else None
            ),
            "waterTemperatureC": float(payload["waterTemperatureC"]),
            "unit": "Cel",
            "instrument": payload["instrument"],
            "condition": payload["condition"],
            "extractionId": extraction_id,
            "extractionIdSource": (
                "declared" if declared_extraction else "telemetry" if extraction_id else None
            ),
            "notes": payload.get("notes"),
            "pairing": pairing,
            "recordedBy": identity.subject,
            "recordedAt": recorded_at,
            "provenance": {
                "actor": identity.subject,
                "recordedAt": recorded_at,
                "source": "manual",
            },
        }
        return self.repository.add_document("water-reference", identifier, document)

    #: Columnas de la exportación de pares, en el orden en que se escriben. La
    #: cabecera sale de aquí mismo, para que no pueda describir otra fila.
    WATER_PAIR_COLUMNS = (
        "id",
        "deviceId",
        "condition",
        "extractionId",
        "extractionIdSource",
        "takenAt",
        "takenAtSource",
        "declaredTakenAt",
        "clockOffsetMs",
        "waterTemperatureC",
        "instrument",
        "pairingStatus",
        "pairingReason",
        "bodyTemperatureC",
        "sampleId",
        "sampleOccurredAt",
        "gapMs",
        "marginMs",
        "temperatureQuality",
        "sampleQuality",
        "bodySource",
        "recordedBy",
        "notes",
    )

    def water_reference_pairs(self) -> list[dict[str, Any]]:
        """Todas las lecturas del agua, aplanadas para ajustar la transformación.

        Van todas, con pareja o sin ella y sea cual sea la calidad del cuerpo:
        quien ajuste la transformación decide qué pares usa (ADR-019 § 4.3), y
        cada versión cita por identificador los que usó (§ 4.2). Se recorren
        todas las páginas en lugar de cortar en un tope, porque un par que
        faltara en silencio cambiaría el ajuste sin que nadie lo notara.
        """
        documents: list[dict[str, Any]] = []
        page = 1
        while True:
            result = self.repository.list_documents("water-reference", page=page, page_size=500)
            documents.extend(result.items)
            if page * 500 >= result.total or not result.items:
                break
            page += 1
        rows = []
        for document in sorted(documents, key=lambda item: (item["takenAt"], item["id"])):
            pairing = document.get("pairing") or {}
            row = {
                **{key: document.get(key) for key in self.WATER_PAIR_COLUMNS},
                "pairingStatus": pairing.get("status"),
                "pairingReason": pairing.get("reason"),
                "bodyTemperatureC": pairing.get("bodyTemperatureC"),
                "sampleId": pairing.get("sampleId"),
                "sampleOccurredAt": pairing.get("sampleOccurredAt"),
                "gapMs": pairing.get("gapMs"),
                "marginMs": pairing.get("marginMs"),
                "temperatureQuality": pairing.get("temperatureQuality"),
                "sampleQuality": pairing.get("quality"),
                # Sin origen declarado no es una medición (invariantes del proyecto).
                "bodySource": (pairing.get("source") or "unknown")
                if pairing.get("status") == "paired"
                else None,
            }
            rows.append(row)
        return rows

    def start_calibration(self, payload: dict[str, Any], identity: Identity) -> dict[str, Any]:
        coffee_id = require_text(payload.get("coffeeId"), "coffeeId", 80)
        self.get("coffee", coffee_id)
        profile_id = payload.get("profileId")
        profile = self.get("profile", profile_id) if profile_id else None
        started_at = iso_now()
        identifier = f"calibration-{uuid4().hex[:12]}"
        document = {
            "id": identifier,
            "status": "open",
            "coffeeId": coffee_id,
            "profileId": profile_id,
            "profileName": profile.get("name") if profile else None,
            "context": deepcopy(payload.get("context") or {}),
            "startedAt": started_at,
            "startedBy": identity.subject,
            "testExtractionIds": [],
            "adjustments": [],
            "events": [
                {
                    "sequence": 1,
                    "kind": "started",
                    "occurredAt": started_at,
                    "recordedBy": identity.subject,
                }
            ],
            "provenance": {
                "actor": identity.subject,
                "recordedAt": started_at,
                "source": "manual",
            },
        }
        return self.repository.add_document("calibration", identifier, document)

    def add_calibration_test(
        self, calibration_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        calibration = self._open_calibration(calibration_id)
        extraction_id = require_text(payload.get("extractionId"), "extractionId", 80)
        extraction = self.get("extraction", extraction_id)
        if extraction.get("coffeeId") != calibration.get("coffeeId"):
            raise ValidationError("la extracción de prueba debe usar el café de la calibración")
        test_ids = list(calibration.get("testExtractionIds") or [])
        if extraction_id in test_ids:
            raise ConflictError("la extracción ya pertenece a esta calibración")
        if test_ids:
            previous = self.get("extraction", test_ids[-1])
            if parse_instant(extraction.get("startedAt"), "extraction.startedAt") <= parse_instant(
                previous.get("startedAt"), "previous.startedAt"
            ):
                raise ValidationError("las extracciones de prueba deben añadirse en orden temporal")
        recorded_at = iso_now()
        test_ids.append(extraction_id)
        calibration["testExtractionIds"] = test_ids
        self._append_calibration_event(
            calibration,
            "test-added",
            recorded_at,
            identity.subject,
            {"extractionId": extraction_id},
        )
        return self.repository.upsert_document("calibration", calibration_id, calibration)

    def add_calibration_adjustment(
        self, calibration_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        calibration = self._open_calibration(calibration_id)
        if not calibration.get("testExtractionIds"):
            raise ValidationError("registra una extracción de prueba antes del ajuste")
        recorded_at = iso_now()
        adjustment = {
            **deepcopy(payload),
            "id": f"adjustment-{uuid4().hex[:12]}",
            "occurredAt": recorded_at,
            "performedBy": identity.subject,
        }
        calibration.setdefault("adjustments", []).append(adjustment)
        self._append_calibration_event(
            calibration,
            "adjustment-added",
            recorded_at,
            identity.subject,
            {"adjustmentId": adjustment["id"]},
        )
        return self.repository.upsert_document("calibration", calibration_id, calibration)

    def close_calibration(
        self, calibration_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        calibration = self._open_calibration(calibration_id)
        if not calibration.get("testExtractionIds"):
            raise ValidationError("la calibración requiere al menos una extracción de prueba")
        result = payload.get("result")
        profile_id = calibration.get("profileId")
        result_profile_id = profile_id
        if result in {"validated-profile", "new-profile-version"} and not profile_id:
            raise ValidationError("este resultado requiere un perfil previo")
        if result == "new-profile-version":
            previous = self.get("profile", str(profile_id))
            fields = {
                "name": previous["name"],
                "coffeeIds": previous["coffeeIds"],
                "specifications": previous["specifications"],
            }
            # Una versión anterior al campo no lo tiene; no se le inventa uno.
            if previous.get("purgeMaximumTimeMs") is not None:
                fields["purgeMaximumTimeMs"] = previous["purgeMaximumTimeMs"]
            version = self.create_profile_version(str(profile_id), fields, identity)
            result_profile_id = version["id"]
        finished_at = iso_now()
        calibration.update(
            {
                "status": "closed",
                "result": result,
                "rationale": payload.get("rationale"),
                "resultProfileId": result_profile_id,
                "finishedAt": finished_at,
                "closedBy": identity.subject,
            }
        )
        self._append_calibration_event(
            calibration,
            "closed",
            finished_at,
            identity.subject,
            {"result": result, "profileId": result_profile_id},
        )
        return self.repository.upsert_document("calibration", calibration_id, calibration)

    def _open_calibration(self, calibration_id: str) -> dict[str, Any]:
        calibration = self.get("calibration", calibration_id)
        if calibration.get("status") != "open":
            raise ConflictError("la calibración ya está cerrada")
        return calibration

    @staticmethod
    def _append_calibration_event(
        calibration: dict[str, Any],
        kind: str,
        occurred_at: str,
        actor: str,
        values: dict[str, Any],
    ) -> None:
        events = calibration.setdefault("events", [])
        events.append(
            {
                "sequence": len(events) + 1,
                "kind": kind,
                "occurredAt": occurred_at,
                "recordedBy": actor,
                **values,
            }
        )

    def evidence(self, resource_id: str) -> dict[str, Any]:
        document = {
            "resourceId": resource_id,
            "ontology": {"name": "EKO", "version": EKO_VERSION},
            "reasoner": {"name": "deterministic-test-adapter", "version": "1.0"},
            "status": "valid",
            "facts": [],
            "limits": "Grafo de prueba derivado de hechos registrados; no demuestra causalidad.",
        }
        try:
            document["facts"] = self.semantics.evidence_for(resource_id)
        except SemanticServiceUnavailable as error:
            # La ficha sigue siendo útil sin el grafo, pero debe decir que no
            # pudo consultarse en lugar de mostrar «sin evidencia».
            document["status"] = "unavailable"
            document["limits"] = str(error)
        return document

    def traceability(self, kind: str, identifier: str) -> dict[str, Any]:
        if kind not in {"barista", "profile", "extraction"}:
            raise ValidationError("origen de trazabilidad no soportado")
        profiles = [
            self._project_resource("profile", item)
            for item in self.repository.list_documents("profile", page=1, page_size=1000).items
        ]
        extractions = [
            self._project_resource("extraction", item)
            for item in self.repository.list_documents("extraction", page=1, page_size=1000).items
        ]
        episodes = self.repository.list_documents("episode", page=1, page_size=1000).items
        recommendations = self.repository.list_documents(
            "recommendation", page=1, page_size=1000
        ).items
        if kind == "profile":
            source = self.get("profile", identifier)
            root_extractions = [
                item for item in extractions if (item.get("profile") or {}).get("id") == identifier
            ]
            source_label = f"{source.get('name', identifier)} · v{source.get('version', 1)}"
            source_url = f"/profiles/{identifier}"
        elif kind == "extraction":
            source = self.get("extraction", identifier)
            root_extractions = [source]
            source_label = source.get("coffeeReference") or identifier
            source_url = f"/extractions/{identifier}"
        else:
            root_extractions = [
                item
                for item in extractions
                if item.get("barista") == identifier
                or (item.get("provenance") or {}).get("actor") == identifier
            ]
            source_label = identifier
            source_url = f"/traceability/barista/{identifier}"

        extraction_ids = {str(item["id"]) for item in root_extractions}
        linked_episodes = [
            episode
            for episode in episodes
            if episode.get("initialExtractionId") in extraction_ids
            or episode.get("followUpExtractionId") in extraction_ids
        ]
        extraction_ids.update(
            str(value)
            for episode in linked_episodes
            for value in (
                episode.get("initialExtractionId"),
                episode.get("followUpExtractionId"),
            )
            if value
        )
        related_extractions = [item for item in extractions if str(item["id"]) in extraction_ids]
        profile_ids = {
            str((item.get("profile") or {}).get("id"))
            for item in related_extractions
            if (item.get("profile") or {}).get("id")
        }
        linked_profiles = [item for item in profiles if str(item["id"]) in profile_ids]
        nodes: list[dict[str, Any]] = [
            {
                "id": f"{kind}:{identifier}",
                "kind": kind,
                "label": source_label,
                "summary": "Punto de partida de la consulta",
                "detailUrl": source_url,
                "occurredAt": None,
                "relatedTo": None,
            }
        ]
        if kind != "profile":
            nodes.extend(
                {
                    "id": f"profile:{profile['id']}",
                    "kind": "profile",
                    "label": f"{profile.get('name', profile['id'])} · v{profile.get('version', 1)}",
                    "summary": "Perfil usado en la preparación",
                    "detailUrl": f"/profiles/{profile['id']}",
                    "occurredAt": profile.get("createdAt"),
                    "relatedTo": None,
                }
                for profile in linked_profiles
            )
        comparisons = {
            str(item["extractionId"]): item
            for item in sorted(
                self.repository.list_documents("twin-comparison", page=1, page_size=1000).items,
                key=lambda item: item.get("createdAt") or "",
            )
            if item.get("extractionId") in extraction_ids
        }
        for extraction in related_extractions:
            extraction_id = str(extraction["id"])
            result = self.extraction_result(extraction_id)
            nodes.append(
                {
                    "id": f"extraction:{extraction_id}",
                    "kind": "extraction",
                    "label": extraction.get("coffeeReference") or extraction_id,
                    "summary": "Preparación registrada",
                    "detailUrl": f"/extractions/{extraction_id}",
                    "occurredAt": extraction.get("startedAt"),
                    "relatedTo": None,
                }
            )
            comparison = comparisons.get(extraction_id)
            if comparison is not None:
                nodes.append(
                    {
                        "id": f"twin-comparison:{comparison['comparisonId']}",
                        "kind": "simulation",
                        "label": f"Réplica simulada · {comparison['simulationRunId']}",
                        "summary": TRACE_VERDICTS.get(
                            comparison.get("verdict"), "Comparación con el modelo virtual"
                        ),
                        "detailUrl": f"/simulations/{comparison['simulationRunId']}",
                        "occurredAt": comparison.get("createdAt"),
                        "relatedTo": f"extraction:{extraction_id}",
                    }
                )
            nodes.append(
                {
                    "id": f"observation:{extraction_id}",
                    "kind": "observation",
                    "label": "Observaciones y mediciones",
                    "summary": f"{len(result.get('properties') or [])} variables revisadas",
                    "detailUrl": f"/extractions/{extraction_id}?tab=evidence",
                    "occurredAt": extraction.get("finishedAt"),
                    "relatedTo": f"extraction:{extraction_id}",
                }
            )
            sensory = extraction.get("sensoryEvaluation")
            if sensory:
                nodes.append(
                    {
                        "id": f"evaluation:{extraction_id}",
                        "kind": "evaluation",
                        "label": "Evaluación sensorial",
                        "summary": (sensory.get("acceptance") or {}).get("rationale")
                        or "Evaluación registrada",
                        "detailUrl": f"/extractions/{extraction_id}?tab=sensory",
                        "occurredAt": sensory.get("evaluatedAt"),
                        "relatedTo": f"extraction:{extraction_id}",
                    }
                )
            diagnosis = result.get("diagnosis") or {}
            nodes.append(
                {
                    "id": f"diagnosis:{extraction_id}",
                    "kind": "diagnosis",
                    "label": "Diagnóstico",
                    "summary": diagnosis.get("conclusion") or "Resultado técnico disponible",
                    "detailUrl": f"/extractions/{extraction_id}?tab=summary",
                    "occurredAt": diagnosis.get("generatedAt") or extraction.get("finishedAt"),
                    "relatedTo": f"observation:{extraction_id}",
                }
            )
            nodes.append(
                {
                    "id": f"result:{extraction_id}",
                    "kind": "result",
                    "label": "Resultado de la preparación",
                    "summary": {
                        "conformant": "Dentro de los objetivos",
                        "non-conformant": "Fuera de los objetivos",
                        "insufficient-information": "Sin datos suficientes",
                    }.get(
                        result.get("technicalConformance", {}).get("status"),
                        "Disponible",
                    ),
                    "detailUrl": f"/extractions/{extraction_id}?tab=summary",
                    "occurredAt": extraction.get("finishedAt"),
                    "relatedTo": f"diagnosis:{extraction_id}",
                }
            )
            for recommendation in recommendations:
                if recommendation.get("extractionId") != extraction_id:
                    continue
                recommendation_id = str(recommendation["recommendationId"])
                explanation = self.repository.get_document("explanation", recommendation_id)
                decision = self.repository.get_document(
                    "recommendation-decision", recommendation_id
                )
                if explanation:
                    nodes.append(
                        {
                            "id": f"explanation:{recommendation_id}",
                            "kind": "explanation",
                            "label": "Explicación",
                            "summary": explanation.get("summary"),
                            "detailUrl": f"/recommendations/{recommendation_id}",
                            "occurredAt": explanation.get("generatedAt")
                            or recommendation.get("createdAt"),
                            "relatedTo": f"diagnosis:{extraction_id}",
                        }
                    )
                nodes.append(
                    {
                        "id": f"recommendation:{recommendation_id}",
                        "kind": "recommendation",
                        "label": "Recomendación",
                        "summary": recommendation.get("summary"),
                        "detailUrl": f"/recommendations/{recommendation_id}",
                        "occurredAt": recommendation.get("createdAt"),
                        "relatedTo": f"diagnosis:{extraction_id}",
                    }
                )
                if decision:
                    nodes.append(
                        {
                            "id": f"decision:{decision['id']}",
                            "kind": "decision",
                            "label": "Decisión del barista",
                            "summary": decision.get("comment") or decision.get("status"),
                            "detailUrl": f"/recommendations/{recommendation_id}",
                            "occurredAt": decision.get("decidedAt"),
                            "relatedTo": f"recommendation:{recommendation_id}",
                        }
                    )
        for episode in linked_episodes:
            adjustment = episode.get("adjustment") or {}
            nodes.append(
                {
                    "id": f"adjustment:{adjustment.get('id', episode['id'])}",
                    "kind": "adjustment",
                    "label": "Ajuste registrado",
                    "summary": adjustment.get("rationale") or adjustment.get("property"),
                    "detailUrl": f"/episodes/{episode['id']}",
                    "occurredAt": adjustment.get("occurredAt") or episode.get("createdAt"),
                    "relatedTo": f"extraction:{episode.get('initialExtractionId')}",
                }
            )
        deduplicated = {node["id"]: node for node in nodes}
        ordered = sorted(
            deduplicated.values(),
            key=lambda node: (node.get("occurredAt") is not None, node.get("occurredAt") or ""),
        )
        return {
            "start": {"kind": kind, "id": identifier, "label": source_label},
            "nodes": ordered,
        }

    def recommend(self, extraction_id: str, identity: Identity) -> dict[str, Any]:
        extraction = self.get("extraction", extraction_id)
        evidence = self.semantics.evidence_for(extraction_id)
        # Solo una preparación medida y completa dice algo de la máquina
        #: con las demás la fuente recibe nada y se abstiene.
        # Una pasada de agua archivada tampoco: no hubo café que juzgar.
        reasonable = (
            extraction.get("source") == "measured"
            and not interrupted_by_power_loss(extraction)
            and extraction.get("purpose") != "water-pass"
        )
        recommendation = self.recommender.recommend(
            extraction_id,
            evidence,
            lambda: self._reasoning_graph(extraction) if reasonable else None,
        )
        if recommendation.get("actuatorCommand") is not None:
            raise ConflictError("una recomendación no puede contener comandos de actuación")
        self._validate_learning_output(recommendation, extraction)
        self._validate_reasoner_output(recommendation, extraction)
        recommendation["createdBy"] = identity.subject
        return self.repository.add_document(
            "recommendation", recommendation["recommendationId"], recommendation
        )

    @staticmethod
    def _validate_reasoner_output(
        recommendation: dict[str, Any], extraction: dict[str, Any]
    ) -> None:
        if recommendation.get("origin") != "reasoner":
            return
        if recommendation.get("status") == "abstained":
            if recommendation.get("proposedAdjustment") is not None:
                raise ConflictError("una abstención no propone ajustes")
            return
        if recommendation.get("status") != "pending-human-review":
            raise ConflictError("la propuesta del razonador requiere revisión humana")
        if recommendation.get("requiresHumanReview") is not True:
            raise ConflictError("la propuesta del razonador requiere revisión humana")
        if extraction.get("source") != "measured":
            raise ConflictError("el razonador solo propone sobre una preparación medida")

    @staticmethod
    def _validate_learning_output(
        recommendation: dict[str, Any], extraction: dict[str, Any]
    ) -> None:
        if recommendation.get("origin") != "machine-learning":
            return
        model = recommendation.get("model") or {}
        approved = model.get("approvalStatus") == "approved"
        if not approved:
            if (
                recommendation.get("status") != "abstained"
                or recommendation.get("proposedAdjustment") is not None
                or recommendation.get("predictedSensoryProfile") is not None
            ):
                raise ConflictError("un modelo no aprobado solo puede abstenerse")
            return
        require_text(model.get("name"), "model.name")
        require_text(model.get("version"), "model.version")
        if extraction.get("source") != "measured":
            raise ConflictError("un modelo aprobado solo puede usar una preparación medida")
        if recommendation.get("status") != "pending-human-review":
            raise ConflictError("la salida del modelo aprobado requiere revisión humana")
        if recommendation.get("requiresHumanReview") is not True:
            raise ConflictError("la recomendación del modelo requiere revisión humana")
        require_text(recommendation.get("applicability"), "applicability")
        uncertainty = recommendation.get("uncertainty") or {}
        if not isinstance(uncertainty.get("value"), int | float):
            raise ValidationError("la salida del modelo requiere incertidumbre")
        require_text(uncertainty.get("methodReference"), "uncertainty.methodReference")
        provenance = recommendation.get("provenance") or {}
        require_text(provenance.get("source"), "provenance.source")
        predicted = recommendation.get("predictedSensoryProfile")
        if predicted is not None:
            if predicted.get("coffeeId") != extraction.get("coffeeId"):
                raise ConflictError("la estimación debe describir el café de la preparación")
            descriptors = predicted.get("descriptors")
            if not isinstance(descriptors, list) or not descriptors:
                raise ValidationError("la estimación requiere descriptores separados")

    def decide(
        self, recommendation_id: str, payload: dict[str, Any], identity: Identity
    ) -> dict[str, Any]:
        recommendation = self.get("recommendation", recommendation_id)
        if self.repository.get_document("recommendation-decision", recommendation_id):
            raise ConflictError("la recomendación ya tiene una decisión inmutable")
        status = payload.get("status")
        if status not in {"accepted", "rejected", "modified"}:
            raise ValidationError("status de decisión no soportado")
        decision = {
            "id": f"decision-{uuid4().hex[:12]}",
            "recommendationId": recommendation_id,
            "status": status,
            "comment": payload.get("comment"),
            "modifiedAdjustment": payload.get("modifiedAdjustment"),
            "decidedBy": identity.subject,
            "decidedAt": iso_now(),
        }
        self.repository.add_document("recommendation-decision", recommendation_id, decision)
        return {**recommendation, "decision": decision}

    def explain(self, recommendation_id: str) -> dict[str, Any]:
        saved = self.repository.get_document("explanation", recommendation_id)
        if saved is not None:
            return saved
        recommendation = self.get("recommendation", recommendation_id)
        evidence = self.semantics.evidence_for(recommendation.get("extractionId", ""))
        explanation = self.explainer.explain(recommendation, evidence)
        cited = set(explanation.get("evidenceIds", []))
        known = {item["id"] for item in evidence}
        if not cited or not cited.issubset(known):
            raise ConflictError("la explicación contiene citas inexistentes")
        return self.repository.add_document("explanation", recommendation_id, explanation)
