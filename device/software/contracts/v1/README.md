# Contrato de mensajes v1

Esquemas JSON Schema 2020-12 de los mensajes entre la máquina y el servidor, y de
los documentos sensoriales que registra el servidor.

## Envolvente

Todo mensaje que la máquina sincroniza va dentro de `envelope.schema.json`:

- `messageId` es la clave de idempotencia: reenviar el mismo mensaje no duplica
  datos.
- `occurredAt` es UTC en RFC 3339; las duraciones dentro de la carga van en
  milisegundos monotónicos.
- `deviceId` identifica la máquina y, junto con `messageId`, evita duplicados.

| `messageType` | Esquema de la carga | Productor | Uso |
|---|---|---|---|
| `telemetry.sample` | `telemetry.schema.json` | Máquina o simulador | Estado, medidas, objetivos y actuación observada |
| `operational.event` | `operational-event.schema.json` | Máquina | Transición, acción del barista o alarma |
| `extraction.completed` | `extraction.schema.json` | Máquina | Resumen de la extracción cerrada |
| `extraction.series` | `extraction-series.schema.json` | Máquina | Serie medida de una extracción, por tramos |
| `profile.proposal` | `profile.schema.json` | Servidor | Propuesta de perfil; nunca se activa sola |

La telemetría, el resumen y la serie de una extracción declaran su procedencia en
`source`. Los ejemplos de `examples/` son datos simulados o sintéticos, como
indica el nombre de cada archivo; los simulados lo declaran además con
`source: "simulation"`.

## Sincronización

`sync.schema.json` es el lote que la máquina envía a `POST /api/v1/sync`: su
`deviceId` y hasta 64 sobres. `sync-response.schema.json` es la respuesta: los
`messageId` confirmados (`acknowledged`), cuántos se insertaron y cuántos eran
duplicados, la hora del servidor y las propuestas de perfil pendientes.

Cuando no hay red, la máquina guarda los mensajes en una cola persistente acotada
en NVS. Un mensaje se borra de la cola solo cuando su `messageId` vuelve en
`acknowledged`; como el servidor usa ese identificador como clave idempotente,
reintentar es seguro.

**El contrato no tiene órdenes de actuación.** No existe `start-extraction`,
`pump-command` ni `heater-command`. Una propuesta de perfil exige validación en la
máquina y aceptación del barista antes de convertirse en configuración efectiva.

## Documentos del servidor

Estos esquemas no viajan dentro de la envolvente de la máquina:

| Esquema | Contenido |
|---|---|
| `coffee.schema.json` | Café y perfil sensorial declarado por una fuente identificada |
| `extraction-context.schema.json` | Café y objetivo sensorial definidos para una extracción |
| `sensory-evaluation.schema.json` | Resultado sensorial observado |
| `sensory-learning-episode.schema.json` | Par de extracciones, ajuste entre ambas y valoración posterior |
| `dataset-manifest.schema.json` | Identidad, integridad, origen y protocolo de una versión de un conjunto de datos |
| `baseline-model.schema.json` | Artefacto de referencia para validar software |
| `model-inference.schema.json` | Inferencia registrada, con entrada inmutable, incertidumbre y posible abstención |
| `recommendation-proposal.schema.json` | Propuesta o abstención, sin ningún campo para órdenes a actuadores |

«Declarado», «objetivo» y «observado» no son intercambiables: cada esquema guarda
uno solo de los tres.

## Validación

Las pruebas de anfitrión del firmware (`device/tests/host/run_*_message_tests.sh`)
validan los mensajes que genera la máquina contra estos esquemas con
`device/tests/host/validate_envelope.py`, que necesita el paquete `jsonschema`;
sin él, la validación se omite y lo avisa. Las pruebas del backend
(`apps/web/backend/tests/test_contracts.py`) también los cargan.
