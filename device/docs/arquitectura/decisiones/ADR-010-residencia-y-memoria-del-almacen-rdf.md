# ADR-010 — Residencia permanente y memoria acotada del almacén RDF

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Fecha de revisión:** 2026-09-15 (§12: empaquetado compatible con ARM)
**Ámbito:** presencia del almacén RDF en la plataforma externa, consumo de memoria en reposo y condiciones de arranque

## 1. Contexto

ADR-004 sitúa la ontología, el grafo consultable y el razonamiento en la
plataforma externa local, fuera del firmware y fuera del lazo de control.
ADR-003 establece que la aplicación web es la única interfaz del sistema y que
debe presentar la evidencia semántica junto con su procedencia. La plataforma
externa será, según ADR-001, un equipo local candidato a Raspberry Pi, cuyo
dimensionamiento debe comprobarse con mediciones de memoria, CPU,
almacenamiento y temperatura.

El despliegue actual no refleja todavía esa condición. En
`apps/web/compose.yaml`, los servicios `rdf-store` y `semantic-worker` están
tras el perfil `semantic`, de modo que no se levantan salvo que se pidan de
forma expresa. El ADR de plataforma de la aplicación web justifica esa
separación porque el historial debe poder operar aunque el almacén RDF esté
degradado. Esa justificación describe una **tolerancia a fallo**, no una
exclusión del alcance: el grafo sigue perteneciendo a la plataforma externa.

Falta por tanto decidir dos cosas que hoy están implícitas en un archivo de
composición: si el almacén RDF es un servicio residente de la instalación, y
con cuánta memoria se ejecuta.

### 1.1 Evidencia medida

Mediciones tomadas el 2026-09-15 sobre el **entorno de desarrollo**, no sobre la
Raspberry Pi: computador x86_64 con 27,29 GiB de memoria, proyecto Compose
`aplicacion-web` en ejecución.

**Comportamiento sin límite declarado.** La máquina virtual de Java adopta como
memoria dinámica máxima el 25 % de la memoria física del equipo, valor que ella
misma deduce del hardware. Comprobado dentro del contenedor con
`java -XX:+PrintFlagsFinal -version`:

| Parámetro | Valor observado |
|---|---|
| `MaxRAMPercentage` | 25 % (valor por omisión) |
| `MaxHeapSize` | 6,82 GiB (deducido del equipo) |

El consumo real oscila dentro de ese margen. El mismo servicio, sin ninguna
solicitud en curso, se observó en **3,52 GiB** poco después de un periodo de
actividad y en **200,4 MiB** tras un intervalo prolongado sin solicitudes,
porque el recolector de memoria devuelve al sistema lo que ya no retiene. No
existe, por tanto, una cifra estable de consumo en reposo: existe un intervalo
cuyo extremo superior lo fija el hardware donde se ejecute el servicio.

Para comparar, el resto del sistema en reposo consume 162 MiB en total: 53,6 MiB
el trabajador semántico, 42,3 MiB el de simulación, 36,5 MiB la API, 16,1 MiB
PostgreSQL y 14,0 MiB el servidor web.

**Comportamiento con límite declarado.** Una instancia equivalente ejecutada con
una memoria dinámica máxima de 512 MiB declarada de forma explícita:

| Situación | Memoria del contenedor |
|---|---|
| Arranque en reposo | 181,3 MiB |
| Tras publicar 200 grafos nombrados (30 404 tripletas) | 386,5 MiB |

En esa misma prueba, la publicación de cada grafo tardó 42 ms, el recuento
global de tripletas 38 ms y una agregación sobre todos los grafos 53 ms, sin
agotar la memoria declarada.

La causa del consumo elevado no es el volumen de datos: el vocabulario, las
formas de validación y las consultas ocupan 236 KB en `knowledge/ontology/`, y
cada preparación añade un grafo nombrado pequeño.

Dos hechos adicionales condicionan la decisión. El almacenamiento TDB2 de Jena
mantiene los datos en archivos mapeados en memoria, **fuera** de la memoria
dinámica de la máquina virtual, que solo debe sostener el procesamiento de
consultas y la carga de grafos. Y el arranque en frío de una máquina virtual de
Java se mide en decenas de segundos, plazo que quedaría delante de la primera
consulta del barista si el servicio se levantara bajo demanda.

## 2. Problema de decisión

Se debe definir si el almacén RDF permanece encendido durante toda la operación
de la instalación y con qué memoria dinámica se ejecuta, de manera que la
consulta de evidencia sea interactiva, el consumo en reposo sea un parámetro
declarado y el dimensionamiento de la plataforma externa pueda comprobarse de
forma reproducible.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Residente sin límite declarado | Es el comportamiento actual. El máximo depende de la memoria física del equipo y el consumo oscila dentro de ese margen, por lo que una medición no es trasladable a otro modelo de Raspberry Pi ni comparable entre ensayos. Un veredicto negativo sobre la plataforma podría deberse al ajuste por omisión y no a una limitación real. |
| Residente con memoria acotada | Conserva la respuesta interactiva y convierte el consumo en reposo en un valor elegido y defendible. Exige comprobar que la validación y las consultas caben en el límite fijado. **Seleccionada.** |
| Bajo demanda | Libera memoria mientras nadie consulta, pero traslada el arranque de la máquina virtual a la primera consulta del barista y exige una orquestación de encendido y apagado que hoy no existe. |
| Fuera de la instalación | Reduce el consumo a cero, pero contradice ADR-004 y deja sin soporte la consulta de evidencia y las recomendaciones trazables en el equipo desplegado. |

## 4. Decisión

El almacén RDF y el trabajador semántico serán **servicios residentes** de la
plataforma externa: se levantan con la instalación y permanecen disponibles
mientras el sistema está en servicio. El perfil `semantic` deja de gobernar su
presencia en el despliegue de producción y conserva su utilidad únicamente como
recurso del entorno de desarrollo.

El almacén RDF se ejecutará con su **memoria dinámica máxima declarada de forma
explícita**, con un valor inicial de **512 MiB**, suficiente en la comprobación descrita en §1.1. El valor efectivo formará
parte de la configuración de la instalación y no podrá quedar determinado por
la memoria física del equipo.

Si durante la campaña de dimensionamiento la validación SHACL, la publicación
de grafos o una consulta de comparación no caben en ese límite, el valor se
elevará a 1 GiB y quedará registrada la medición que lo obliga. Elevar el
límite es un resultado de medición documentado, no una corrección silenciosa.

La tolerancia a fallo descrita en el ADR de plataforma de la aplicación web se
mantiene sin cambios: la aplicación debe seguir sirviendo el historial y
declarar el estado semántico cuando el almacén no responda, y la ausencia de
respuesta del grafo nunca podrá presentarse como ausencia de hechos.

## 5. Justificación

1. **Respuesta interactiva.** La consulta de evidencia forma parte de la
   navegación normal de una preparación; no puede pagar el arranque de una
   máquina virtual.
2. **Mediciones reproducibles.** Con el límite declarado, el consumo en reposo
   del equipo es comparable entre ensayos y entre equipos, que es la condición
   para que el dimensionamiento de la plataforma externa signifique algo.
3. **Honestidad del veredicto.** Sin límite declarado, un fallo de memoria en
   la Raspberry Pi no permitiría distinguir entre una limitación del hardware y
   un ajuste por omisión heredado del entorno de desarrollo.
4. **Coherencia con el almacenamiento.** TDB2 sitúa los datos en archivos
   mapeados fuera de la memoria dinámica, de modo que acotarla no limita el
   tamaño del grafo sino el trabajo simultáneo de consulta.
5. **Cumplimiento de ADR-004.** El grafo consultable pertenece a la plataforma
   externa; dejarlo apagado por omisión lo convertía de hecho en una
   herramienta de desarrollo.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- El consumo en reposo de la instalación pasa a ser un valor declarado.
- La consulta de evidencia responde sin arranque en frío.
- El dimensionamiento de la plataforma externa se vuelve comparable y repetible.
- La configuración deja de depender del equipo donde se ejecute.

### 6.2 Consecuencias desfavorables

- La instalación mantiene una máquina virtual de Java encendida de forma
  permanente, con su consumo asociado.
- Un límite demasiado ajustado puede degradar o interrumpir una consulta
  compleja, lo que obliga a comprobarlo experimentalmente.
- El despliegue de producción deja de coincidir con la invocación de desarrollo
  y debe documentarse por separado.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Agotamiento de memoria dinámica durante una validación o consulta | Medir la proyección, la validación SHACL y las consultas de comparación con el límite fijado; elevarlo a 1 GiB documentando la medición. |
| Pausas de recolección de basura perceptibles en la interfaz | Incluir la latencia de consulta en la campaña y registrar sus valores extremos, no solo la mediana. |
| Crecimiento del grafo con el número de preparaciones | Medir con un volumen representativo y tratar la retención en la decisión de almacenamiento correspondiente. |
| Confusión entre servicio caído y ausencia de hechos | Ya tratada en el adaptador semántico, que distingue ambos casos de forma explícita; se conserva. |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Construcción de la imagen en la plataforma externa | Comprobar la construcción y ejecución en la Raspberry Pi; en el equipo de desarrollo no fue posible por falta de emulación registrada (§12.4). |
| Valor definitivo de la memoria dinámica | Confirmar 512 MiB o elevarlo, con la medición que lo respalde. |
| Retención y crecimiento del grafo | Definir el volumen conservado y su efecto sobre memoria y almacenamiento. |
| Ejecución del razonador | Decidir si la materialización de conocimiento se ejecuta en la instalación y con qué disparador. |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. consumo en reposo del conjunto de servicios residentes por debajo de 1 GiB;
2. proyección semántica y validación SHACL de una preparación completa sin
   agotar la memoria dinámica declarada;
3. publicación de un grafo nombrado y consulta posterior de su evidencia;
4. latencia de consulta de evidencia registrada con sus valores extremos;
5. operación del historial con el almacén RDF detenido, declarando el estado
   semántico y sin presentar ausencia de hechos;
6. permanencia del servicio durante un ensayo prolongado, sin crecimiento
   sostenido de memoria.

## 10. Trazabilidad

- [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md): responsabilidades
  de la plataforma externa y comprobación de sus recursos.
- [ADR-003](ADR-003-interfaz-web-unica-en-plataforma-externa.md): interfaz única
  y funcionamiento degradado.
- [ADR-004](ADR-004-distribucion-datos-ontologia-aprendizaje.md): ubicación del
  grafo, el razonamiento y el conocimiento derivado.
- Arquitectura de despliegue: artefactos y
  comunicaciones de la plataforma externa.
- Alcance de producción de la plataforma externa:
  servicios residentes y trabajo episódico.
- Requisitos: RF-029, RNF-007 y RNF-008.

## 11. Criterios de reconsideración

La decisión deberá revisarse si el grafo crece hasta exigir una memoria que la
plataforma externa no pueda reservar de forma permanente, si la campaña de
dimensionamiento demuestra que la instalación no puede sostener el servicio
residente junto con la simulación y el historial, o si se sustituye el almacén
RDF por una tecnología cuyo consumo no dependa de una máquina virtual de Java.

## 12. Revisión · Empaquetado del almacén RDF compatible con ARM

La decisión anterior presupone que el almacén RDF puede ejecutarse en la
plataforma externa. Al comprobarlo resultó que no podía, de modo que esta
revisión cierra esa condición.

### 12.1 Hallazgo

La imagen fijada en la composición, `stain/jena-fuseki`, publica **únicamente**
`linux/amd64`. Verificado sobre el manifiesto del registro con
`docker buildx imagetools inspect`. El resto de las imágenes del sistema
—PostgreSQL, Python, nginx y Node— sí publican `linux/arm64`, de modo que el
almacén RDF era el único impedimento para levantar la plataforma completa en una
Raspberry Pi.

### 12.2 Alternativas comprobadas

| Alternativa | Arquitecturas | Evaluación |
|---|---|---|
| `stain/jena-fuseki` | solo `amd64` | No ejecutable en la plataforma externa. |
| `secoresearch/fuseki` | `amd64` y `arm64` en sus etiquetas de versión, no en `latest` | Ejecutable, pero su configuración usa TDB1, añade un índice Lucene, carga clases propias del grupo que la mantiene y nombra el conjunto de datos `ds`, con la consulta en `sparql`. El despliegue actual usa TDB2, el conjunto `espresso` y los extremos `query`, `sparql`, `data` y `get`: habría que sustituir su configuración completa, que es justamente lo que aportaba. |
| Imagen propia con el paquete oficial de Apache Jena sobre `eclipse-temurin` | base con `amd64` y `arm64` | Conserva Fuseki, TDB2 y la configuración vigente sin componentes ajenos al sistema. **Seleccionada.** |
| Sustituir el motor por otro almacén RDF | — | Obligaría a revalidar formas SHACL y consultas de competencia, y contradice la decisión de plataforma de la aplicación web. Descartada. |

### 12.3 Decisión

El almacén RDF se empaqueta como **imagen propia del proyecto**, definida en
`apps/web/rdf-store/`: paquete oficial de Apache Jena Fuseki 5.5.0 descargado
del archivo de la fundación con verificación SHA-512, sobre
`eclipse-temurin:21-jre-alpine`, con la configuración TDB2 del conjunto
`espresso` ya vigente, autenticación de administración y ejecución como usuario
sin privilegios.

### 12.4 Verificación realizada

Comprobado el 2026-09-15 en el equipo de desarrollo, ejercitando el cliente real
del proyecto (`espresso_semantics.FusekiClient`) y no solicitudes construidas
para la prueba:

| Comprobación | Resultado |
|---|---|
| Arranque hasta responder | ≈2 s, declarando la memoria dinámica fijada |
| Comprobación de salud | correcta |
| Sustitución de un grafo nombrado | 56 ms |
| Consulta SPARQL y recuperación de evidencia | 13 ms, hechos correctos |
| Republicación del mismo grafo | correcta, sin duplicación |
| Administración sin credenciales | rechazada (401); con credenciales, aceptada |
| Carga de 200 grafos nombrados (30 404 tripletas) | 8,5 s; 42 ms por grafo |
| Agregación sobre todos los grafos | 53 ms |
| Memoria tras esa carga | 386,5 MiB, dentro del límite declarado |
| Tamaño de la imagen | 399 MB, frente a 486 MB de la anterior |

Integración en el despliegue existente: el volumen `rdf-data` conservó sus 2 061
tripletas, el trabajador semántico reanudó su operación sin errores y volvió a
publicar el grafo de esquema.

**No verificado todavía:** la construcción para ARM. El equipo de desarrollo no
tiene registrada la emulación necesaria y la construcción cruzada falla con
`exec format error`. La imagen no compila código —descarga, verifica y
extrae—, por lo que puede construirse directamente en la Raspberry Pi; queda
como comprobación de la campaña de dimensionamiento.

### 12.5 Consecuencias

- El proyecto mantiene una definición de imagen propia y debe actualizar la
  versión de Fuseki de forma deliberada, con su suma de verificación.
- El servicio se ejecuta como usuario sin privilegios: un volumen creado por la
  imagen anterior pertenece a `root` y debe reasignarse antes de reutilizarse.
- La memoria dinámica se declara desde la composición mediante
  `JDK_JAVA_OPTIONS`, lo que hace aplicable la decisión de §4 a esta imagen.
- Desaparece la dependencia de una imagen de terceros para un componente que
  forma parte del alcance permanente de la plataforma externa.
