# ADR-004 — Distribución de datos, conocimiento semántico y razonamiento

**Estado:** Aceptada (2026-08-23)
**Ámbito:** datos operativos, conocimiento semántico, razonamiento, modelos y recomendaciones

## 1. Contexto

El sistema produce estados, series, configuraciones, perfiles, datos del café,
evaluaciones sensoriales, diagnósticos y recomendaciones, con volúmenes,
procedencias y ciclos de vida distintos. La ontología EKO
(`ontology/owl/espresso-dt.ttl`) distingue observaciones, diagnósticos
probables, explicaciones, recomendaciones y decisiones humanas. Un razonador de
aplicación compara evidencias y materializa lo que la inferencia OWL sola no
obtiene. La cafetera funciona sin servidor; todo lo demás corre en la plataforma
externa local (ADR-001, ADR-003), fuera del lazo de control.

## 2. Alternativas consideradas

| Alternativa | Razón |
|---|---|
| Todo en RDF | Descartada: es ineficiente para series y confunde datos primarios con conocimiento derivado. |
| Solo una base de datos convencional | Descartada: no ofrece navegación semántica ni procedencia del conocimiento. |
| Razonamiento en la ESP32-S3 | Descartada: consume recursos del controlador y mezcla conocimiento con control. |
| Separar datos, grafo, razonador y modelos | **Seleccionada.** |

## 4. Decisión

### 4.1 Almacenamiento

La ESP32-S3 guarda en RAM o PSRAM la extracción en curso, y en NVS la identidad,
la configuración, los límites, las calibraciones y la cola de preparaciones
pendientes (ADR-002). La plataforma guarda en una base histórica y un almacén de
objetos las preparaciones, perfiles, evaluaciones, eventos y series completas.
La ESP32-S3 no guarda historial, grafo, modelos ni series de alta frecuencia.

### 4.2 Servicios de conocimiento

La ontología define el vocabulario del dominio. El grafo semántico conserva
hechos en RDF, referencias a datos externos y conocimiento materializado con
procedencia. El razonador valida, compara evidencias y materializa diagnósticos,
explicaciones y recomendaciones de comprobación.

### 4.3 Interfaz

La aplicación web externa es la única interfaz: presenta historial, grafo y
evidencia, y registra las decisiones del barista.

### 4.4 Fuentes autorizadas

El firmware es la fuente del estado actual, la configuración efectiva y las
alarmas. La base histórica lo es del registro y las series. Los hechos
semánticos son una transformación trazable del registro confirmado, y el
conocimiento materializado, una ejecución identificada del razonador. El perfil
sensorial declarado procede del productor o tostador; el objetivo sensorial, la
evaluación y la decisión sobre una recomendación son declaraciones explícitas
del barista o del evaluador, nunca inferidas de puntuaciones.

Un dato derivado no modifica los hechos que lo produjeron. El tiempo máximo
pertenece al perfil y la causa de finalización a la preparación; ambos los
produce el firmware. La ontología representa la dosis, la masa y el TDS
manuales, pero no decide cuándo apagar la bomba.

### 4.5 Separación entre ontología, grafo y razonador

El razonador recibe registros confirmados, los transforma en hechos de EKO,
compara observaciones, especificaciones y evaluación sensorial, y materializa
conocimiento con evidencia, agente, método, instante y nivel de conclusión. El
grafo referencia las series mediante `dataReference`; no guarda cada muestra
como un triple.

### 4.6 Tipos y procedencia del conocimiento

El sistema distingue hechos registrados, reglas definicionales, conocimiento
experto, conclusiones basadas en evidencia, causas demostradas y decisiones
humanas. Cada uno declara su procedencia: fuente,
versión, evidencia y límites. Una coincidencia, una comparación o una asociación
no se presenta como causa demostrada.

### 4.7 Identidades entre almacenes

Preparaciones, perfiles, cafés, calibraciones, series, evaluaciones,
recomendaciones y versiones de firmware, ontología y razonador llevan
identificadores estables, que el grafo usa para navegar sin copiar las series.

### 4.8 Representación del café

Cada preparación se vincula a una referencia interna de café. Variedad, proceso,
tueste, lote, fecha de tueste y terroir son opcionales; si faltan, no se suponen.
Molienda, dosis y molino son contexto de la preparación. El agua y la canastilla
quedan fuera del alcance.

### 4.9 Recomendaciones

Una recomendación puede originarse en un procedimiento documentado o en el
razonador. Conserva
identificador, agente, evidencia, fecha, aplicabilidad, versión de quien la
produjo y estado de validación humana. EKO la representa con
`RecommendedAction` y separa evidencia (`derivedFrom`, `hasEvidence`), base de
conocimiento (`hasKnowledgeBasis`) y agente (`generatedBy`), para no inventar
reglas ni causalidad. La respuesta del barista es evidencia nueva; aceptar no
demuestra eficacia.

### 4.10 Sincronización

La sincronización es diferida e idempotente (ADR-002). La transformación RDF y
el razonamiento operan solo sobre datos confirmados por la plataforma.

### 4.11 Frontera de autoridad

Ni la base externa, ni la ontología, ni el grafo, ni el razonador, ni los
modelos, ni la aplicación acceden a actuadores o referencias activas. Sus
resultados son información o propuestas pendientes.

Aplicar una recomendación exige una decisión explícita del barista y la
validación local de identidad, versión, unidad, rango, estado y permisos. Una
sincronización, inferencia, reconexión o actualización de modelo nunca es una
autorización operativa.

## 5. Consecuencias

La cafetera conserva su autonomía. Las series no cargan la flash ni el grafo.
Cada diagnóstico se reconstruye desde su evidencia, y la ontología evoluciona
sin tocar el control. A cambio, hacen falta contratos e identificadores
consistentes entre componentes, y el conocimiento materializado debe
regenerarse de forma controlada cuando cambian sus entradas.

## 6. Verificación

1. Una extracción se ejecuta sin servidor.
2. Los reintentos no duplican preparaciones.
3. Desde una preparación se llega a sus series, evaluación y recomendación.
4. El grafo no guarda las muestras como triples.
5. Rematerializar no altera los hechos.
6. Ningún servicio externo accede a los actuadores.

## 11. Alcance del razonador en el prototipo desplegado

Ejecutar el razonador cuesta 0,26 s y 42 MB, así que la decisión es de alcance y
de datos. Sus reglas que juzgan una preparación necesitan solo el perfil y sus
tolerancias, y aportan desde el primer registro. Las que comparan varias
preparaciones necesitan historial; sin él solo producen ausencia de evidencia.

### 11.3 Decisión

1. Fuera de la plataforma, el razonador sirve como herramienta de análisis sobre
   datos exportados.
2. Entra en la plataforma con la primera preparación real registrada, con las
   reglas sobre una preparación; las demás rinden a medida que crece el
   historial.
3. Su disparador es explícito: lo pide el barista o el investigador, y sus
   conclusiones se publican en el grafo con su procedencia. No corre al terminar
   cada preparación.
4. No se escriben reglas de recomendación paralelas en la aplicación. El
   criterio del dominio vive solo en `ontology/reasoning/materialize.py`;
   duplicarlo crearía dos formulaciones del mismo conocimiento.

### 11.4 Consecuencia sobre el nivel alcanzado

Si ninguna procedencia de la § 4.9 produce contenido, la plataforma no
recomienda: se abstiene de forma declarada y trazable. Sin una procedencia
activa y sin el retorno de la configuración al dispositivo, el sistema se
describe como sombra digital.

El contrato `platform/contracts/device/recommendation-proposal.schema.json`
admite los estados `abstained`, `test-only` y `pending-human-review`. La forma
de recomendación de la aplicación tiene campos que ese contrato no admite; ambas
deben unificarse antes de emitir una recomendación con contenido.

## 12. Los modelos no cambian entre una preparación y la siguiente

El sistema no usa modelos de aprendizaje automático. Los modelos que sí usa,
como el modelo físico, se actualizan por versiones: cada versión declara sus
datos y sus límites, y ninguna entra en uso sin aprobación de los autores.
Ningún modelo se reajusta con cada preparación.

### 12.4 Justificación

1. **Trazabilidad.** Hay que poder declarar de qué modelo procede cada
   resultado; un modelo que cambia en cada preparación lo impide.
2. **Atribución de los episodios de ajuste.** El método compara una preparación
   con la siguiente tras un ajuste autorizado. Si el modelo cambiara entre
   ambas, una mejora no podría atribuirse ni al ajuste ni al modelo.
3. **Tamaño de muestra.** Con decenas de preparaciones, reajustar en cada
   registro seguiría el ruido.

## 13. La activación de un objetivo es la decisión explícita del sostenimiento

[ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md) corrige la
referencia sin aprobación caso por caso para sostener el objetivo que activó el
barista. Así se lee la § 4.11 frente a ese sostenimiento:

1. Activar el objetivo es la decisión explícita, por cualquiera de las vías de
   ADR-017. Cada corrección cita la activación de la que depende.
2. Qué se corrige, cuánto y hasta cuándo lo fija la política versionada de los
   autores, no el barista.
3. Lo que exceda la política, o no sea sostenimiento, sigue requiriendo decisión
   caso por caso.
4. El reajuste del modelo cambia el cálculo de la corrección, no su permiso.
5. La corrección baja como configuración por el canal habitual, y el
   dispositivo puede rechazarla.

La § 12 no alcanza al modelo físico que calcula las correcciones, que tiene su
propia decisión. Su argumento de atribución (§ 12.4) sí: ADR-018 § 4.6 suspende
las correcciones mientras un episodio de ajuste está abierto, y cada corrección
declara con qué parámetros se calculó.
