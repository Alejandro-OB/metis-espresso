from __future__ import annotations

from typing import Any

FIELD_LABELS = {
    "altitudeM": "altitud",
    "ambientHumidityPercent": "humedad ambiente",
    "ambientTemperatureC": "temperatura ambiente",
    "bagIdentifier": "identificador de bolsa",
    "beverageMassG": "masa de la bebida",
    "coffeeDoseG": "dosis de café",
    "coffeeId": "café",
    "commercialReference": "referencia comercial",
    "descriptors": "descriptores",
    "evaluatedAt": "fecha de evaluación",
    "lastCalibrationAt": "última calibración",
    "location": "ubicación",
    "lotIdentifier": "identificador de lote",
    "maximum": "valor máximo",
    "minimum": "valor mínimo",
    "mineralComposition": "composición mineral",
    "mineralSource": "fuente de la composición mineral",
    "name": "nombre",
    "notApplicableFields": "datos no aplicables",
    "pressureBar": "presión",
    "profileId": "perfil",
    "property": "variable",
    "rationale": "justificación",
    "samplingFrequencyHz": "frecuencia de muestreo",
    "scores": "evaluación sensorial",
    "specifications": "parámetros del perfil",
    "target": "valor objetivo",
    "tdsPercent": "TDS",
    "temperatureC": "temperatura",
    "tolerance": "tolerancia",
    "type": "tipo",
    "unit": "unidad",
}

MESSAGE_BY_TYPE = {
    "extra_forbidden": "Este dato no está permitido.",
    "float_parsing": "Escribe un número válido.",
    "greater_than": "Debe ser mayor que el límite indicado.",
    "greater_than_equal": "No puede ser menor que el límite indicado.",
    "int_parsing": "Escribe un número entero válido.",
    "less_than": "Debe ser menor que el límite indicado.",
    "less_than_equal": "No puede superar el límite indicado.",
    "list_type": "Selecciona una o más opciones válidas.",
    "literal_error": "Selecciona una opción válida.",
    "missing": "Completa este dato.",
    "string_too_long": "El texto es demasiado largo.",
    "string_too_short": "Completa este dato.",
}

TERM_REPLACEMENTS = {
    "descriptors": "descriptores",
    "maximum": "máximo",
    "minimum": "mínimo",
    "notApplicableFields": "datos no aplicables",
    "target": "objetivo",
    "tolerance": "tolerancia",
}


def _location_parts(error: dict[str, Any]) -> list[str | int]:
    return [part for part in error.get("loc", ()) if part not in {"body", "query", "path"}]


def _field_label(parts: list[str | int]) -> str:
    names = [part for part in parts if isinstance(part, str)]
    label = FIELD_LABELS.get(names[-1], names[-1]) if names else "formulario"
    indexes = [part for part in parts if isinstance(part, int)]
    return f"{label}, elemento {indexes[-1] + 1}" if indexes else label


def _message(error: dict[str, Any]) -> str:
    error_type = str(error.get("type", ""))
    if error_type in MESSAGE_BY_TYPE:
        return MESSAGE_BY_TYPE[error_type]
    raw = str(error.get("msg") or "Revisa este dato.").removeprefix("Value error, ")
    for technical, readable in TERM_REPLACEMENTS.items():
        raw = raw.replace(technical, readable)
    return raw[:1].upper() + raw[1:] if raw else "Revisa este dato."


def actionable_validation_errors(errors: list[dict[str, Any]]) -> tuple[str, list[dict[str, str]]]:
    fields = [
        {
            "field": ".".join(str(part) for part in _location_parts(error)),
            "label": _field_label(_location_parts(error)),
            "message": _message(error),
        }
        for error in errors
    ]
    first = fields[0] if fields else None
    message = (
        f"Revisa {first['label']}: {first['message']}"
        if first
        else "Revisa los datos e inténtalo de nuevo."
    )
    return message, fields
