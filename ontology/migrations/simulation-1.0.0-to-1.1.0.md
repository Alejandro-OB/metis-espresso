# Compatibilidad del módulo de simulación 1.0.0 a 1.1.0

`espresso-simulation` 1.1.0 es una ampliación aditiva para la comparación entre
una preparación y su réplica simulada. Añade `PhysicalVirtualComparison`,
`ChannelDifference` y sus propiedades de magnitud, cobertura, tolerancia y
veredicto. No elimina ni cambia el significado de ningún triple válido de 1.0.0,
por lo que no requiere un SPARQL Update sobre los datos existentes.

Esta versión del módulo no modifica EKO y sigue importando
`<https://w3id.org/espresso-dt/3.1.0>`.

## Por qué términos nuevos y no `edt:ObservedDifference`

El núcleo ya define `edt:ObservedDifference`, pero su forma exige
`edt:comparesExtraction` con dos instancias de `edt:EspressoExtraction`,
porque fue diseñada para contrastar una preparación inicial con su comprobación
dentro de un episodio de aprendizaje. Además, `esim:VirtualExtraction` es
`owl:disjointWith edt:EspressoExtraction` y su propia forma incluye
`sh:not [ sh:class edt:EspressoExtraction ]`.

Reutilizar ese término obligaría a declarar la extracción virtual como real y
rompería la separación entre lo medido y lo simulado, que es la garantía en la
que se apoya la distinción entre datos medidos y calculados. Relajar la forma del núcleo tampoco procede: la
evaluación de ajustes (`edt:hasOutcomeCategory`) depende de que compare dos
extracciones reales.

## Procedimiento

1. Conservar una copia del grafo con el módulo 1.0.0 y su versión.
2. Cargar `owl/espresso-simulation.ttl` 1.1.0 y sus formas.
3. Actualizar las sumas de `semantic-manifest-v1.json`.
4. Republicar el grafo de esquema, que el trabajador semántico reemplaza
   completo al arrancar.
5. Ejecutar las formas SHACL y `python3 ontology/tests/validate.py` desde la raíz del repositorio.

Las simulaciones publicadas con 1.0.0 siguen siendo válidas y no adquieren
comparación retroactivamente: una comparación solo existe cuando se ejecuta una
simulación en modo réplica sobre una preparación concreta.

## Condición de emisión

Una comparación solo puede publicarse si la extracción virtual declara
`esim:canBeComparedWith` sobre la extracción medida. La forma
`esim:PhysicalVirtualComparisonShape` lo comprueba con una restricción SPARQL:
compartir perfil no basta para afirmar comparabilidad, como ya establecía el
contrato de mapeo de 1.0.0.
