# Migraciones de la ontología

Cada archivo describe el paso de una versión de EKO, o del módulo de simulación,
a la siguiente: qué cambia, si los datos existentes siguen siendo válidos y cómo
se actualiza un grafo. Las rutas `owl/…` se refieren a la carpeta `ontology/`.

| Archivo | Cambio |
|---|---|
| `2.7.1-to-3.0.0.ru` | SPARQL Update que separa la clase `Rule` en criterios, heurísticas, procedimientos, patrones de evidencia y políticas |
| [`3.0.0-to-3.1.0.md`](3.0.0-to-3.1.0.md) | Perfiles sensoriales declarados y objetivo; aditiva |
| [`3.1.0-to-3.2.0.md`](3.1.0-to-3.2.0.md) | Causa de finalización `UnexpectedPowerLoss`; una extracción cortada por un corte de energía se publica sin fin, y sin inicio si no constaba |
| [`3.2.0-to-3.3.0.md`](3.2.0-to-3.3.0.md) | Un objetivo sin tolerancia no se juzga y deja la conformidad inconclusa; cambian las conclusiones, no los datos |
| [`3.3.0-to-3.4.0.md`](3.3.0-to-3.4.0.md) | Temperatura del cuerpo de la caldera separada de la del agua, observaciones estimadas y reglas de orientación |
| [`3.4.0-to-3.5.0.md`](3.4.0-to-3.5.0.md) | Ficha de cata alineada con la CVA de la SCA y retiro de los términos sin uso |
| [`simulation-1.0.0-to-1.1.0.md`](simulation-1.0.0-to-1.1.0.md) | Comparación entre una preparación y su réplica simulada |

## De 2.7.1 a 3.0.0

`2.7.1-to-3.0.0.ru` migra los datos de instancia que usan la clase `Rule`,
`generatedByRule`, `recommendedByRule` y los ocho individuos obsoletos. No usa
equivalencias OWL, porque la correspondencia depende del contexto y es de uno a
varios.

1. Conservar una copia inmutable del grafo 2.7.1.
2. Aplicar el archivo SPARQL Update al grafo de **datos**, no al archivo que
   define la ontología 3.0.0.
3. Cargar `owl/espresso-dt.ttl` 3.0.0.
4. Ejecutar las formas de `owl/espresso-dt-shapes.ttl`.
5. Ejecutar `python3 ontology/tests/validate.py` desde la raíz del repositorio.
6. Confirmar que el grafo de datos no usa `rdf:type edt:Rule`,
   `edt:generatedByRule` ni `edt:recommendedByRule`.

El guion es idempotente: una segunda ejecución no encuentra triples 2.7.1 y no
modifica el resultado. `ontology/tests/validate.py` también lo aplica sobre datos
de prueba.
