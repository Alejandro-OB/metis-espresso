from __future__ import annotations

from datetime import datetime
from typing import Annotated, Any, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


ShortText = Annotated[str, Field(min_length=1, max_length=120)]
SensoryDescriptor = Literal[
    "Citrus",
    "RedFruit",
    "TropicalFruit",
    "RipeFruit",
    "DriedFruit",
    "Caramel",
    "Honey",
    "Panela",
    "Chocolate",
    "Vanilla",
    "GenericFloral",
    "Jasmine",
    "Rose",
    "Nuts",
    "Spices",
    "Cereal",
    "Herbal",
    "FermentedDescriptor",
    # Los de la ficha CVA que faltaban (EKO 3.5.0).
    "BrownSugar",
    "Cocoa",
    "Tobacco",
    "Earthy",
    "Woody",
]


class DeclaredSensoryProfile(StrictModel):
    id: str = Field(min_length=1, max_length=80)
    declaredBy: str = Field(min_length=1, max_length=200)
    source: str = Field(min_length=1, max_length=500)
    originalText: str = Field(min_length=1, max_length=2000)
    descriptors: list[SensoryDescriptor] = Field(min_length=1)

    @field_validator("descriptors")
    @classmethod
    def unique_descriptors(cls, values: list[SensoryDescriptor]) -> list[SensoryDescriptor]:
        if len(values) != len(set(values)):
            raise ValueError("descriptors no admite valores repetidos")
        return values


class DeclaredSensoryProfileInput(StrictModel):
    declaredBy: str = Field(min_length=1, max_length=200)
    source: str = Field(min_length=1, max_length=500)
    originalText: str = Field(min_length=1, max_length=2000)
    descriptors: list[SensoryDescriptor] = Field(min_length=1, max_length=30)

    @field_validator("descriptors")
    @classmethod
    def unique_descriptors(cls, values: list[SensoryDescriptor]) -> list[SensoryDescriptor]:
        if len(values) != len(set(values)):
            raise ValueError("descriptors no admite valores repetidos")
        return values


class SyncRequest(StrictModel):
    deviceId: str = Field(min_length=1, max_length=120)
    messages: list[dict[str, Any]] = Field(max_length=64)

    @model_validator(mode="after")
    def messages_belong_to_device(self) -> SyncRequest:
        if any(message.get("deviceId") != self.deviceId for message in self.messages):
            raise ValueError("todos los mensajes deben pertenecer a deviceId")
        return self


class SimulationModelReference(StrictModel):
    id: str = Field(min_length=1, max_length=80)
    version: str = Field(pattern=r"^[0-9]+\.[0-9]+\.[0-9]+$")


class SimulationControllerReference(StrictModel):
    """Controlador registrado: `profile-tracking` (planta virtual) o
    `board-replica` (réplica del control de la placa, solo al replicar)."""

    id: Literal["profile-tracking", "board-replica"] = "profile-tracking"
    version: Literal["1.0.0"] = "1.0.0"


class SimulationModelTolerances(StrictModel):
    temperatureC: float | None = Field(default=None, gt=0)
    pressureBar: float | None = Field(default=None, gt=0)


class SimulationReplicates(StrictModel):
    """Preparación que la simulación reproduce; activa el modo réplica."""

    extractionId: str = Field(min_length=1, max_length=80)
    modelTolerances: SimulationModelTolerances | None = None
    minimumCoverageRatio: float | None = Field(default=None, ge=0, le=1)


class SimulationCreate(StrictModel):
    schemaVersion: Literal["1.0"] = "1.0"
    model: SimulationModelReference
    profileId: str | None = Field(default=None, min_length=1, max_length=80)
    durationSeconds: float | None = Field(default=None, gt=0)
    controller: SimulationControllerReference = Field(default_factory=SimulationControllerReference)
    replicates: SimulationReplicates | None = None


class TwinComparisonRequest(StrictModel):
    """Solicitud de comparación entre una preparación y su réplica simulada."""

    model: SimulationModelReference | None = None
    controller: SimulationControllerReference | None = None
    modelTolerances: SimulationModelTolerances | None = None
    minimumCoverageRatio: float | None = Field(default=None, ge=0, le=1)


class DashboardSensor(StrictModel):
    id: str
    label: str
    status: Literal["available", "unavailable"]
    value: float | None = None
    unit: str


class DashboardPendingAction(StrictModel):
    kind: Literal["coffee", "measurements", "sensory", "calibration"]
    label: str
    extractionId: str


class DashboardResponse(StrictModel):
    activeProfile: dict[str, Any] | None
    activeCoffee: dict[str, Any] | None
    activeEquipment: dict[str, str | None]
    sensors: list[DashboardSensor]
    lastReadingAt: str | None
    lastReadingDeviceId: str | None = None
    lastCalibration: dict[str, Any] | None
    pendingActions: list[DashboardPendingAction]
    recentExtractions: list[dict[str, Any]]


CoffeeOptionalField = Literal[
    "commercialReference",
    "lotIdentifier",
    "bagIdentifier",
    "variety",
    "postHarvestProcess",
    "roastLevel",
    "roastDate",
    "region",
    "altitudeM",
    "soilCharacteristics",
    "mineralComposition",
    "mineralSource",
]


class EquipmentCapability(StrictModel):
    property: ShortText
    manualAdjustment: bool = False
    automaticAdjustment: bool = False

    @model_validator(mode="after")
    def declared_capability(self) -> EquipmentCapability:
        if not self.manualAdjustment and not self.automaticAdjustment:
            raise ValueError("declara al menos una capacidad para la propiedad")
        return self


class EquipmentSensor(StrictModel):
    id: str = Field(min_length=1, max_length=80)
    measuredProperty: ShortText
    location: ShortText
    accuracy: float = Field(gt=0)
    accuracyUnit: ShortText
    samplingFrequencyHz: float = Field(gt=0)
    lastCalibrationAt: datetime


class EquipmentCreate(StrictModel):
    id: str = Field(min_length=1, max_length=80)
    type: Literal["machine", "grinder"]
    name: ShortText
    location: ShortText
    capabilities: list[EquipmentCapability] = Field(default_factory=list)
    sensors: list[EquipmentSensor] = Field(default_factory=list)

    @model_validator(mode="after")
    def unique_entries(self) -> EquipmentCreate:
        properties = [item.property for item in self.capabilities]
        sensor_ids = [item.id for item in self.sensors]
        if len(properties) != len(set(properties)):
            raise ValueError("cada propiedad puede declarar una sola capacidad")
        if len(sensor_ids) != len(set(sensor_ids)):
            raise ValueError("los identificadores de sensor deben ser únicos")
        return self


class CoffeeCreate(StrictModel):
    id: str | None = Field(default=None, min_length=1, max_length=80)
    commercialReference: str | None = Field(default=None, max_length=200)
    lotIdentifier: str | None = Field(default=None, max_length=120)
    bagIdentifier: str | None = Field(default=None, max_length=120)
    variety: str | None = Field(default=None, max_length=120)
    postHarvestProcess: str | None = Field(default=None, max_length=120)
    roastLevel: str | None = Field(default=None, max_length=120)
    roastDate: datetime | None = None
    region: str | None = Field(default=None, max_length=200)
    altitudeM: float | None = Field(default=None, ge=-500, le=6000)
    soilCharacteristics: str | None = Field(default=None, max_length=1000)
    mineralComposition: str | None = Field(default=None, max_length=1000)
    mineralSource: str | None = Field(default=None, max_length=500)
    notApplicableFields: list[CoffeeOptionalField] = Field(default_factory=list)
    declaredSensoryProfile: DeclaredSensoryProfile | None = None

    @field_validator(
        "commercialReference",
        "lotIdentifier",
        "bagIdentifier",
        "variety",
        "postHarvestProcess",
        "roastLevel",
        "region",
        "soilCharacteristics",
        "mineralComposition",
        "mineralSource",
        mode="before",
    )
    @classmethod
    def empty_text_is_unknown(cls, value: Any) -> Any:
        return None if isinstance(value, str) and not value.strip() else value

    @model_validator(mode="after")
    def validate_optional_information(self) -> CoffeeCreate:
        identifiers = (self.commercialReference, self.lotIdentifier, self.bagIdentifier)
        if not any(value and value.strip() for value in identifiers):
            raise ValueError("indica al menos una referencia comercial, lote o bolsa")
        if len(self.notApplicableFields) != len(set(self.notApplicableFields)):
            raise ValueError("notApplicableFields no admite valores repetidos")
        for field in self.notApplicableFields:
            if getattr(self, field) is not None:
                raise ValueError(f"{field} no puede tener valor y marcarse como no aplicable")
        if self.mineralComposition and not self.mineralSource:
            raise ValueError("la composición mineral requiere una fuente")
        if self.mineralSource and not self.mineralComposition:
            raise ValueError("la fuente mineral requiere una composición mineral")
        return self


class CoffeeUpdate(CoffeeCreate):
    """Los datos del café, corregidos después de registrarlo.

    El identificador y la descripción declarada no se tocan aquí: el primero
    es la referencia de las preparaciones y la segunda tiene su propia ruta.
    """

    id: None = None
    declaredSensoryProfile: None = None


class TargetSensoryProfile(StrictModel):
    id: str = Field(min_length=1, max_length=80)
    definedBy: str | None = Field(default=None, min_length=1, max_length=120)
    rationale: str = Field(min_length=1, max_length=2000)
    descriptors: list[ShortText] = Field(min_length=1)

    @field_validator("descriptors")
    @classmethod
    def unique_descriptors(cls, values: list[str]) -> list[str]:
        if len(values) != len(set(values)):
            raise ValueError("descriptors no admite valores repetidos")
        return values


class TastingSession(StrictModel):
    """De qué sesión de catación es la preparación (protocolo de catación § 3, § 4).

    Todo opcional y por partes, como el resto del contexto: el código ciego se
    asigna al empezar el día y la condición la conoce el operador, no el barista.
    No se valida que la condición sea una de las del protocolo: el protocolo es
    un borrador y una lista cerrada aquí rechazaría una condición nueva el día
    de la sesión.
    """

    sessionCode: str | None = Field(default=None, min_length=1, max_length=40)
    blindCode: str | None = Field(default=None, pattern=r"^[A-Z]{3}$")
    block: Literal["A", "B", "C", "D", "E"] | None = None
    condition: str | None = Field(default=None, min_length=1, max_length=120)

    @field_validator("sessionCode", "condition", mode="before")
    @classmethod
    def empty_text_is_unknown(cls, value: Any) -> Any:
        if isinstance(value, str):
            value = value.strip()
            return value or None
        return value

    @field_validator("blindCode", mode="before")
    @classmethod
    def blind_code_in_capitals(cls, value: Any) -> Any:
        # Tres letras escritas a mano en una taza: «abc» y «ABC» son el mismo
        # código, y guardarlos distintos partiría la misma taza en dos.
        if isinstance(value, str):
            value = value.strip().upper()
            return value or None
        return value

    @model_validator(mode="after")
    def has_a_field(self) -> TastingSession:
        if all(
            value is None
            for value in (self.sessionCode, self.blindCode, self.block, self.condition)
        ):
            raise ValueError("indica al menos un dato de la sesión de catación")
        return self


class ExtractionContext(StrictModel):
    """Lo que pone el barista sobre una preparación, antes o después de hacerla.

    Todo es opcional: se corrige lo que se manda y lo demás queda como estaba.
    Lo que midió la máquina no está aquí y no se puede tocar.
    """

    coffeeId: str | None = Field(default=None, min_length=1, max_length=80)
    targetSensoryProfile: TargetSensoryProfile | None = None
    machineId: str | None = Field(default=None, min_length=1, max_length=80)
    grinderId: str | None = Field(default=None, min_length=1, max_length=80)
    grindSetting: float | None = Field(default=None, ge=0, le=1000)
    notes: str | None = Field(default=None, max_length=2000)
    # El cuarto de una preparación real lo anota quien prepara (sonda R del
    # protocolo de catación, § 3.2): la placa no lo mide. Los mismos rangos que
    # la preparación simulada, para que las dos se lean con la misma regla.
    ambientTemperatureC: float | None = Field(default=None, ge=-20, le=60)
    ambientHumidityPercent: float | None = Field(default=None, ge=0, le=100)
    tastingSession: TastingSession | None = None
    # Con qué se hizo la preparación. El gemelo la replica con la carga
    # hidráulica de esa configuración: con agua sola y sin canastilla pasan 68 g
    # a 3,4 g/s y ~7 bar, con café la presión sube a la referencia y el caudal
    # baja, y el agua fría que entra a la caldera cambia con ello (2026-10-22).
    # Sin declarar, la réplica no supone ninguna. La canastilla no presurizada
    # se registra para la sesión de ampliación aunque ningún modelo la tenga
    # todavía: la réplica la rechaza en vez de hacerla pasar por la presurizada.
    portafilterConfiguration: (
        Literal["pressurized-double-coffee", "non-pressurized-double-coffee", "no-basket-water"]
        | None
    ) = None
    # Para qué fue el uso de la máquina (4-nov-2026). En la catación varias
    # pasadas de agua de calentamiento quedaron tomadas por Preparar café como
    # cafés del barista. Marcarlas las archiva: el barista deja de verlas y el
    # razonador no las evalúa, pero no se borran, porque el agua pasó por el
    # grupo y el modelo del primer uso la cuenta. Sin declarar no se supone
    # nada: un uso sin marca se trata como hasta ahora.
    purpose: Literal["coffee", "water-pass"] | None = None


class ManualMeasurements(StrictModel):
    coffeeDoseG: float | None = Field(default=None, gt=0, le=100)
    beverageMassG: float | None = Field(default=None, gt=0, le=300)
    tdsPercent: float | None = Field(default=None, gt=0, le=30)
    # El protocolo pide tres lecturas de la misma muestra y registra su media
    # (§ 5). Con las lecturas, la media la calcula la plataforma y se guardan
    # las dos cosas: una media sin lecturas no deja ver la dispersión.
    tdsReadings: list[Annotated[float, Field(gt=0, le=30)]] | None = Field(
        default=None, min_length=1, max_length=3
    )
    instrument: str | None = Field(default=None, max_length=200)

    @model_validator(mode="after")
    def has_a_manual_entry(self) -> ManualMeasurements:
        if all(
            value is None
            for value in (
                self.coffeeDoseG,
                self.beverageMassG,
                self.tdsPercent,
                self.tdsReadings,
                self.instrument,
            )
        ):
            raise ValueError("registra al menos un dato manual")
        # Las dos a la vez podrían no cuadrar y no habría cómo saber cuál vale.
        if self.tdsReadings is not None and self.tdsPercent is not None:
            raise ValueError("envía las lecturas de TDS o su media, no las dos")
        return self


SensoryAttribute = Literal[
    "Fragrance",
    "Aroma",
    "Flavor",
    "Acidity",
    "Sweetness",
    "Bitterness",
    "Body",
    "Aftertaste",
    "Astringency",
    "Balance",
    "OverallLiking",
    "Mouthfeel",
]


class SensoryScore(StrictModel):
    attribute: SensoryAttribute
    dimension: Literal["perceived-intensity", "hedonic-liking"]
    value: int

    @model_validator(mode="after")
    def validate_scale(self) -> SensoryScore:
        # Intensidad de 0 a 15 y agrado de 1 a 9, como la ficha CVA de la SCA
        # (EKO 3.5.0); la intensidad iba de 0 a 10.
        lower, upper = (0, 15) if self.dimension == "perceived-intensity" else (1, 9)
        if not lower <= self.value <= upper:
            raise ValueError(f"value fuera del intervalo {lower}–{upper}")
        if self.attribute in {"Balance", "OverallLiking"} and self.dimension != "hedonic-liking":
            raise ValueError("balance y agrado global solo admiten agrado")
        return self


class SensoryDescriptorRating(StrictModel):
    descriptor: SensoryDescriptor
    intensity: int = Field(ge=0, le=15)


class OtherSensoryDescriptor(StrictModel):
    """Un descriptor que no está en la lista, escrito como lo dijo el barista.

    Como el campo de notas de la ficha CVA (23-oct-2026): «azúcar morena en el
    retrogusto» no cabe en ninguna casilla y forzarla a la más cercana
    afirmaría algo que no dijo. Se guarda tal cual y no pasa al grafo: el
    razonador solo trabaja con el vocabulario de la ontología.
    """

    name: str = Field(min_length=1, max_length=60)
    intensity: int = Field(ge=0, le=15)

    @field_validator("name", mode="before")
    @classmethod
    def trimmed(cls, value: object) -> object:
        return value.strip() if isinstance(value, str) else value


class SensoryDesiredChange(StrictModel):
    attribute: SensoryAttribute
    direction: Literal["Increase", "Decrease", "Improve"]


class SensoryAcceptance(StrictModel):
    status: Literal["accepted", "rejected", "inconclusive"]
    rationale: str | None = Field(default=None, min_length=1, max_length=2000)

    @model_validator(mode="after")
    def rejected_requires_rationale(self) -> SensoryAcceptance:
        if self.status == "rejected" and not self.rationale:
            raise ValueError("una evaluación rechazada requiere rationale")
        return self


class SensoryEvaluation(StrictModel):
    id: str = Field(min_length=1, max_length=80)
    evaluatedBy: str | None = Field(default=None, max_length=120)
    evaluatedAt: str
    protocolReference: str | None = Field(default=None, max_length=500)
    # Observaciones de la evaluación, no de la preparación (protocolo § 5): la
    # misma taza sabe distinto caliente que tibia. Sin termómetro o sin reloj
    # quedan vacías; no se deducen de la hora de la evaluación.
    servingTemperatureC: float | None = Field(default=None, ge=0, le=100)
    minutesSinceExtraction: float | None = Field(default=None, ge=0)
    descriptors: list[SensoryDescriptor] = Field(default_factory=list)
    descriptorRatings: list[SensoryDescriptorRating] = Field(default_factory=list)
    otherDescriptors: list[OtherSensoryDescriptor] = Field(default_factory=list, max_length=5)
    defects: list[
        Literal[
            "Burnt",
            "ExcessiveAstringency",
            "ExcessiveBitterness",
            "ExcessiveAcidity",
            "InsufficientAcidity",
            "FermentedDefect",
            "Vegetal",
            "Ashy",
            "Metallic",
            "PersistentUnpleasantTaste",
            # Los tres de la ficha CVA (EKO 3.5.0).
            "Moldy",
            "Phenolic",
            "Potato",
        ]
    ] = Field(default_factory=list)
    # Lo que la ficha CVA marca aparte, hasta dos de cada uno (EKO 3.5.0): los
    # sabores básicos que dominan y el tipo de sensación en boca.
    mainTastes: list[Literal["Salty", "Bitter", "Sour", "Umami", "Sweet"]] = Field(
        default_factory=list, max_length=2
    )
    mouthfeelTextures: list[
        Literal["Rough", "Smooth", "Oily", "Metallic", "MouthDrying"]
    ] = Field(default_factory=list, max_length=2)
    desiredChanges: list[SensoryDesiredChange] = Field(default_factory=list)
    comparisons: list[Literal["declared-profile", "barista-target"]] = Field(default_factory=list)
    scores: list[SensoryScore] = Field(min_length=1)
    acceptance: SensoryAcceptance
    comment: str | None = Field(default=None, max_length=4000)

    @field_validator("descriptors", "defects", "comparisons", "mainTastes", "mouthfeelTextures")
    @classmethod
    def unique_terms(cls, values: list[str]) -> list[str]:
        if len(values) != len(set(values)):
            raise ValueError("la lista no admite valores repetidos")
        return values

    @field_validator("descriptorRatings")
    @classmethod
    def unique_descriptor_ratings(
        cls, values: list[SensoryDescriptorRating]
    ) -> list[SensoryDescriptorRating]:
        descriptors = [value.descriptor for value in values]
        if len(descriptors) != len(set(descriptors)):
            raise ValueError("cada descriptor puede calificarse una sola vez")
        return values

    @field_validator("desiredChanges")
    @classmethod
    def unique_desired_changes(
        cls, values: list[SensoryDesiredChange]
    ) -> list[SensoryDesiredChange]:
        attributes = [value.attribute for value in values]
        if len(attributes) != len(set(attributes)):
            raise ValueError("cada atributo puede tener un solo cambio deseado")
        return values

    @model_validator(mode="after")
    def ratings_match_descriptors(self) -> SensoryEvaluation:
        if {rating.descriptor for rating in self.descriptorRatings} != set(self.descriptors):
            raise ValueError("cada descriptor debe tener una intensidad")
        names = [item.name.casefold() for item in self.otherDescriptors]
        if len(names) != len(set(names)):
            raise ValueError("cada descriptor escrito puede aparecer una sola vez")
        return self

    @field_validator("scores")
    @classmethod
    def unique_scores(cls, values: list[SensoryScore]) -> list[SensoryScore]:
        combinations = {(value.attribute, value.dimension) for value in values}
        if len(values) != len(combinations):
            raise ValueError("no se admite repetir atributo y dimensión")
        return values


class ProfileActivation(StrictModel):
    """Vía por la que una configuración pasa a regir, según ADR-017 § 4.1."""

    via: Literal["selection", "edition", "recommendation"] = "selection"
    recommendationId: str | None = Field(default=None, max_length=80)


class ToleranceFromExtractions(StrictModel):
    """Los cafés repetidos de los que sale la tolerancia de una receta."""

    extractionIds: list[str] = Field(min_length=2, max_length=12)
    apply: bool = False


class SustainmentExclusion(StrictModel):
    """El barista saca una configuración del sostenimiento (ADR-018 § 4.3)."""

    reason: str | None = Field(default=None, max_length=280)


class SustainmentSuspension(StrictModel):
    """El protocolo suspende el sostenimiento mientras dure una serie comparable."""

    reason: str = Field(min_length=1, max_length=280)


class Adjustment(StrictModel):
    id: str | None = Field(default=None, max_length=80)
    performedBy: str | None = Field(default=None, max_length=120)
    property: Literal[
        "water-temperature",
        "extraction-time",
        "grind-setting",
        "coffee-dose",
        "brew-ratio",
        "pressure",
    ]
    previousValue: float
    newValue: float
    unit: str = Field(min_length=1, max_length=40)
    occurredAt: str
    rationale: str | None = Field(default=None, max_length=2000)
    recommendationId: str | None = Field(default=None, max_length=80)

    @model_validator(mode="after")
    def value_changes(self) -> Adjustment:
        if self.previousValue == self.newValue:
            raise ValueError("el ajuste debe cambiar el valor")
        return self


class TargetOutcome(StrictModel):
    kind: Literal["sensory-defect", "observable-property"]
    code: str = Field(min_length=1, max_length=120)


class ObservedOutcome(StrictModel):
    category: Literal[
        "expected-observed",
        "insufficient-observed",
        "adverse-observed",
        "inconclusive",
    ]
    assessedBy: str | None = Field(default=None, max_length=120)
    assessedAt: str
    rationale: str = Field(min_length=1, max_length=2000)


class EpisodeCreate(StrictModel):
    id: str | None = Field(default=None, max_length=80)
    coffeeId: str = Field(min_length=1, max_length=80)
    initialExtractionId: str = Field(min_length=1, max_length=80)
    followUpExtractionId: str = Field(min_length=1, max_length=80)
    adjustment: Adjustment
    targetOutcome: TargetOutcome
    outcome: ObservedOutcome


class AdjustmentRegistration(StrictModel):
    initialExtractionId: str = Field(min_length=1, max_length=80)
    adjustment: Adjustment
    targetOutcome: TargetOutcome


class EpisodeFollowUpLink(StrictModel):
    """El café real con que el barista comprueba un ajuste (23-oct-2026)."""

    extractionId: str = Field(min_length=1, max_length=80)


class EpisodeOutcomeCreate(StrictModel):
    category: Literal[
        "expected-observed",
        "insufficient-observed",
        "adverse-observed",
        "inconclusive",
    ]
    rationale: str = Field(min_length=1, max_length=2000)


class CalibrationContext(StrictModel):
    station: str | None = Field(default=None, max_length=120)
    machineId: str | None = Field(default=None, max_length=80)
    grinderId: str | None = Field(default=None, max_length=80)
    notes: str | None = Field(default=None, max_length=1000)


class CalibrationStart(StrictModel):
    coffeeId: str = Field(min_length=1, max_length=80)
    profileId: str | None = Field(default=None, max_length=80)
    context: CalibrationContext = Field(default_factory=CalibrationContext)


class CalibrationTestAdd(StrictModel):
    extractionId: str = Field(min_length=1, max_length=80)


class CalibrationAdjustmentAdd(StrictModel):
    property: Literal[
        "water-temperature",
        "extraction-time",
        "grind-setting",
        "coffee-dose",
        "brew-ratio",
        "pressure",
    ]
    previousValue: float
    newValue: float
    unit: str = Field(min_length=1, max_length=40)
    rationale: str = Field(min_length=1, max_length=2000)

    @model_validator(mode="after")
    def value_changes(self) -> CalibrationAdjustmentAdd:
        if self.previousValue == self.newValue:
            raise ValueError("el ajuste debe cambiar el valor")
        return self


class CalibrationClose(StrictModel):
    result: Literal["validated-profile", "new-profile-version", "rejected"]
    rationale: str = Field(min_length=1, max_length=2000)


class WaterReferenceReading(StrictModel):
    """Lectura del agua de salida con un termómetro de referencia (ADR-019 § 4.3).

    La temperatura del cuerpo no es un campo: sale de la serie del dispositivo.
    Admitirla desde la petición la separaría de la evidencia que la respalda.

    ``takenAt`` es opcional y solo se guarda como referencia: el instante que
    empareja lo sella la plataforma al recibir la lectura (ADR-019 § 6.2).
    """

    deviceId: str = Field(min_length=1, max_length=80)
    takenAt: datetime | None = None
    waterTemperatureC: float = Field(ge=0, le=110)
    instrument: str = Field(min_length=1, max_length=200)
    condition: Literal["rest", "after-extraction", "during-extraction"]
    extractionId: str | None = Field(default=None, max_length=80)
    notes: str | None = Field(default=None, max_length=1000)

    @model_validator(mode="after")
    def declared_instant_has_its_zone(self) -> WaterReferenceReading:
        # Que una lectura durante la extracción nombre su preparación se comprueba
        # al registrarla: puede tomarse de la muestra emparejada, que aquí aún no
        # se conoce.
        if self.takenAt is not None and self.takenAt.tzinfo is None:
            raise ValueError("takenAt debe incluir zona horaria")
        return self


class LiveExtractionStart(StrictModel):
    coffeeId: str = Field(min_length=1, max_length=80)
    profileId: str = Field(min_length=1, max_length=80)
    ambientTemperatureC: float | None = Field(default=None, ge=-20, le=60)
    ambientHumidityPercent: float | None = Field(default=None, ge=0, le=100)
    machineId: str | None = Field(default=None, max_length=80)
    grinderId: str | None = Field(default=None, max_length=80)
    calibrationId: str | None = Field(default=None, max_length=80)


class LiveExtractionFinish(StrictModel):
    cause: Literal[
        "completed",
        "barista-decision",
        "acquisition-lost",
        "unexpected-interruption",
        "other",
    ]
    detail: str | None = Field(default=None, max_length=1000)

    @model_validator(mode="after")
    def other_needs_detail(self) -> LiveExtractionFinish:
        if self.cause == "other" and not (self.detail or "").strip():
            raise ValueError("la causa otra requiere una descripción")
        return self


class ExtractionAlertPreferences(StrictModel):
    temperature: bool = True
    pressure: bool = True
    acquisition: bool = True


class ProfileSpecification(StrictModel):
    property: str = Field(min_length=1, max_length=80)
    unit: str = Field(min_length=1, max_length=30)
    strategy: Literal["target", "range"]
    target: float | None = None
    tolerance: float | None = Field(default=None, ge=0)
    minimum: float | None = None
    maximum: float | None = None

    @model_validator(mode="after")
    def validate_strategy(self) -> ProfileSpecification:
        if self.strategy == "target":
            if self.target is None:
                raise ValueError("una especificación por objetivo requiere target")
            if self.minimum is not None or self.maximum is not None:
                raise ValueError("una especificación por objetivo no admite minimum ni maximum")
        else:
            if self.minimum is None or self.maximum is None:
                raise ValueError("una especificación por intervalo requiere minimum y maximum")
            if self.minimum > self.maximum:
                raise ValueError("minimum no puede superar maximum")
            if self.target is not None or self.tolerance is not None:
                raise ValueError("una especificación por intervalo no admite target ni tolerance")
        return self


class ProfileFields(StrictModel):
    name: str = Field(min_length=1, max_length=64)
    coffeeIds: list[str] = Field(min_length=1, max_length=100)
    specifications: list[ProfileSpecification] = Field(min_length=1, max_length=30)
    # Corte del purgado manual. No es una especificación de la preparación sino
    # un ajuste de la máquina, por eso va fuera de `specifications`. El rango es
    # el que valida el firmware (`process_limits_set_purge`); los 10 s son la
    # decisión del usuario del 16 de octubre, no una cifra plausible.
    purgeMaximumTimeMs: int = Field(default=10000, ge=1000, le=30000)
    # Subida de la referencia de CALDERA, en °C, para una sola preparación: la
    # compensación del primer uso tras un reposo (ADR-009, enmienda del
    # 2026-10-19). Sin tope aquí a propósito: el máximo es del acta de
    # temperatura y todavía no consta; la placa es la que lo valida y rechaza
    # la propuesta entera si se pasa (boiler-offset-out-of-range). Opcional y
    # sin valor por omisión: un perfil que no lo declara no pide subida.
    boilerOffsetC: float | None = Field(default=None, ge=0)

    @field_validator("coffeeIds")
    @classmethod
    def unique_coffees(cls, values: list[str]) -> list[str]:
        if len(values) != len(set(values)):
            raise ValueError("coffeeIds no admite valores repetidos")
        return values

    @field_validator("specifications")
    @classmethod
    def unique_properties(cls, values: list[ProfileSpecification]) -> list[ProfileSpecification]:
        properties = [item.property for item in values]
        if len(properties) != len(set(properties)):
            raise ValueError("cada propiedad puede aparecer una sola vez")
        return values


class ProfileCreate(ProfileFields):
    id: str | None = Field(default=None, min_length=1, max_length=64)


class ProfileUpdate(ProfileFields):
    pass


class DesiredSensoryChange(StrictModel):
    attribute: Literal[
        "Aroma",
        "Flavor",
        "Acidity",
        "Sweetness",
        "Bitterness",
        "Body",
        "Aftertaste",
        "Astringency",
        "Balance",
        "Fragrance",
        "Mouthfeel",
    ]
    direction: Literal["Increase", "Decrease", "Improve"]


class ProfileSensoryTarget(StrictModel):
    """El sabor que busca el barista: descriptores, cambios deseados o ambos.

    Basta uno de los dos (23-oct-2026): un barista que solo quiere menos
    amargor no tiene por qué inventar un descriptor para poder guardarlo.
    """

    coffeeId: str = Field(min_length=1, max_length=80)
    rationale: str = Field(min_length=1, max_length=2000)
    descriptors: list[SensoryDescriptor] = Field(default_factory=list, max_length=30)
    # Los que no están en la lista, escritos como los dijo el barista: igual
    # que en la cata, no pasan al grafo (23-oct-2026).
    otherDescriptors: list[Annotated[str, Field(min_length=1, max_length=60)]] = Field(
        default_factory=list, max_length=5
    )
    desiredChanges: list[DesiredSensoryChange] = Field(default_factory=list, max_length=20)

    @field_validator("otherDescriptors", mode="before")
    @classmethod
    def trimmed_others(cls, values: object) -> object:
        if isinstance(values, list):
            return [value.strip() if isinstance(value, str) else value for value in values]
        return values

    @model_validator(mode="after")
    def descriptors_or_changes(self) -> ProfileSensoryTarget:
        if not self.descriptors and not self.otherDescriptors and not self.desiredChanges:
            raise ValueError("indica al menos un descriptor o un cambio deseado")
        names = [value.casefold() for value in self.otherDescriptors]
        if len(names) != len(set(names)):
            raise ValueError("cada descriptor escrito puede aparecer una sola vez")
        return self

    @field_validator("descriptors")
    @classmethod
    def unique_target_descriptors(cls, values: list[SensoryDescriptor]) -> list[SensoryDescriptor]:
        if len(values) != len(set(values)):
            raise ValueError("descriptors no admite valores repetidos")
        return values

    @field_validator("desiredChanges")
    @classmethod
    def unique_target_changes(
        cls, values: list[DesiredSensoryChange]
    ) -> list[DesiredSensoryChange]:
        attributes = [item.attribute for item in values]
        if len(attributes) != len(set(attributes)):
            raise ValueError("cada atributo puede tener un solo cambio deseado")
        return values


class LegacyProfileCreate(StrictModel):
    id: str = Field(min_length=1, max_length=40)
    name: str = Field(min_length=1, max_length=64)
    definedBy: str | None = Field(default=None, min_length=1, max_length=120)
    version: int = Field(ge=1)
    temperatureSetpointC: float
    temperatureToleranceC: float = Field(gt=0)
    pressureSetpointBar: float = Field(gt=0)
    pressureToleranceBar: float = Field(gt=0)
    maximumTimeMs: int = Field(ge=1)
    expectedBeverageMassG: float | None = Field(default=None, gt=0)
    purgeMaximumTimeMs: int | None = Field(default=None, ge=1000, le=30000)
    status: Literal["draft", "proposed", "accepted", "effective", "retired"] = "proposed"


class RecommendationCreate(StrictModel):
    extractionId: str = Field(min_length=1, max_length=80)


class RecommendationAdjustment(StrictModel):
    property: Literal[
        "water-temperature",
        "extraction-time",
        "grind-setting",
        "coffee-dose",
        "brew-ratio",
        "pressure",
    ]
    direction: Literal["increase", "decrease", "maintain"]
    value: float | None = None
    unit: str | None = Field(default=None, max_length=40)


class RecommendationDecision(StrictModel):
    status: Literal["accepted", "rejected", "modified"]
    comment: str | None = Field(default=None, max_length=4000)
    modifiedAdjustment: RecommendationAdjustment | None = None

    @model_validator(mode="after")
    def modification_requires_details(self) -> RecommendationDecision:
        if self.status == "modified" and not (self.comment or "").strip():
            raise ValueError("una modificación requiere justificación")
        if self.status == "modified" and self.modifiedAdjustment is None:
            raise ValueError("una modificación requiere el ajuste propuesto por el barista")
        if self.status != "modified" and self.modifiedAdjustment is not None:
            raise ValueError("el ajuste modificado solo corresponde a una modificación")
        return self


class CompareRequest(StrictModel):
    ids: list[str] = Field(min_length=2, max_length=4)
    dimensions: list[
        Literal[
            "profile",
            "coffee",
            "equipment",
            "barista",
            "environment",
            "preparation",
            "sensory",
            "curves",
            "adjustments",
        ]
    ] = Field(default_factory=lambda: ["preparation"])

    @field_validator("dimensions")
    @classmethod
    def unique_dimensions(cls, values: list[str]) -> list[str]:
        if not values or len(values) != len(set(values)):
            raise ValueError("dimensions requiere valores únicos")
        return values


class ProfileCompareRequest(StrictModel):
    ids: list[ShortText] = Field(min_length=2, max_length=4)

    @field_validator("ids")
    @classmethod
    def unique_ids(cls, values: list[str]) -> list[str]:
        if len(values) != len(set(values)):
            raise ValueError("ids no admite valores repetidos")
        return values
