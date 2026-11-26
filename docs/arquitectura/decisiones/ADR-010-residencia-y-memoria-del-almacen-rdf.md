# ADR-010 — Residencia permanente y memoria acotada del almacén RDF

**Estado:** Aceptada (2026-09-21)

## 1. Contexto

ADR-004 sitúa la ontología, el grafo consultable y el razonamiento en la
plataforma externa, fuera del firmware y del lazo de control. En
`platform/web/compose.yaml`, los servicios `rdf-store` y `semantic-worker` están
tras el perfil `semantic` para que el historial opere aunque el almacén RDF
falle. Eso es tolerancia a fallo, no exclusión del alcance.

Sin límite declarado, la máquina virtual de Java toma como memoria dinámica
máxima el 25 % de la memoria física (`MaxRAMPercentage`). En el equipo de
desarrollo, de 27,29 GiB, eso dio 6,82 GiB, y el servicio en reposo osciló entre
200,4 MiB y 3,52 GiB. El resto del sistema en reposo consume 162 MiB.

Con 512 MiB declarados, el contenedor ocupó 181,3 MiB al arrancar y 386,5 MiB
tras publicar 200 grafos nombrados (30 404 tripletas), a 42 ms por grafo. El
volumen de datos no explica el consumo: la ontología ocupa 236 KB y cada
preparación añade un grafo pequeño. TDB2 guarda los datos en archivos mapeados
fuera de la memoria dinámica, que solo sostiene consultas y cargas. El arranque
en frío de la máquina virtual tarda decenas de segundos.

## 3. Alternativas descartadas

| Alternativa | Razón |
|---|---|
| Residente sin límite declarado | El máximo depende del equipo; una medición no se traslada a la Raspberry Pi ni es comparable entre pruebas, y un fallo de memoria no distinguiría el hardware del ajuste por omisión. |
| Bajo demanda | La primera consulta del barista pagaría el arranque de la máquina virtual, y haría falta una orquestación de encendido que no existe. |
| Fuera de la instalación | Contradice ADR-004 y deja sin consulta de evidencia el equipo desplegado. |

## 4. Decisión

El almacén RDF y el trabajador semántico son **servicios residentes**: se
levantan con la instalación y permanecen disponibles mientras está en servicio.
El perfil `semantic` no gobierna su presencia en producción.

El almacén se ejecuta con una **memoria dinámica máxima declarada de 512 MiB**,
mediante `JDK_JAVA_OPTIONS` en la composición. Si la validación SHACL, la
publicación de grafos o una consulta de comparación no caben, el límite sube a
1 GiB y se registra la medición que lo obliga.

Si el almacén no responde, la aplicación sigue sirviendo el historial y declara
el estado semántico. La falta de respuesta del grafo nunca se presenta como
ausencia de hechos.

## 5. Justificación

La consulta de evidencia no puede esperar el arranque de una máquina virtual.
Con el límite declarado, el consumo es comparable entre pruebas y equipos. Como
TDB2 mantiene los datos fuera de la memoria dinámica, acotarla limita el trabajo
de consulta, no el tamaño del grafo.

## 6. Consecuencias

- La instalación mantiene una máquina virtual de Java encendida de forma
  permanente.
- Un límite demasiado ajustado puede interrumpir una consulta compleja; por eso
  se mide la latencia con sus valores extremos, no solo la mediana.

## 9. Verificación de la decisión

1. Consumo en reposo de los servicios residentes por debajo de 1 GiB.
2. Validación SHACL de una preparación completa sin agotar la memoria.
3. Publicación de un grafo y consulta de su evidencia.
4. Latencia de consulta registrada con sus valores extremos.
5. Historial operativo con el almacén detenido.
6. Sin crecimiento sostenido de memoria en una prueba prolongada.

## 10. Trazabilidad

[ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md),
[ADR-003](ADR-003-interfaz-web-unica-en-plataforma-externa.md),
[ADR-004](ADR-004-distribucion-datos-ontologia-aprendizaje.md).

## 11. Criterios de reconsideración

Se revisa si el grafo exige más memoria de la que la plataforma puede reservar
de forma permanente, si la instalación no sostiene el servicio junto con la
simulación y el historial, o si el almacén se sustituye por uno que no dependa
de una máquina virtual de Java.

## 12. Empaquetado del almacén RDF compatible con ARM

La imagen `stain/jena-fuseki` solo se publica para `linux/amd64`, así que no se
ejecuta en la Raspberry Pi. El resto de las imágenes del sistema sí se publica
para `linux/arm64`.

| Alternativa | Razón |
|---|---|
| `secoresearch/fuseki` | Publica `arm64`, pero usa TDB1, añade un índice Lucene y clases propias, y nombra el conjunto `ds`. Habría que sustituir su configuración completa, que es lo que aportaba. |
| Otro motor RDF | Obligaría a revalidar las formas SHACL y las consultas de competencia. |

El almacén se empaqueta como **imagen propia**, definida en
[`platform/web/rdf-store/`](../../../platform/web/rdf-store/): Apache Jena
Fuseki 5.5.0 descargado del archivo de Apache con verificación SHA-512, sobre
`eclipse-temurin:21-jre-alpine`, con TDB2, el conjunto `espresso`,
autenticación de administración y ejecución como usuario sin privilegios. La
imagen no compila código, solo descarga, verifica y extrae, por lo que se
construye directamente en la Raspberry Pi.

Con el cliente `espresso_semantics.FusekiClient`, la imagen respondió en unos
2 s, sustituyó un grafo en 56 ms, devolvió la evidencia en 13 ms, republicó sin
duplicar y rechazó la administración sin credenciales (401). Ocupa 399 MB.

Consecuencias: la versión de Fuseki se actualiza de forma deliberada, con su
suma de verificación. Un volumen creado por la imagen anterior pertenece a
`root` y debe reasignarse antes de reutilizarse.
