# Metis: a digital shadow of an espresso machine

Data acquisition and analysis system for espresso preparation, built on a
household vibratory-pump espresso machine. The prototype is called Metis.

- **Metis.** A controller inside the machine regulates the boiler temperature
  and the extraction pressure, and records both during every preparation.
- **Local server.** A server next to the machine stores each preparation and
  simulates it with a physical model. It compares the simulation with the
  measurements and describes everything with the EKO ontology.
- **The barista.** The decision about what to change in the next preparation
  always belongs to the barista.

Undergraduate thesis project, Electronics and Telecommunications Engineering,
Universidad del Cauca, Colombia. A Spanish summary is at the
[end of this page](#en-español).

## Repository layout

The folders follow the parts of the system described in the article.

```text
.
├── firmware/    Metis controller (ESP-IDF 6.1, ESP32-S3) and its host tests
├── hardware/    The three boards: EasyEDA projects and fabrication files
├── platform/    Local server: API, web interface, workers, RDF store,
│                shared Python packages and message contracts
├── model/       Physical model in Modelica and the FMU the server runs
├── ontology/    EKO ontology (OWL 2), SHACL shapes, competency questions, reasoner
├── data/        Tasting data: the twelve preparations of the evaluation
└── docs/        Design decisions (ADR), commissioning records and test reports
```

## Architecture

The system follows KAFE (*Knowledge-Assisted Feedback for Espresso*), an
architecture based on MAPE-K that keeps the barista in the loop. It runs on two
devices.

| Device | Role | Rate |
|---|---|---|
| ESP32-S3 controller, inside the machine | Acquisition, temperature and pressure control, state machine and safety interlock | 20 ms (pressure), 200 ms (temperature) |
| Raspberry Pi 5, next to the machine | History, model simulation, measured-versus-simulated comparison, ontology | Between preparations |

The controller does not depend on the network to regulate or to protect the
machine. No result from the server reaches the pump or the heater without
passing through the controller.

## Firmware safety

The power outputs, pump and heater, go through an actuation guard that decides
before any controller. Each loop acts only if the firmware was built with its
identified plant (`firmware/main/commissioned/`); without those files the guard
keeps the outputs off.

This firmware is tuned to one specific machine. Loading it on another machine
without identifying its plants again drives a mains-powered pump and heater with
numbers that do not belong to it.

## Getting started

**Firmware** (requires ESP-IDF 6.1; `idf.sh` uses `IDF_PATH` or
`~/esp/esp-idf-v6.1`):

```sh
cd firmware
./idf.sh build
./idf.sh -b 921600 monitor
```

Wi-Fi, server address and device identifier are set with `./idf.sh menuconfig`
and stored in `sdkconfig`, which is not versioned.

**Local server** (requires Docker Compose; the defaults in `compose.yaml` are
for development, and a real deployment sets passwords and tokens in a `.env`
file):

```sh
cd platform/web
docker compose up --build
```

The interface is served at `http://localhost:8080`.

**Physical model** (requires OpenModelica to regenerate the FMU):

```sh
cd model && ./exportar_fmu_adapter_v21.sh
```

## Tests

```sh
for s in firmware/tests/host/run_*.sh; do "$s" || break; done      # firmware modules (C17)
(cd platform/web/backend && python -m pytest tests -q)              # server
PYTHONPATH=platform/packages python -m pytest platform/packages -q  # shared packages
python ontology/tests/validate.py                                   # EKO
```

## EKO ontology

EKO has a domain core (`espresso-dt.ttl`), an acquisition module
(`espresso-acquisition.ttl`) and a simulation module
(`espresso-simulation.ttl`), each with its SHACL shapes, in `ontology/owl/`.
Labels and definitions are in Spanish and English.

| | |
|---|---|
| Documentation | https://alejandro-ob.github.io/metis-espresso/ |
| Permanent IRI | https://w3id.org/espresso-dt |
| DOI (all versions) | [10.5281/zenodo.24561370](https://doi.org/10.5281/zenodo.24561370) |
| DOI (version 3.5.0) | [10.5281/zenodo.24561371](https://doi.org/10.5281/zenodo.24561371) |
| AgroPortal | https://agroportal.eu/ontologies/EKO |

## Documentation

The code cites the origin of each decision and each number. Those documents are
in `docs/`, in Spanish:

- `arquitectura/decisiones/`: the design decisions (ADR) cited by the code.
- `engineering/actas/`: the commissioning records from which the plants and
  tunings compiled into the firmware come; the tests check that every number
  matches its record. Its README summarises each record.
- `engineering/evidencias/` and `engineering/protocolo-catacion.md`: the tests
  and the tasting protocol cited by the model, the server and the interface.

Some documents mention internal project reports that are not part of this
repository; those mentions are left as plain text.

## Licence and citation

The source code (`firmware/`, `platform/`, `model/` and the scripts in
`ontology/`) is released under the [MIT licence](LICENSE). The EKO ontology and
its shapes (`ontology/owl/`), the tasting data (`data/`), the documentation and
the hardware designs (`hardware/`) are released under the
[Creative Commons Attribution 4.0 International licence](https://creativecommons.org/licenses/by/4.0/)
(CC BY 4.0). To cite this work, use the DOI
[10.5281/zenodo.24565331](https://doi.org/10.5281/zenodo.24565331) (all versions)
or see [`CITATION.cff`](CITATION.cff).

## En español

Metis es un sistema de adquisición y análisis de datos para la preparación de
café espresso, construido sobre una cafetera doméstica de bomba vibratoria. Un
controlador en la máquina regula la temperatura de la caldera y la presión de
extracción y registra cada preparación. Un servidor local la simula con un
modelo físico, compara lo simulado con lo medido y lo representa en la
ontología EKO. La decisión sobre cada ajuste es siempre del barista.

Cada carpeta corresponde a una parte del sistema: `firmware/` (controlador),
`hardware/` (tarjetas), `platform/` (servidor local), `model/` (modelo físico),
`ontology/` (EKO), `data/` (datos de la cata) y `docs/` (decisiones de diseño,
actas e informes de pruebas, en español).

Trabajo de grado del programa de Ingeniería Electrónica y Telecomunicaciones de
la Universidad del Cauca.
