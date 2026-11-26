# Diagramas de la ontología

Diagramas de la ontología EKO 3.5.0 y de los casos de dominio que la ponen a
prueba. Cada diagrama se entrega en HTML o drawio, como fuente editable, y en
PNG y SVG para leerlo o insertarlo en un documento.

## Ontología

| Archivos | Contenido |
|---|---|
| `ontologia/ontologia-diagrama.html`, `.png`, `.svg` | Núcleo de la ontología, con sus clases y relaciones principales |
| `ontologia/ontologia-diagrama-integral.drawio`, `.png`, `.svg` | Mapa del esquema intensional completo, incluidas las relaciones obsoletas que se conservan por compatibilidad |
| `ontologia/recorrido-semantico-principal.drawio`, `.png`, `.svg` | Recorrido que separa la evidencia, la base de conocimiento y el agente que genera las conclusiones |

El diagrama del núcleo incluye la conformidad técnica agregada, la aceptación
sensorial explícita, `TerminationCause`, `recordedBy` y `recordingTime`. No
representa una parada por masa, porque el sistema no la tiene.

Los dos mapas generados salen de `owl/espresso-dt.ttl` con los guiones de
`ontology/scripts/`. Desde la raíz del repositorio, con `rdflib` instalado y
ImageMagick (`magick`) para el PNG:

```bash
python3 ontology/scripts/generate_integral_ontology_diagram.py
python3 ontology/scripts/generate_semantic_route_diagram.py
```

## Casos

Cada caso tiene su `caso-N/caso-N-diagrama.html`, `.png` y `.svg`.

| Caso | Qué muestra |
|---|---|
| 1 | `TechnicalDeviation`, `Diagnosis` y `ExplanationAssertion` |
| 2 | El recorrido desde los dos baristas y el perfil compartido hasta `ObservedDifference`, las desviaciones térmicas, el diagnóstico y la explicación probable |
| 4 | Tres extracciones y dos ajustes manuales de molienda: el primer cambio no tiene respuesta y el segundo recupera la masa y el tiempo dentro del perfil. Esas pruebas se formalizan con `targetsOutcome`, `evaluatesOutcomeOf` y las categorías insuficiente y esperada |
| 7 | Una calibración con acidez excesiva, un ajuste del tiempo de 28 a 31 s y amargor posterior, con puntuaciones sintéticas. La segunda extracción evalúa el ajuste, cuyo objetivo declarado es `ExcessiveAcidity`, y su resultado se clasifica como adverso sin afirmar que la acidez desapareció. Distingue los hechos del diagnóstico probable y propone una prueba de confirmación |
| 8 | Una secuencia mañana–tarde–corrección que relaciona la humedad, la condición de la cama, la subextracción, la acidez excesiva, el ajuste del molino y la evidencia posterior de recuperación |
| 9 | La diferencia entre conformidad técnica y aceptación sensorial: la extracción cumple las ocho especificaciones del perfil, pero el barista rechaza la calibración por un sabor desagradable persistente. El diagnóstico no inventa una desviación técnica ni una causa única, y propone una nueva calibración controlada |
| 10 | Tres extracciones técnicamente conformes con los mismos controles. La cama cambia de uniforme a desigual y vuelve a uniforme, y el agrado pasa de 8 a 4 y vuelve a 8. Esto sustenta la condición de la cama como origen probable del defecto, sin afirmar causalidad universal |

Los casos usan `ProfileConformanceCriterion` para la comparación determinista
entre una observación y el perfil. Los diagnósticos y las explicaciones se
atribuyen a `EvidenceBasedReasoner` y, cuando corresponde, enlazan heurísticas,
procedimientos o patrones contextuales. No dependen de condiciones causales
creadas para el caso.

Las preguntas de competencia PC22 y PC23 de
[`../queries/competency-questions.rq`](../queries/competency-questions.rq)
muestran la clasificación completa de cada extracción, sin convertir las
puntuaciones sensoriales en decisiones automáticas. La PC24 reconstruye la causa
real de finalización, el tiempo máximo del perfil y los datos externos
introducidos a mano para calcular el rendimiento.
