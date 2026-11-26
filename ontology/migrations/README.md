# Migración de EKO 2.7.1 a 3.0.0

`2.7.1-to-3.0.0.ru` migra datos de instancia que utilicen la clase `Rule`,
`generatedByRule`, `recommendedByRule` y los ocho individuos obsoletos.

La migración separa criterios, heurísticas, procedimientos, patrones de
evidencia y políticas. No utiliza equivalencias OWL porque la correspondencia
es contextual y uno-a-varios.

## Procedimiento

1. Conservar una copia inmutable del grafo 2.7.1.
2. Aplicar el archivo SPARQL Update al grafo de **datos**, no al archivo que
   define la ontología 3.0.0.
3. Cargar `owl/espresso-dt.ttl` 3.0.0.
4. Ejecutar las formas de `owl/espresso-dt-shapes.ttl`.
5. Ejecutar `python tests/validate.py`.
6. Confirmar que el grafo de datos no use `rdf:type edt:Rule`,
   `edt:generatedByRule` ni `edt:recommendedByRule`.

El script es idempotente sobre datos migrados: una segunda ejecución no
encuentra triples 2.7.1 y no modifica el resultado.

## Actualización 3.0.0 a 3.1.0

La ampliación de perfiles sensoriales es aditiva y no requiere reescritura
automática. Consulte `3.0.0-to-3.1.0.md` para las comprobaciones y para evitar
convertir evaluaciones observadas en declaraciones u objetivos sin procedencia.

## Actualización 3.1.0 a 3.2.0

Añade la causa de finalización `UnexpectedPowerLoss` y permite que una
extracción cortada por un corte de energía se publique sin fin, y sin inicio si
no constaba. Es compatible con los datos 3.1.0. Consulte `3.1.0-to-3.2.0.md`.

## Actualización 3.2.0 a 3.3.0

Un objetivo sin tolerancia deja de leerse como exacto: la especificación no se
juzga y la conformidad queda inconclusa. Los datos no cambian; las conclusiones
sí. Consulte `3.2.0-to-3.3.0.md`.
