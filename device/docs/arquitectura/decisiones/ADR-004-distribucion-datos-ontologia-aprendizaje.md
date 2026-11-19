# ADR-004 — Distribución de datos, conocimiento semántico, razonamiento y aprendizaje

**Estado:** Aceptada, revisada  
**Fecha de decisión original:** 2026-08-23  
**Fecha de revisión:** 2026-09-09; 2026-09-15 (§11: alcance del razonador en el prototipo desplegado; §12: régimen de aprendizaje); 2026-09-30 (§13: activación del objetivo como decisión explícita del sostenimiento)
**Ámbito:** datos operativos, conocimiento semántico, razonamiento, modelos y recomendaciones  
**Reemplaza:** la formulación inicial del mismo ADR que no separaba suficientemente la ontología del razonador y atribuía toda recomendación a un modelo de aprendizaje

## 1. Contexto

El proyecto genera estados operativos, series temporales, configuraciones,
perfiles, indicadores, datos opcionales del café, evaluaciones sensoriales,
diagnósticos, explicaciones, recomendaciones y, en una etapa posterior,
artefactos de aprendizaje. Estos elementos tienen volúmenes, estructuras,
procedencias y ciclos de vida diferentes.

La ontología canónica del proceso de extracción se mantiene en
`knowledge/ontology/ontology/espresso-dt.ttl`. Su versión vigente (EKO 3.1.0) diferencia observaciones, desviaciones
técnicas, diagnósticos probables, explicaciones, recomendaciones, decisiones
humanas y evaluaciones posteriores. También utiliza un razonador de aplicación
externo para comparar evidencias y materializar conocimiento que no se obtiene
únicamente mediante inferencia OWL.

La cafetera debe funcionar sin servidor. El historial completo, la
transformación semántica, las consultas del grafo, el razonamiento de
aplicación y el aprendizaje requieren recursos externos y no forman parte del
lazo de control. Estas responsabilidades se ejecutarán inicialmente en la
plataforma externa local, candidata a Raspberry Pi, de acuerdo con ADR-001 y
ADR-003.

## 2. Problema de decisión

Se debe definir:

- dónde reside cada tipo de información;
- cuál es la fuente autorizada de cada dato;
- cómo se relacionan los almacenes sin duplicar las series en RDF;
- cómo se distinguen hechos, conocimiento definido, conclusiones probables,
  decisiones humanas y asociaciones aprendidas;
- qué componente produce cada diagnóstico o recomendación;
- y qué frontera impide que los servicios externos actúen directamente sobre
  la cafetera.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Almacenar todo como RDF | Unifica la representación, pero resulta ineficiente para series temporales, archivos y artefactos numéricos; además, confunde datos primarios con conocimiento derivado. |
| Utilizar únicamente una base de datos convencional | Simplifica la persistencia, pero no satisface la navegación semántica, la procedencia del conocimiento ni las consultas requeridas por la tesis. |
| Integrar el razonamiento dentro de la ESP32-S3 | Reduce componentes externos, pero consume recursos del dispositivo, mezcla conocimiento con control y compromete su autonomía operativa. |
| Separar datos, grafo semántico, razonador y modelos | Permite utilizar cada componente para su propósito, conservar trazabilidad y mantener los servicios externos fuera del control. Alternativa seleccionada. |

## 4. Decisión

Se adopta una arquitectura distribuida por responsabilidad. La siguiente
separación distingue dónde se guardan los datos, qué servicios los procesan y
qué componente los presenta.

### 4.1 Almacenamiento

| Lugar | Contenido |
|---|---|
| RAM o PSRAM de la ESP32-S3 | Muestras, estados y buffers temporales de la extracción en curso. La variante confirmada dispone de 8 MB de PSRAM. |
| NVS dentro de la flash de la ESP32-S3 | Identidad del dispositivo, credenciales, configuración mínima, límites protegidos, calibraciones, configuración efectiva y estado de recuperación. La tabla vigente reserva 256 KiB para NVS. |
| Flash de la ESP32-S3 fuera de NVS | Firmware y cola acotada de preparaciones pendientes de sincronización. La variante confirmada dispone de 16 MB de flash y la partición de aplicación reserva 3 MiB. |
| Base histórica y almacenamiento de objetos en la plataforma externa local | Registro consolidado de preparaciones, perfiles, café, evaluaciones, eventos, indicadores, series temporales completas, exportaciones y conjuntos de datos. La plataforma es candidata a Raspberry Pi. |

### 4.2 Servicios de conocimiento y análisis

| Servicio o artefacto | Responsabilidad |
|---|---|
| Ontología canónica | Define los conceptos, relaciones, restricciones y vocabularios del dominio. Su fuente autorizada es `knowledge/ontology/ontology/espresso-dt.ttl` (EKO 3.1.0). |
| Grafo semántico de ejecución | Conserva hechos transformados a RDF, referencias a datos externos y conocimiento materializado con procedencia y estado explícitos. |
| Servicio de razonamiento externo | Valida la información, compara evidencias, detecta desviaciones y materializa diagnósticos, explicaciones y recomendaciones de comprobación. |
| Repositorio de modelos | Conserva versiones, parámetros, métricas, datos o referencias de entrenamiento y artefactos de inferencia cuando exista aprendizaje validado. Es una responsabilidad lógica y no exige inicialmente un producto independiente. |

Estos servicios se ejecutan en la plataforma externa local y permanecen fuera
del lazo de control.

### 4.3 Interfaz

| Componente | Responsabilidad |
|---|---|
| Aplicación web externa | Única interfaz web del sistema. Consulta el historial, el grafo y los resultados de razonamiento o aprendizaje; presenta la evidencia y registra las decisiones del barista. |

La distribución física puede concentrarse inicialmente en la Raspberry Pi,
pero las responsabilidades permanecen separadas. La base histórica conservará
los metadatos y relaciones; el almacenamiento de objetos conservará series y
archivos grandes; el grafo RDF conservará hechos, relaciones, referencias y
conocimiento materializado; y el repositorio de modelos conservará sus
versiones y metadatos. La ESP32-S3 no conservará el historial completo, el
grafo, los modelos ni las series de alta frecuencia.

La tecnología concreta de base de datos, almacenamiento de objetos, grafo RDF
y repositorio de modelos se decidirá por separado. Una plataforma puede asumir
varias responsabilidades lógicas, siempre que preserve sus contratos,
identidades y fronteras de autoridad.

### 4.4 Fuentes autorizadas

| Información | Fuente autorizada |
|---|---|
| Estado actual, configuración efectiva y alarmas | Firmware de la ESP32-S3. |
| Registro histórico confirmado y series completas | Base histórica y almacenamiento de objetos externos. |
| Definición del dominio | Versión identificada de la ontología canónica. |
| Hechos semánticos de una preparación | Transformación trazable del registro histórico confirmado. |
| Conocimiento materializado | Ejecución identificada del razonador sobre una versión declarada de hechos, ontología y método. |
| Perfil sensorial declarado | Fuente comercial, productor o tostador identificado; se conserva también el texto original. |
| Objetivo sensorial | Declaración explícita del barista para una prueba identificada. |
| Evaluación sensorial observada | Declaración explícita del evaluador, con instante y protocolo, sin copiarla del perfil declarado. |
| Decisión sensorial o validación de una recomendación | Declaración explícita del barista registrada sin inferirla de puntuaciones. |
| Asociación o recomendación aprendida | Modelo identificado y validado, junto con la evidencia y condiciones en que fue aplicado. |

Un dato derivado no modificará retrospectivamente los hechos utilizados para
producirlo. Los diagnósticos, explicaciones y recomendaciones podrán
recalcularse al cambiar la ontología o el razonador, conservando la versión
anterior cuando haya sido utilizada para una decisión.

El tiempo máximo pertenece a la configuración o perfil y la causa real de
finalización pertenece a la preparación ejecutada. El firmware produce esos
hechos durante la operación. La dosis, la masa final de bebida y el TDS son
observaciones externas introducidas manualmente; la ontología puede
representarlas, conservar su procedencia y relacionarlas con el rendimiento,
pero no decide cuándo apagar la bomba ni infiere retrospectivamente la causa de
corte.

### 4.5 Separación entre ontología, grafo y razonador

La ontología define el significado y las restricciones del dominio, pero no
ejecuta por sí sola las comparaciones multivariable ni el razonamiento
contrastivo requerido para explicar una extracción. El servicio externo:

1. recibe registros confirmados de la base histórica;
2. los transforma en hechos compatibles con la ontología;
3. valida estructura y consistencia;
4. compara observaciones, especificaciones, contexto, secuencias y evaluación
   sensorial;
5. materializa nuevo conocimiento en el grafo con evidencia, agente, método,
   instante y nivel de conclusión;
6. expone consultas trazables a la aplicación externa.

El grafo podrá referenciar series mediante identificadores o URI, por ejemplo
mediante `dataReference`, pero no almacenará cada muestra de alta frecuencia
como un triple individual. Las series completas permanecerán en el almacén
externo apropiado.

### 4.6 Tipos y procedencia del conocimiento

El sistema distinguirá, como mínimo:

| Tipo | Ejemplo | Procedencia requerida |
|---|---|---|
| Hecho registrado | Una temperatura observada o un defecto declarado por el barista. | Preparación, instante, método o responsable y estado de calidad. |
| Regla definicional | Comparar una observación con el límite de su perfil. | Regla, versión y observación y especificación utilizadas. |
| Conocimiento experto o procedimiento | Calibrar modificando un parámetro por prueba. | Fuente experta o bibliográfica, alcance y versión. |
| Conclusión basada en evidencia | Diagnóstico probable obtenido por contraste entre extracciones. | Evidencias, razonador, versión del método, fecha y límites de la conclusión. |
| Causa demostrada | Relación confirmada mediante un diseño experimental suficiente. | Experimento, controles, análisis y criterio de validación. |
| Asociación aprendida | Patrón estimado desde un conjunto de datos. | Modelo, versión, datos, métricas, incertidumbre y condiciones de aplicabilidad. |
| Decisión humana | Aceptación sensorial o validación de una recomendación. | Barista, fecha, estado y justificación cuando corresponda. |

Una coincidencia temporal, una comparación o una asociación aprendida no se
presentará como causa demostrada. Una explicación probable deberá declarar
qué evidencia la respalda y qué no permite concluir.

### 4.7 Identidad y referencias entre almacenes

Se utilizarán identificadores estables, como mínimo, para:

- dispositivo;
- barista o evaluador, sujeto a la política de privacidad;
- preparación;
- perfil, configuración y versión;
- referencia de café;
- calibración;
- serie y observación resumida;
- evaluación sensorial;
- diagnóstico, explicación y recomendación;
- ajuste y evaluación de su resultado;
- versiones de firmware, ontología, razonador, método de cálculo y modelo
  cuando hayan influido en el resultado.

La base histórica conservará las identidades y localizaciones físicas de los
datos. El grafo utilizará esas mismas identidades o referencias estables para
navegar el conocimiento sin copiar las series completas.

### 4.8 Representación del café

Cada preparación se vinculará con una referencia interna de café generada por
el sistema. Si el barista no proporciona metadatos, la referencia permanecerá
sin caracterización adicional y no se asignarán valores supuestos.

Los metadatos opcionales aprobados son:

- variedad o cultivar;
- proceso poscosecha;
- nivel de tueste;
- lote, bolsa o referencia comercial;
- fecha de tueste;
- terroir o terruño, compuesto opcionalmente por región, altitud y composición
  mineral o características del suelo.

La ontología 3.1.0 ya representa estos elementos y separa el perfil sensorial
declarado, el objetivo del barista y la evaluación observada. La implementación del
sistema deberá definir su esquema de persistencia y la transformación hacia
RDF. La información mineral solo se conservará cuando se conozca su fuente.

La molienda, la masa de café seco y el molino pertenecen al contexto de la
preparación o del equipo, no a la descripción intrínseca del café. El agua y la
canasta permanecen fuera del registro de contexto, de la representación
ontológica y de las variables de aprendizaje dentro del alcance vigente.

### 4.9 Razonamiento, aprendizaje y recomendaciones

Una recomendación puede originarse en:

- un barista o sistema que aplique un criterio, heurística, procedimiento,
  patrón de evidencia o política documentados;
- el razonador basado en evidencia, como propuesta para comprobar o descartar
  un diagnóstico;
- un modelo de aprendizaje validado, cuando exista evidencia suficiente para
  utilizarlo.

Toda recomendación conservará un identificador, el agente y actividad
generadores,
la evidencia, la fecha, el alcance, las condiciones de aplicabilidad y el
estado de validación humana. Además:

- si utiliza conocimiento orientador, identificará su tipo, fuente y alcance;
- si procede del razonador, identificará su versión y los límites de la
  conclusión que motivó la propuesta;
- si procede de aprendizaje, identificará modelo y versión, conjunto de datos
  o referencia, métricas, confianza o incertidumbre y condiciones de uso.

La ontología EKO 3.1.0 representa recomendaciones mediante `RecommendedAction` y
separa cuatro dimensiones: evidencia con `derivedFrom`/`hasEvidence`, base de
conocimiento con `hasKnowledgeBasis` y sus subpropiedades, agente mediante
`generatedBy`, y actividad de razonamiento o inferencia mediante PROV-O cuando
se implemente. Así una salida de aprendizaje no necesita una regla ficticia ni
recibe causalidad inexistente.

Las respuestas del barista —aceptación, rechazo, modificación o comentario— se
registrarán como nueva evidencia sin modificar los hechos originales. La
aceptación no demuestra por sí sola la eficacia: el resultado se evaluará en
una preparación posterior explícitamente relacionada con el ajuste.

### 4.10 Sincronización y materialización

La sincronización será diferida y seguirá ADR-002:

1. la preparación recibe un identificador estable antes de comenzar;
2. el dispositivo conserva el registro en su cola hasta recibir confirmación
   externa;
3. los reintentos utilizan el mismo identificador y son idempotentes;
4. el servidor valida esquema, integridad y versión antes de confirmar;
5. solo después de la confirmación se libera el registro local según la
   política de retención;
6. la transformación RDF y el razonamiento se ejecutan sobre datos históricos
   confirmados;
7. el conocimiento materializado conserva la versión de todas las entradas
   relevantes y no sustituye los datos primarios.

La cola local se ajusta a la capacidad verificada en ADR-002: 32 sobres de
hasta 1536 bytes en la partición NVS de 256 KiB. Esta cola contiene únicamente
registros pendientes de confirmación externa; después de la confirmación, la
retención local se aplica según la política definida para el dispositivo.

Los conflictos de edición, la política de rematerialización y la retención de
versiones se detallarán en la arquitectura de datos.

### 4.11 Frontera de autoridad

La base externa, la ontología, el grafo, el razonador, los modelos y la
aplicación externa no tendrán acceso directo a actuadores ni a referencias
activas. Sus resultados serán información o propuestas pendientes.

Aplicar una recomendación requerirá una decisión explícita del barista y la
validación local de identidad, versión, unidad, rango, estado y permisos. Una
sincronización, inferencia, reconexión o actualización de modelo nunca se
interpretará como autorización operativa.

## 5. Consecuencias

### 5.1 Consecuencias favorables

- Cada almacén y servicio tiene una responsabilidad verificable.
- La cafetera conserva autonomía aunque los servicios externos no estén disponibles.
- Las series completas no sobrecargan la flash ni el grafo RDF.
- Los diagnósticos y recomendaciones pueden reconstruirse desde su evidencia.
- Se evita confundir reglas, asociaciones aprendidas y causas demostradas.
- La ontología y el razonador pueden evolucionar sin modificar el lazo de control.
- Una misma infraestructura externa puede asumir varias responsabilidades sin mezclarlas conceptualmente.

### 5.2 Consecuencias desfavorables

- Se requieren contratos e identificadores consistentes entre varios componentes.
- La sincronización, transformación RDF, materialización y versionado aumentan la complejidad.
- Algunas consultas necesitarán combinar base histórica, objetos y grafo semántico.
- El conocimiento materializado deberá invalidarse o regenerarse de forma controlada cuando cambien sus entradas.
- Las recomendaciones aprendidas exigirán datos suficientes y una extensión de procedencia antes de habilitarse.

## 6. Riesgos y medidas de tratamiento

| Riesgo | Tratamiento requerido |
|---|---|
| Duplicación o pérdida de identidad | Identificadores estables, validación de esquema, operaciones idempotentes y deduplicación. |
| Divergencia entre base y grafo | Transformación versionada, referencias al registro fuente y controles de integridad. |
| Inferencia sobre datos ausentes | Conservación explícita de la ausencia y limitación del alcance de las conclusiones. |
| Uso de RDF para telemetría completa | Referenciar series y conservar únicamente hechos, resúmenes y conocimiento relevante. |
| Confusión entre ontología y razonador | Despliegue y documentación separados, con agente y versión en cada resultado materializado. |
| Regla creada para justificar una conclusión | Exigir procedencia real y permitir diagnósticos basados en evidencia sin regla causal. |
| Asociación presentada como causalidad | Estado epistemológico explícito y causa demostrada solo con evidencia experimental suficiente. |
| Recomendación aplicada automáticamente | Estado pendiente, decisión del barista y validación local antes de cualquier activación. |
| Modelo insuficientemente validado | Mantenerlo fuera de uso operativo hasta cumplir el protocolo y los umbrales aprobados. |
| Exposición de datos personales | Identificadores mínimos, control de acceso y política de privacidad para baristas y evaluadores. |

## 7. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Tecnología de base histórica, objetos y grafo semántico | Comparación técnica documentada sin acoplar los contratos al proveedor. |
| Esquema de identificadores y registros | Definición en `arquitectura_datos_conocimiento_v1.md`. |
| Despliegue del grafo RDF | El contrato de transformación a EKO 3.1.0 ya está implementado; falta seleccionar y desplegar el almacén consultable en la plataforma externa local. |
| Despliegue del razonador externo | API, entradas, salidas, versionado, persistencia e idempotencia documentados. |
| Rematerialización de conocimiento | Política para conservar, invalidar o recalcular resultados cuando cambien hechos, ontología o razonador. |
| Procedencia de recomendaciones aprendidas | **Contrato preparado en `espresso-learning` 0.2.0:** módulo opcional que importa EKO, separa el perfil predicho de los perfiles del dominio, usa PROV-O, DCAT 3, DQV y ML Schema y no incorpora infraestructura ML al dominio. La integración con modelos reales sigue pendiente. |
| Estrategia de entrenamiento y validación | Protocolo experimental, tamaño de muestra, partición, métricas y límites de uso. |
| Umbral mínimo para emitir recomendaciones aprendidas | Aprobación metodológica con evidencia y declaración de incertidumbre. |
| Retención, respaldo, acceso y privacidad | Políticas operativas y pruebas sobre la infraestructura seleccionada. |

La representación de lote, bolsa, referencia comercial, fecha de tueste y
terroir ya no es un pendiente de la ontología. Su persistencia y transformación
a RDF ya están implementadas; continúa pendiente únicamente la infraestructura
externa donde se publicará y consultará el grafo validado.

## 8. Verificación de la decisión

Se probará, como mínimo:

1. ejecución de una extracción y recuperación local sin servidor;
2. sincronización idempotente sin duplicar una preparación después de reintentos;
3. reconstrucción desde una preparación hasta sus series, perfil, café, evaluación, conocimiento y recomendación;
4. consulta del grafo sin almacenar las muestras de alta frecuencia como triples individuales;
5. identificación separada de hechos, reglas, diagnósticos probables, explicaciones y decisiones humanas;
6. materialización reproducible con versión de ontología, razonador y datos de entrada;
7. preservación de los hechos después de rematerializar conocimiento;
8. ausencia de valores supuestos cuando faltan metadatos opcionales;
9. rechazo de una recomendación o dato externo inválido sin modificar la configuración efectiva;
10. comprobación de que ningún servicio externo accede directamente a los actuadores;
11. antes de habilitar aprendizaje, trazabilidad completa entre modelo, datos, métricas, recomendación e incertidumbre.

## 9. Trazabilidad

- Requisitos funcionales: RF-014, RF-017–RF-029.
- Requisitos no funcionales: RNF-001, RNF-004, RNF-006–RNF-009.
- Restricciones de diseño: RD-001, RD-003–RD-005.
- Ontología de referencia: EKO 3.1.0 en `knowledge/ontology/ontology/espresso-dt.ttl`.

## 10. Criterios de reconsideración

La distribución se revisará si las mediciones de volumen, latencia o costo
justifican combinar o separar almacenes; si cambia el contrato de la ontología;
si el razonador requiere otra infraestructura; o si el aprendizaje adopta un
modelo de despliegue distinto. Una tecnología podrá asumir varias
responsabilidades lógicas sin invalidar la decisión.

La frontera que impide la actuación directa de servicios externos no podrá
modificarse sin revisar los requisitos de autoridad y seguridad.

## 11. Revisión · Alcance del razonador en el prototipo desplegado

**Fecha:** 2026-09-15
**Motivo:** al definir qué componentes residen en la plataforma externa fue
necesario decidir si el razonador forma parte de la instalación. La § 4.9 lo
admite como una de las tres procedencias posibles de una recomendación, pero no
establece si debe ejecutarse en el equipo desplegado ni con qué disparador.

### 11.1 Situación comprobada

| Comprobación | Resultado |
|---|---|
| Entradas del razonador | Dos archivos versionados del repositorio: el vocabulario y un conjunto de casos de validación. **No lee el grafo ni la base histórica.** |
| Publicación de sus conclusiones | Ninguna: escribe un archivo de salida opcional; la aplicación no las consulta |
| Coste de una ejecución | 0,26 s y 42 MB de memoria, generando 1 353 afirmaciones nuevas sobre 4 953 |
| Conformidad de sus recomendaciones | Declaran procedimiento, política de validación humana, agente generador, evidencia y propiedad ajustada, conforme a las formas de EKO 3.1.0 |
| Fuentes de recomendación activas en la instalación | Ninguna: el adaptador de aprendizaje se abstiene por falta de modelo aprobado y el razonador no está conectado |

El coste de ejecución es despreciable: **la decisión no es de capacidad, es de
alcance y de datos.**

### 11.2 Dos clases de regla

Las reglas del razonador se dividen según la evidencia que necesitan:

| Clase | Evidencia necesaria | Disponibilidad |
|---|---|---|
| Sobre una preparación | El perfil aplicado y sus tolerancias | Útil desde la primera preparación real registrada |
| Sobre varias preparaciones | Historial acumulado | Requiere una serie de preparaciones comparables |

La distinción importa porque la segunda clase, sin historial, produce
sistemáticamente ausencia de evidencia, mientras que la primera aporta desde el
primer registro.

### 11.3 Decisión

1. **El razonador no forma parte del prototipo desplegado.** Permanece como
   herramienta de análisis ejecutada por el investigador sobre datos exportados.
2. **La condición de entrada para integrarlo es la primera preparación real
   registrada**, no un volumen de datos indefinido. Al integrarse, se integrará
   la clase de reglas que opera sobre una preparación; las demás quedan
   disponibles conforme se acumule historial.
3. **Cuando se integre, su disparador será explícito**, a petición del barista o
   del investigador, y sus conclusiones se publicarán en el grafo con su
   procedencia. No se ejecutará automáticamente al terminar una preparación.
4. **No se escribirán reglas de recomendación paralelas en la aplicación.** El
   criterio del dominio permanece en un único lugar; duplicarlo en la capa de
   aplicación crearía dos formulaciones del mismo conocimiento.

### 11.4 Consecuencia sobre el nivel alcanzado

Mientras ninguna de las tres procedencias de § 4.9 esté activa, la plataforma
desplegada **no emite recomendaciones**: responde con abstención declarada y
trazable. El paso de propuesta al barista existe como estructura, pero sin
contenido.

En consecuencia, el sistema instalado se describirá como **sombra digital**
mientras no exista al menos una procedencia de recomendación activa y el tramo
de retorno de la configuración hacia el dispositivo. La clasificación y sus
niveles están en el
marco conceptual del gemelo digital.
Esta es una limitación declarada del prototipo, no una omisión.

### 11.5 Pendiente detectado

Existen **dos representaciones distintas de una recomendación** en el
repositorio: la del contrato `recommendation-proposal.schema.json`, validada en
las pruebas del recorrido histórico y limitada a los estados `abstained`,
`test-only` y `pending-human-review`; y la que produce la aplicación web, que
incluye campos que ese contrato no admite. Hoy no entran en conflicto porque la
aplicación se abstiene siempre. Deben unificarse antes de que la instalación
emita una recomendación con contenido.

## 12. Revisión · Régimen de aprendizaje y lugar del entrenamiento

**Fecha:** 2026-09-15
**Motivo:** ADR-001 § 4.2 asigna a la plataforma externa «entrenar o ejecutar»
modelos de aprendizaje, sin distinguir ambas actividades ni establecer con qué
régimen se actualiza un modelo a partir de las preparaciones registradas.

### 12.1 Situación comprobada

| Comprobación | Resultado |
|---|---|
| Modelo contextual | **No implementado.** Lo que existe es una referencia técnica que ignora deliberadamente todas las características y conserva la clase más frecuente |
| Dependencias del módulo | Únicamente la biblioteca estándar de Python: no hay bibliotecas de cálculo numérico ni de aprendizaje |
| Coste de entrenar e inferir hoy | Despreciable en cualquier equipo |
| Andamiaje de trazabilidad | Completo: ficha del conjunto de datos, informe de calidad, tarjeta del modelo, registro con identificadores estables, partición que evita repartir un mismo café entre entrenamiento y prueba |
| Controles ya implementados | El registro rechaza insertar un modelo con estado aprobado; sin grupos independientes suficientes la métrica retenida queda como no disponible en lugar de sustituirse |
| Protocolo experimental | Pendiente, según § 7: tamaño de muestra, partición, métricas y límites de uso |

**La decisión no es de capacidad.** Con lo construido hoy, entrenar y ejecutar
son operaciones triviales; lo que está en juego es la atribución de lo que el
modelo afirme.

### 12.2 Regímenes considerados

| Régimen | Evaluación |
|---|---|
| Acumulación de evidencia | Ya ocurre de forma continua: cada preparación, su evaluación sensorial y la decisión del barista quedan registradas y enlazadas. Es el aprendizaje por episodios que describe el dominio, y no requiere decisión adicional. |
| Reentrenamiento por lotes, versionado y aprobado | Cada actualización produce un modelo identificable, con su ficha, sus métricas y sus límites, que no se utiliza hasta que una persona lo aprueba. El andamiaje existente ya lo soporta. **Seleccionado.** |
| Actualización continua automática | Cada preparación modificaría el modelo sin versión ni revisión. Descartado por los motivos de § 12.4. |

### 12.3 Decisión

1. **El modelo se actualiza por lotes.** Cada reentrenamiento produce un
   artefacto versionado con su conjunto de datos, sus métricas, sus
   limitaciones y los tipos de café en los que puede utilizarse.
2. **Ningún modelo entra en uso sin aprobación humana explícita.** La
   aprobación es un acto registrado, no una consecuencia de haber entrenado.
3. **Queda excluida la actualización continua automática** del modelo a partir
   de cada preparación.
4. **La inferencia se ejecuta en la plataforma externa**, junto a la aplicación,
   como parte del ciclo de propuesta al barista.
5. **El entrenamiento se realiza en el entorno de desarrollo** mientras el
   protocolo experimental no esté aprobado. Podrá trasladarse a la plataforma
   externa cuando el protocolo exista y el modelo resultante lo permita, sin
   que ello altere los puntos 1 a 3.
6. **El protocolo experimental deberá fijar el criterio de reentrenamiento**:
   qué cantidad de evaluaciones nuevas o qué variedad de cafés lo justifican, y
   contra qué se compara el modelo nuevo para aceptarlo frente al anterior.

### 12.4 Justificación

1. **Trazabilidad.** RNF-009 exige que el modelo registre versión, datos de
   entrenamiento, métricas, limitaciones y tipos de café. Un modelo que cambia
   con cada preparación obligaría a versionar cada actualización —ninguna de
   ellas evaluada— o haría imposible declarar de qué modelo procede una
   recomendación, como exige § 4.9.
2. **Atribución de los episodios de ajuste.** El método del dominio compara una
   preparación con la siguiente tras un ajuste autorizado. Si el modelo se
   modificara entre ambas, una mejora no podría atribuirse al ajuste ni al
   modelo. Es el mismo argumento por el que los servicios externos no modifican
   referencias activas: dos cambios simultáneos impiden aprender de cualquiera
   de ellos.
3. **Tamaño de muestra.** La evidencia disponible será de decenas de
   preparaciones con una variable de salida subjetiva. Un modelo reajustado con
   cada registro seguiría el ruido y aparentaría aprender.
4. **Coherencia con RF-025.** El requisito exige abstenerse cuando no existan
   datos comparables suficientes, lo que presupone evaluar la suficiencia de la
   evidencia en un momento determinado, no actualizar de forma continua.

### 12.5 Consecuencias

- El sistema aprende de las preparaciones por episodios y por lotes, y cada
  afirmación puede atribuirse a un modelo concreto evaluado con datos concretos.
- Entre un reentrenamiento y el siguiente, el modelo desplegado permanece fijo y
  sus recomendaciones son reproducibles.
- La aprobación humana añade un paso que debe registrarse y que impide que una
  mejora aparente entre en uso sin revisión.
- El lugar del entrenamiento deja de ser una decisión estructural y pasa a
  depender del protocolo experimental.

### 12.6 Pendiente detectado

El recorrido completo del aprendizaje, ejecutado el 2026-09-15 contra la
aplicación con datos de demostración, **falla al exportar los hechos a RDF**: el
perfil que entrega la API no incluye el campo de nombre que espera el
exportador. No se ha determinado la causa. Debe verificarse antes de construir
sobre esa ruta, porque es el camino por el que los datos llegarían al conjunto
de entrenamiento.

## 13. Revisión · La activación de un objetivo es la decisión explícita del sostenimiento

**Fecha:** 2026-09-30
**Motivo:** [ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md)
introduce correcciones de referencia sin aprobación caso por caso, que sostienen
el objetivo activado por el barista dentro de una política fijada por los
autores. La § 4.11 exige una decisión explícita del barista para aplicar una
recomendación, y conviene dejar escrito cómo se lee esa exigencia frente al
sostenimiento.

### 13.1 Lectura de la § 4.11

1. **La activación del objetivo es la decisión explícita.** El barista decide a
   qué debe llegar el agua al activar una configuración por cualquiera de las
   vías de ADR-017. Sostener ese objetivo no decide nada nuevo, y cada corrección
   cita la activación de la que cuelga.
2. **Los límites no son una decisión del barista.** Qué se corrige, cuánto y
   cuándo se deja de corregir lo fija la política de los autores, con versión.
   La monografía debe atribuirlo así.
3. **Fuera de la política nada cambia.** Toda corrección que la exceda, y toda
   recomendación que no sea una corrección de sostenimiento, sigue pasando por
   la decisión caso por caso.
4. **La autorización nunca la da el modelo.** La § 4.11 establece que una
   actualización de modelo no se interpreta como autorización operativa, y eso
   se mantiene: el reajuste del modelo cambia el cálculo de la corrección, no su
   permiso.
5. **Ningún servicio escribe referencias activas.** La corrección produce una
   configuración que baja por el canal de siempre, y el dispositivo la valida y
   puede rechazarla, como exige la misma sección.

### 13.2 Relación con la § 12

La § 12 excluye la actualización continua automática del **modelo de
aprendizaje** que produce recomendaciones. No alcanza al modelo físico que
calcula las correcciones de ADR-018, cuyo reajuste tendrá su propia decisión.

El argumento de atribución de la § 12.4 sí alcanza al sostenimiento, y ADR-018
§ 4.6 lo atiende: mientras un episodio de ajuste esté abierto, el sistema no
corrige. Las demás razones de la § 12.4 siguen siendo pertinentes para la
decisión futura sobre el reajuste del modelo físico: cada versión de parámetros
debe ser identificable, y una corrección debe declarar con cuál se calculó.
