# Servidor local

Aplicación que corre en la Raspberry Pi 5 junto a la cafetera. Recibe la
telemetría del controlador ESP32-S3, guarda cada preparación con su contexto,
ejecuta el modelo físico (FMU) para compararlo con lo medido y publica los datos
como RDF en un almacén SPARQL.

El servidor **no acciona la máquina**. La API no tiene ninguna ruta para
iniciar una extracción ni para mandar la bomba o el calentador; lo único que la
máquina recibe de vuelta son confirmaciones y configuraciones que ella misma valida
antes de aplicarlas.

## Qué hace

- Sesión por usuario, con roles de barista y de administración.
- Catálogo de cafés y de equipos.
- Perfiles de preparación: creación, aprobación, activación, retiro y comparación.
- Preparación en vivo con las curvas de temperatura y presión que envía la máquina,
  y registro posterior de mediciones manuales, contexto y evaluación sensorial.
- Historial, comparación y exportación de extracciones, con trazabilidad de cada
  documento.
- Simulaciones con la FMU del modelo y comparación entre la serie medida y la
  simulada. Cada serie declara su procedencia: una simulación nunca se presenta
  como medición.
- Recomendaciones de ajuste en las que la decisión final queda registrada a nombre
  del barista.
- Calibraciones, referencias de temperatura del agua y estado de la máquina.
- Auditoría de actividad y administración de cuentas.
- Interfaz instalable (PWA).

## Servicios

`compose.yaml` define:

| Servicio | Función |
|---|---|
| `db` | PostgreSQL 17 |
| `migrate` | Aplica las migraciones de Alembic y termina |
| `api` | API FastAPI (`aplicacion_web.main:app`) |
| `simulation-worker` | Ejecuta las simulaciones FMU en cola (`aplicacion_web.workers.simulation`) |
| `web` | Nginx con la interfaz construida; publica el puerto `8080` y reenvía `/api/` y `/events` a la API |
| `rdf-store` | Apache Jena Fuseki, perfil `semantic` (ver [`rdf-store/`](rdf-store/README.md)) |
| `semantic-worker` | Proyecta los documentos a RDF y los publica en el almacén, perfil `semantic` |

La imagen del backend se construye con contexto en la raíz del repositorio,
porque además de `apps/web/backend` copia `packages/`, `models/modelica/`,
`knowledge/ontology/` y `contracts/`.

## Tecnologías

| Parte | Tecnologías |
|---|---|
| Interfaz | React 19, TypeScript, Vite, TanStack Query, React Aria |
| API | Python 3.12 o superior, FastAPI, SQLAlchemy, Alembic |
| Datos | PostgreSQL; SQLite en las pruebas |
| Simulación | FMPy sobre las FMU de `models/modelica/dist/` |
| Semántica | RDFLib, pySHACL, Apache Jena Fuseki |
| Pruebas | Pytest, Ruff, Vitest, Playwright |

## Arranque

Requisitos: Docker con el complemento Compose. Desde `apps/web/`:

```bash
docker compose --profile semantic up --build -d
```

Sin `--profile semantic` arrancan todos los servicios menos el almacén RDF y su
trabajador.

| Dirección | Qué hay |
|---|---|
| <http://localhost:8080> | Interfaz |
| <http://localhost:8080/api/docs> | Documentación de la API |
| <http://localhost:8080/health> | Respuesta de Nginx |
| <http://localhost:8080/api/v1/health> | Estado de la API |

Para detener sin borrar datos: `docker compose --profile semantic down`.

### Credenciales por omisión

Solo sirven para desarrollo local:

| Rol | Usuario | Contraseña |
|---|---|---|
| Barista | `barista` | `espresso-barista` |
| Administración | `admin` | `espresso-admin` |

Las contraseñas se guardan con Argon2 y la sesión usa una cookie `HttpOnly` con
`SameSite=Strict`.

## Configuración

Todas las variables tienen valor por omisión en `compose.yaml`; se cambian
exportándolas o con un archivo `.env` junto a `compose.yaml`. En un despliegue
real hay que cambiar al menos las contraseñas y los *tokens*.

| Variable | Propósito |
|---|---|
| `POSTGRES_DB`, `POSTGRES_USER`, `POSTGRES_PASSWORD` | Acceso a PostgreSQL |
| `APP_SEED_DEMO` | Carga de datos de demostración (por omisión `false`) |
| `APP_INITIAL_BARISTA_*`, `APP_INITIAL_ADMIN_*` | Cuentas iniciales |
| `APP_VIEWER_TOKEN`, `APP_BARISTA_TOKEN`, `APP_ADMIN_TOKEN` | *Tokens* `Bearer` para clientes sin sesión |
| `APP_SESSION_HOURS` | Duración de la sesión |
| `RDF_ADMIN_PASSWORD` | Contraseña de administración de Fuseki |
| `RDF_JAVA_OPTIONS` | Memoria de la JVM de Fuseki (por omisión `-Xmx512m`) |
| `WEB_PORT` | Puerto de la interfaz construida (por omisión `8080`) |
| `DEV_WEB_PORT` | Puerto de Vite en desarrollo (por omisión `5173`) |

## Entrada desde la cafetera

La máquina envía lotes de mensajes a `POST /api/v1/sync` con un *token* de
escritura (`APP_BARISTA_TOKEN`). Los mensajes siguen el contrato de
[`device/software/contracts/v1/`](../../device/software/contracts/v1/README.md); la
respuesta confirma cada `messageId` recibido y la máquina borra de su cola solo lo
confirmado.

Las muestras de telemetría que llegan durante una preparación se archivan como la
serie medida de esa extracción, que es la que el comparador enfrenta a la serie
simulada.

## Desarrollo

Interfaz con recarga automática en <http://localhost:5173>:

```bash
docker compose -f compose.yaml -f compose.dev.yaml up --build -d
```

Los cambios en `frontend/` se ven al momento; si cambia el backend hay que
reconstruir su servicio (`... up -d --build api`).

## Pruebas

Backend, desde `apps/web/backend/`:

```bash
python3 -m venv .venv
.venv/bin/pip install -e '.[test]'
.venv/bin/python -m pytest tests -q
.venv/bin/python -m ruff check src tests
```

`pyproject.toml` añade `src` y `../../../packages` a la ruta de importación, de
modo que las pruebas usan los paquetes compartidos del repositorio.

Paquetes compartidos, desde la raíz del repositorio:

```bash
PYTHONPATH=packages apps/web/backend/.venv/bin/python -m pytest packages -q
```

Interfaz, desde `apps/web/frontend/`:

```bash
npm ci
npm run lint
npm run test
npm run build
```

Coherencia entre la API y los tipos de TypeScript, desde `apps/web/` (necesita el
entorno del backend y las dependencias de la interfaz):

```bash
make contract
```

## Despliegue en la Raspberry Pi

| Archivo | Función |
|---|---|
| `systemd/espresso-plataforma.service` | Lleva los servicios, con el perfil `semantic`, al estado de `compose.yaml` en cada arranque, sin reconstruir imágenes |
| `systemd/espresso-respaldo.service` y `.timer` | Ejecutan `respaldar.sh` a diario a las 03:00 |
| `respaldar.sh [destino]` | Volcado lógico de PostgreSQL, copia de los volúmenes del grafo y de las simulaciones, inventario de documentos y sumas SHA-256 |
| `restaurar.sh <carpeta>` | Comprueba las sumas, restaura la base de datos y compara el resultado con el inventario de la copia |

Las unidades suponen el usuario `espresso`, el repositorio en
`/home/espresso/espresso` y el disco de copias montado en `/mnt/datos`; cada
archivo trae en la cabecera cómo se instala. Si el disco no está montado la
el servidor arranca igual, pero la copia falla y la unidad queda en estado
`failed`. `restaurar.sh` solo restaura la base de datos; los volúmenes quedan en
la copia como `.tar.gz`.

## Estructura

```text
apps/web/
├── backend/          API, dominio, persistencia, migraciones y trabajadores
├── frontend/         Interfaz React, PWA y pruebas E2E
├── rdf-store/        Imagen del almacén RDF
├── scripts/          Comprobación del contrato OpenAPI y simulador del modelo de lenguaje para las pruebas
├── systemd/          Unidades de arranque y copia en la Raspberry Pi
├── compose.yaml      Entorno integrado
├── compose.dev.yaml  Vite con recarga automática
├── compose.test.yaml Entorno aislado de aceptación
├── respaldar.sh      Copia del servidor
├── restaurar.sh      Restauración verificada
└── Makefile          Atajos de construcción y pruebas
```
