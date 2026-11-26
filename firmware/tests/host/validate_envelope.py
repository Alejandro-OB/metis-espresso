"""Valida un mensaje generado por el firmware contra los contratos publicados.

Las referencias entre esquemas se resuelven desde el propio directorio de
contratos, sin salir a la red.
"""

import json
import sys
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
    from referencing import Registry, Resource
except ImportError:
    print("SKIP: jsonschema no está instalado; no se validó contra el contrato")
    raise SystemExit(0)


def main() -> int:
    message = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
    contracts = Path(sys.argv[2])
    registry = Registry()
    schemas: dict[str, dict] = {}
    for path in sorted(contracts.glob("*.schema.json")):
        schema = json.loads(path.read_text(encoding="utf-8"))
        schemas[path.name] = schema
        if schema.get("$id"):
            registry = registry.with_resource(schema["$id"], Resource.from_contents(schema))

    # El esquema de la carga depende del tipo de mensaje: un sobre solo está
    # bien formado si su contenido cumple el contrato que él mismo declara.
    payload_schemas = {
        "extraction.completed": "extraction.schema.json",
        "telemetry.sample": "telemetry.schema.json",
        "operational.event": "operational-event.schema.json",
        "extraction.series": "extraction-series.schema.json",
        "profile.proposal": "profile.schema.json",
    }
    message_type = message.get("messageType")
    payload_schema = payload_schemas.get(message_type)
    if payload_schema is None:
        print(f"FAIL: tipo de mensaje sin contrato conocido: {message_type}")
        return 1

    failed = False
    for name, payload in (
        ("envelope.schema.json", message),
        (payload_schema, message["payload"]),
    ):
        validator = Draft202012Validator(schemas[name], registry=registry)
        for error in sorted(validator.iter_errors(payload), key=lambda e: list(e.path)):
            print(f"FAIL: {name}: {'/'.join(str(p) for p in error.path)}: {error.message}")
            failed = True
    if failed:
        return 1
    print(f"PASS: el mensaje cumple envelope.schema.json y {payload_schema}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
