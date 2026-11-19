# Metis: sombra digital de una cafetera espresso

Sistema de adquisición y análisis de datos para la preparación de café espresso,
construido sobre una cafetera doméstica de bomba vibratoria. El prototipo se llama
Metis. Un controlador en la máquina regula la temperatura de la caldera y la presión
de extracción. Un servidor local junto a la máquina guarda cada preparación, la simula con un modelo físico, compara lo
simulado con lo medido, representa todo en la ontología EKO y propone ajustes. La
decisión sobre cada ajuste es siempre del barista.

Proyecto de grado del programa de Ingeniería Electrónica y Telecomunicaciones de la
Universidad del Cauca.

## Contenido

```text
.
├── device/
│   ├── firmware/esp32-s3-single-mcu/   Firmware del controlador (ESP-IDF 6.1, ESP32-S3)
│   ├── tests/host/                     Pruebas de los módulos del firmware en el computador
│   ├── hardware/easyeda/current/       Proyectos de las tres tarjetas en EasyEDA
│   ├── hardware/fabrication/           Gerber, esquemáticos y plantillas de fabricación
│   └── software/                       Contratos de mensajes y modelo de referencia
├── apps/web/                           Servidor local: API, interfaz, trabajadores y almacén RDF
├── packages/                           Lógica de simulación, semántica, perfiles y estabilidad
├── knowledge/ontology/                 Ontología EKO (OWL 2), formas SHACL, consultas y razonador
├── models/modelica/                    Modelo físico en Modelica, FMU y contratos del modelo
└── contracts/simulation/               Contrato de la simulación entre el servidor y el modelo
```

## Arquitectura

El sistema sigue KAFE, una arquitectura basada en MAPE-K que incorpora al barista en
las decisiones, repartida en dos equipos.

| Equipo | Funciones | Ritmo |
|---|---|---|
| Controlador ESP32-S3, en la máquina | Adquisición, regulación de temperatura y presión, máquina de estados y enclavamiento de seguridad | 20 ms (presión) y 200 ms (temperatura) |
| Raspberry Pi 5, junto a la máquina | Historial, simulación del modelo, comparación físico-virtual, ontología y recomendaciones | Entre preparaciones |

El controlador no depende de la red para regular ni para proteger la máquina. Ningún
resultado del servidor llega a la bomba ni a la resistencia calefactora sin pasar
por la revisión del controlador.

## Seguridad del firmware

Las salidas de potencia, la bomba y la resistencia calefactora, pasan por un guardián
de actuación que decide antes que cualquier controlador. Cada lazo solo actúa si el
firmware se compiló con su planta identificada (`main/commissioned/`). Sin esos
archivos, el guardián mantiene las salidas cortadas.

Este firmware está ajustado a una máquina concreta. Cargarlo en otra máquina sin
identificar de nuevo sus plantas acciona una bomba y una resistencia de red eléctrica
con números que no le corresponden.

## Firmware

Requiere ESP-IDF 6.1. El script `idf.sh` usa `IDF_PATH` o, si no está definida,
`~/esp/esp-idf-v6.1`.

```sh
cd device/firmware/esp32-s3-single-mcu
./idf.sh build
./idf.sh -b 921600 monitor
```

La consola va a 921600 baudios. La red, la dirección del servidor y el
identificador del equipo se configuran con `./idf.sh menuconfig`, y quedan en
`sdkconfig`, que no se versiona.

Pruebas de los módulos en el computador (C17, `-Wall -Wextra -Werror -pedantic`):

```sh
for s in device/tests/host/run_*.sh; do "$s" || break; done
```

## Servidor local

Requiere Docker con Compose. Los valores por omisión del archivo `compose.yaml` son
de desarrollo. En un despliegue real, las contraseñas y los tokens se definen en un
archivo `.env` junto a `compose.yaml`.

```sh
cd apps/web
docker compose up --build
```

La interfaz queda en `http://localhost:8080`. Pruebas:

```sh
cd apps/web/backend && python -m pytest tests -q
PYTHONPATH=packages python -m pytest packages -q
```

## Ontología

La ontología EKO tiene tres módulos: el núcleo del dominio (`espresso-dt.ttl`), la adquisición
(`espresso-acquisition.ttl`) y la simulación (`espresso-simulation.ttl`), cada uno con
sus formas SHACL. Las preguntas de competencia están en `knowledge/ontology/queries/`.

```sh
python knowledge/ontology/tests/validate.py
```

## Modelo físico

El paquete `models/modelica/GemeloDigitalPreparacionCafe` describe la caldera, la
bomba y el recorrido del agua. El servidor usa la FMU de `models/modelica/dist/`,
declarada en `model-registry-v1.json`. Para regenerarla hace falta OpenModelica:

```sh
cd models/modelica && ./exportar_fmu_adapter_v21.sh
```
