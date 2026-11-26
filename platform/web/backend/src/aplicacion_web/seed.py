from __future__ import annotations

from aplicacion_web.application.ports import Repository
from aplicacion_web.domain.models import iso_now


def seed_demo(repository: Repository) -> None:
    if repository.counts():
        return
    coffee = {
        "id": "coffee-huila-001",
        "commercialReference": "Huila · Microlote 24",
        "lotIdentifier": "HL-24-07",
        "bagIdentifier": "B-03",
        "variety": "Caturra",
        "postHarvestProcess": "Lavado",
        "roastLevel": "Medio claro",
        "roastDate": "2026-08-21T00:00:00Z",
        "region": "Pitalito, Huila",
        "altitudeM": 1780,
        "soilCharacteristics": None,
        "mineralComposition": None,
        "mineralSource": None,
        "notApplicableFields": [],
        "declaredSensoryProfile": {
            "id": "declared-huila-001",
            "declaredBy": "Tostador de referencia",
            "source": "Etiqueta del lote",
            "originalText": "Panela, mandarina y cacao",
            "descriptors": ["panela", "mandarina", "cacao"],
        },
        "createdAt": "2026-08-25T14:00:00Z",
        "provenance": {"actor": "synthetic-seed", "recordedAt": iso_now(), "source": "synthetic"},
    }
    repository.add_document("coffee", coffee["id"], coffee)

    common_profile = {
        "id": "espresso-lab",
        "name": "Espresso laboratorio",
        "version": 3,
        # El vocabulario del contrato admite draft, proposed, accepted, effective
        # y retired. "active-local" no era ninguno de ellos, de modo que el
        # exportador semántico no podía declarar el estado del perfil y el grafo
        # resultante incumplía la ontología.
        "status": "effective",
        # La ontología exige saber quién definió una configuración: sin agente,
        # una referencia de temperatura y presión aparece sin responsable.
        "definedBy": "barista-lab-01",
        "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
        "tolerances": {"temperatureC": 1.0, "pressureBar": 0.5},
        # Sin tiempo máximo la configuración no puede gobernar la máquina: el
        # dispositivo no sabría cuándo cortar la extracción y la rechaza. No se
        # notaba mientras ninguna configuración llegaba al dispositivo.
        "maximumTimeMs": 30000,
    }
    repository.add_document(
        "profile",
        common_profile["id"],
        {
            "id": common_profile["id"],
            "familyId": common_profile["id"],
            "name": common_profile["name"],
            "version": common_profile["version"],
            "status": "approved",
            "coffeeIds": [coffee["id"]],
            "specifications": [
                {
                    "property": "temperature",
                    "unit": "°C",
                    "strategy": "target",
                    "target": 93.0,
                    "tolerance": 1.0,
                },
                {
                    "property": "pressure",
                    "unit": "bar",
                    "strategy": "target",
                    "target": 9.0,
                    "tolerance": 0.5,
                },
                {
                    "property": "extraction-time",
                    "unit": "s",
                    "strategy": "range",
                    "minimum": 22.0,
                    "maximum": 30.0,
                },
            ],
            "createdAt": "2026-08-25T14:00:00Z",
            "approvedAt": "2026-08-25T15:00:00Z",
            "provenance": {
                "actor": "synthetic-seed",
                "recordedAt": iso_now(),
                "source": "synthetic",
            },
        },
    )
    repository.add_document(
        "equipment",
        "espresso-lab-01",
        {
            "id": "espresso-lab-01",
            "type": "machine",
            "name": "Máquina del laboratorio",
            "location": "Barra principal",
            "capabilities": [
                {
                    "property": "water-temperature",
                    "manualAdjustment": True,
                    "automaticAdjustment": False,
                }
            ],
            "sensors": [
                {
                    "id": "temp-lab-01",
                    "measuredProperty": "water-temperature",
                    "location": "Salida de agua",
                    "accuracy": 0.2,
                    "accuracyUnit": "°C",
                    "samplingFrequencyHz": 10.0,
                    "lastCalibrationAt": "2026-08-01T05:00:00Z",
                },
                {
                    "id": "pressure-lab-01",
                    "measuredProperty": "pressure",
                    "location": "Circuito hidráulico",
                    "accuracy": 0.1,
                    "accuracyUnit": "bar",
                    "samplingFrequencyHz": 10.0,
                    "lastCalibrationAt": "2026-08-01T05:00:00Z",
                },
            ],
            "createdAt": "2026-08-01T05:00:00Z",
            "provenance": {
                "actor": "synthetic-seed",
                "recordedAt": iso_now(),
                "source": "synthetic",
            },
        },
    )
    repository.add_document(
        "equipment",
        "molino-lab-01",
        {
            "id": "molino-lab-01",
            "type": "grinder",
            "name": "Molino del laboratorio",
            "location": "Barra principal",
            "capabilities": [],
            "sensors": [],
            "createdAt": "2026-08-01T05:00:00Z",
            "provenance": {
                "actor": "synthetic-seed",
                "recordedAt": iso_now(),
                "source": "synthetic",
            },
        },
    )
    extractions = [
        {
            "id": "ext-20260904-001",
            "source": "measured",
            "profile": common_profile,
            "coffeeId": coffee["id"],
            "coffeeSnapshot": coffee,
            "coffeeReference": coffee["commercialReference"],
            "machineId": "espresso-lab-01",
            "grinderId": "molino-lab-01",
            "calibrationId": "calibracion-turno-01",
            "environment": {"temperatureC": 22.4, "humidityPercent": 61.0},
            "barista": "barista-lab-01",
            "startedAt": "2026-09-04T14:32:10Z",
            "finishedAt": "2026-09-04T14:32:39Z",
            "durationMs": 29180,
            "termination": "manual-normal-stop",
            "sampleCount": 292,
            "seriesRef": "series://ext-20260904-001",
            "statistics": {
                "temperatureMaeC": 0.42,
                "pressureMaeBar": 0.31,
                "temperatureWithinTolerancePercent": 94.8,
                "pressureWithinTolerancePercent": 91.4,
            },
            "manualMeasurements": {
                "coffeeDoseG": 18.0,
                "beverageMassG": 37.2,
                "tdsPercent": 9.1,
                "extractionYieldPercent": 18.807,
                "instrument": "Refractómetro ATC",
                "recordedBy": "barista-lab-01",
                "recordedAt": "2026-09-04T14:35:00Z",
            },
            "sensoryEvaluation": {
                "id": "eval-001",
                "evaluatedBy": "barista-lab-01",
                "evaluatedAt": "2026-09-04T14:38:00Z",
                "protocolReference": "Protocolo sensorial piloto v0.2",
                "descriptors": ["cítrico", "panela"],
                "defects": [],
                "scores": [
                    {"attribute": "acidity", "dimension": "perceived-intensity", "value": 7},
                    {"attribute": "overall-liking", "dimension": "hedonic-liking", "value": 7},
                ],
                "acceptance": {"status": "accepted", "rationale": "Balance limpio"},
                "comment": "Final dulce y definido.",
            },
            "provenance": {
                "actor": "device-espresso-01",
                "recordedAt": "2026-09-04T14:32:40Z",
                "source": "measured",
            },
        },
        {
            "id": "ext-20260904-002",
            "source": "measured",
            "profile": common_profile,
            "coffeeId": coffee["id"],
            "coffeeSnapshot": coffee,
            "coffeeReference": coffee["commercialReference"],
            "machineId": "espresso-lab-01",
            "grinderId": "molino-lab-01",
            "calibrationId": "calibracion-turno-01",
            "environment": {"temperatureC": 22.8, "humidityPercent": 59.0},
            "barista": "barista-lab-01",
            "startedAt": "2026-09-04T15:06:12Z",
            "finishedAt": "2026-09-04T15:06:39Z",
            "durationMs": 27410,
            "termination": "manual-normal-stop",
            "sampleCount": 275,
            "seriesRef": "series://ext-20260904-002",
            "statistics": {
                "temperatureMaeC": 0.27,
                "pressureMaeBar": 0.22,
                "temperatureWithinTolerancePercent": 97.1,
                "pressureWithinTolerancePercent": 95.6,
            },
            "manualMeasurements": {
                "coffeeDoseG": 18.0,
                "beverageMassG": 36.4,
                "tdsPercent": 9.4,
                "extractionYieldPercent": 19.009,
                "instrument": "Refractómetro ATC",
                "recordedBy": "barista-lab-01",
                "recordedAt": "2026-09-04T15:10:00Z",
            },
            "sensoryEvaluation": {
                "id": "eval-002",
                "evaluatedBy": "barista-lab-01",
                "evaluatedAt": "2026-09-04T15:12:00Z",
                "protocolReference": "Protocolo sensorial piloto v0.2",
                "descriptors": ["mandarina", "cacao"],
                "defects": ["ExcessiveAcidity"],
                "scores": [
                    {"attribute": "acidity", "dimension": "perceived-intensity", "value": 6},
                    {"attribute": "overall-liking", "dimension": "hedonic-liking", "value": 8},
                ],
                "acceptance": {"status": "accepted", "rationale": "Mayor balance"},
                "comment": "Acidez integrada.",
            },
            "provenance": {
                "actor": "device-espresso-01",
                "recordedAt": "2026-09-04T15:06:40Z",
                "source": "measured",
            },
        },
        {
            "id": "sim-20260904-003",
            "source": "simulation",
            "profile": common_profile,
            "coffeeId": None,
            "coffeeReference": None,
            "machineId": "espresso-simulada-01",
            "grinderId": None,
            "calibrationId": None,
            "barista": None,
            "startedAt": "2026-09-04T16:00:00Z",
            "finishedAt": "2026-09-04T16:00:30Z",
            "durationMs": 30000,
            "termination": "maximum-extraction-time-reached",
            "sampleCount": 300,
            "seriesRef": "series://sim-20260904-003",
            "statistics": {
                "temperatureMaeC": 0.18,
                "pressureMaeBar": 0.15,
                "temperatureWithinTolerancePercent": 99.0,
                "pressureWithinTolerancePercent": 98.0,
            },
            "provenance": {
                "actor": "fmu-reference",
                "recordedAt": "2026-09-04T16:00:31Z",
                "source": "simulation",
            },
        },
    ]
    for extraction in extractions:
        repository.add_document("extraction", extraction["id"], extraction)
    for extraction in extractions[:2]:
        values = (
            ((None, None), (None, None), (None, None))
            if extraction["id"] == "ext-20260904-001"
            else ((92.8, 8.8), (93.1, 9.1), (93.0, 9.0))
        )
        for sequence, (temperature, pressure) in enumerate(values):
            sample_id = f"{extraction['id']}-sample-{sequence:05d}"
            repository.add_document(
                "live-sample",
                sample_id,
                {
                    "id": sample_id,
                    "extractionId": extraction["id"],
                    "sequence": sequence,
                    "elapsedMs": sequence * 10000,
                    "occurredAt": extraction["startedAt"],
                    "temperatureC": temperature,
                    "pressureBar": pressure,
                    "quality": "valid" if temperature is not None else "missing",
                    "source": "measured",
                },
            )

    repository.add_document(
        "recommendation",
        "rec-demo-grind-001",
        {
            "schemaVersion": "1.0",
            "origin": "knowledge-base",
            "recommendationId": "rec-demo-grind-001",
            "inferenceId": "inference-demo-001",
            "extractionId": extractions[0]["id"],
            "status": "test-only",
            "reasonCode": "ACIDITY_ABOVE_TARGET",
            "summary": "Subir ligeramente la temperatura del agua en la próxima preparación.",
            "respondsTo": {
                "code": "acidity",
                "summary": "La acidez percibida quedó por encima del objetivo del perfil.",
            },
            "proposedAdjustment": {
                "property": "water-temperature",
                "direction": "increase",
                "value": 93.0,
                "unit": "°C",
            },
            "uncertainty": {
                "value": 0.24,
                "methodReference": "demo://recommendation-method-v1",
            },
            "evidenceIds": ["fact:ext-20260904-001:temperatureMaeC"],
            "knowledgeSource": {"name": "EKO", "version": "3.5.0"},
            "applicability": "Preparación de demostración con el mismo café y perfil.",
            "requiresHumanReview": True,
            "actuatorCommand": None,
            "predictedSensoryProfile": None,
            "createdBy": "synthetic-seed",
            "createdAt": "2026-09-04T14:40:00Z",
            "provenance": {
                "actor": "synthetic-seed",
                "recordedAt": iso_now(),
                "source": "synthetic",
            },
        },
    )

    repository.add_document(
        "episode",
        "episode-001",
        {
            "id": "episode-001",
            "coffeeId": coffee["id"],
            "initialExtractionId": extractions[0]["id"],
            "followUpExtractionId": extractions[1]["id"],
            "adjustment": {
                "id": "adjustment-001",
                "performedBy": "barista-lab-01",
                "property": "grind-setting",
                "previousValue": 4.2,
                "newValue": 4.5,
                "unit": "dial",
                "occurredAt": "2026-09-04T14:55:00Z",
                "rationale": "Reducir intensidad de acidez",
            },
            "targetOutcome": {"kind": "observable-property", "code": "balance"},
            "outcome": {
                "category": "expected-observed",
                "assessedBy": "barista-lab-01",
                "assessedAt": "2026-09-04T15:14:00Z",
                "rationale": "Subió el agrado general de 7 a 8",
            },
            "provenance": {
                "actor": "synthetic-seed",
                "recordedAt": iso_now(),
                "source": "synthetic",
            },
        },
    )
    repository.upsert_document(
        "live-sample",
        "telemetry-seed",
        {
            "id": "telemetry-seed",
            "deviceId": "espresso-lab-01",
            "occurredAt": iso_now(),
            "source": "measured",
            "state": "idle",
            "elapsedMs": 0,
            "sequence": 0,
            "extractionId": None,
            "temperatureC": 92.7,
            "pressureBar": 0.0,
            "measurements": {"temperatureC": 92.7, "pressureBar": 0.0},
            "quality": "valid",
            "channelQuality": {"temperature": "valid", "pressure": "valid"},
            "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
            "actuators": {"heaterOn": False, "pumpDutyPercent": 0, "virtual": False},
        },
    )
