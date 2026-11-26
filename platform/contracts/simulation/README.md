# Contratos de simulación v1

Esquemas JSON Schema 2020-12 que fijan la frontera entre la aplicación web, el
ejecutor FMI (`platform/packages/espresso_simulation`), el almacén de series y el
procesador semántico.

| Esquema | Contenido |
|---|---|
| `model-registry.schema.json` | Registro de artefactos FMU autorizados |
| `simulation-request.schema.json` | Solicitud de simulación que acepta la API |
| `profile-snapshot.schema.json` | Perfil resuelto e inmutable con que se ejecuta |
| `controller.schema.json` | Controlador virtual reproducible; los de `model/controllers/` lo cumplen |
| `simulation-run.schema.json` | Ciclo de vida del trabajo |
| `simulation-sample.schema.json` | Punto de una serie, extensible por canales |
| `simulation-artifact.schema.json` | Manifiesto del archivo inmutable de la serie |
| `simulation-summary.schema.json` | Resultados calculados |
| `semantic-publication.schema.json` | Publicación RDF y su validación SHACL |

## Compatibilidad

Los esquemas rechazan propiedades no declaradas (`additionalProperties: false`),
y los productores escriben siempre una versión conocida. Un cambio incompatible
crea una versión mayor nueva en una carpeta propia, no modifica esta.
