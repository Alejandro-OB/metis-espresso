# ADR-018 — Política de sostenimiento del objetivo aprobado

**Estado:** Aceptada, revisada
**Fecha de redacción:** 2026-09-30
**Fecha de aprobación:** 2026-09-30
**Fecha de revisión:** 2026-09-30 (la política deja de aprobarla el barista y la fijan los autores; § 4.6: episodios de ajuste abiertos); 2026-10-22 (§ 12: subida de caldera del primer uso, antes del café); 2026-10-22 (§ 13: se retira la corrección después del café)
**Ámbito:** correcciones de referencia que la plataforma aplica sin aprobación caso por caso, política que las acota y paso de la sombra digital al gemelo digital
**Reemplaza:** la primera redacción del mismo ADR, en la que cada política era un acto del barista atado a una configuración

## 1. Contexto

ADR-017 dejó tres vías de activación, las tres con una decisión humana
registrada detrás de cada configuración. El ciclo físico → virtual → físico
quedó recorrido y verificado el 2026-09-15, y el único tramo que no ocurre solo
es la autorización del barista. Según la clasificación que adopta el trabajo,
ese tramo es exactamente lo que separa una sombra digital de un gemelo digital
(marco conceptual § 6).

El argumento que sostiene la frontera de autoridad es que la máquina no debe
decidir sola **porque no puede probar el café**. Ese argumento excluye que la
máquina decida a qué debe saber la bebida. No excluye que cumpla lo que el
barista ya decidió: volver a los 91 °C fijados cuando la cocina está más fría no
cambia la receta, la sostiene. Hoy, incluso para eso, el sistema tiene que pedir
permiso en cada preparación.

La distinción fue aprobada como propuesta por los autores el 2026-09-16
(marco conceptual § 6.1). Esta decisión la convierte en regla de arquitectura.

### 1.1 Por qué la política no la aprueba el barista

La primera redacción de esta decisión hacía de cada política un acto del
barista: él la aprobaba para un objetivo y fijaba sus límites. Se revisó el
mismo día por dos razones.

La primera es que el barista no está disponible para fijar los límites, así que
los fijan los autores (§ 4.7). Con los límites ajenos, lo único que quedaba en
manos del barista era activarla, un trámite que la monografía habría presentado
como una decisión de fondo.

La segunda es que ese trámite no añade autoridad. El barista ya decidió al
activar el objetivo, y sostenerlo no decide nada nuevo. Es la misma relación que
hay entre fijar la velocidad de un control de crucero y que el vehículo la
mantenga en una subida.

Lo que no desaparece es la política. Sigue habiendo reglas sobre qué se corrige,
cuánto y cuándo se deja de corregir; lo que cambia es su autor. **La política
existe, es única para todo el sistema y la fijan los autores.**

### 1.2 Situación comprobada

| Elemento | Situación |
|---|---|
| Transporte, validación local y entrada en vigor | Implementados y verificados (ADR-012, ADR-017 § 4.4) |
| Derivación de configuración desde un ajuste aceptado | Implementada el 2026-09-15 (ADR-017 § 8) |
| Comparación entre serie medida y simulada | Implementada (`packages/espresso_simulation/comparison.py`) |
| Registro de episodios de ajuste y de su resultado | Implementado (`AdjustmentRegistration` y `EpisodeOutcomeCreate` en `schemas.py`) |
| Reajuste del modelo con cada preparación | **No implementado**: el modelo corre con parámetros fijos |
| Estimación de la temperatura del agua desde la del cuerpo | **No implementada** |
| Política, exclusión, suspensión, cuarta vía y decisión tras cada preparación | **Implementadas el 2026-10-09**, inactivas: sin las dos filas anteriores, la decisión es siempre «no corrige» y queda guardada con su motivo (`packages/espresso_sustainment`, `application/sustainment.py`) |
| Subida de caldera del primer uso, decidida antes del café | **Implementada el 2026-10-22** (§ 12): actúa en la máquina real (`espresso_sustainment/first_use.py`) |

## 2. Problema de decisión

Se debe definir qué puede corregir la plataforma sin aprobación caso por caso,
quién fija los límites de esa corrección, cuándo debe detenerse y avisar, y cómo
queda trazada cada corrección.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Mantener la aprobación caso por caso | Conserva la situación actual. El sistema queda como sombra digital con ciclo cerrado por decisión humana, y la consistencia se pierde justo donde más varía: entre preparaciones de un mismo objetivo. |
| Corrección automática sin política | Cumpliría el automatismo de la clasificación, pero nada acotaría la corrección ni permitiría reconstruir por qué rigió cada referencia. |
| Corrección automática de cualquier propiedad, incluido el objetivo | Desplaza al barista en lo único que el sistema no puede juzgar. Contradice el argumento que sostiene toda la frontera de autoridad. |
| Política aprobada por el barista para cada objetivo | Primera redacción de esta decisión. Con los límites fijados por los autores, la aprobación del barista se reduce a un trámite sin contenido (§ 1.1). |
| Política única fijada por los autores, que sostiene el objetivo que el barista activó | El barista decide el objetivo y nada más; los límites son públicos, versionados y reproducibles. **Seleccionada.** |

## 4. Decisión

### 4.1 Objetivo y referencia son cosas distintas

El **objetivo** es lo que el barista fija: la temperatura del agua sobre el
café, la presión de extracción y el tiempo máximo con que quiere preparar ese
café. La **referencia** es el valor que el dispositivo regula para alcanzarlo,
por ejemplo la temperatura del cuerpo de la caldera.

Ambos coinciden solo si la máquina no tiene pérdidas. Cuando la cocina está más
fría, o la caldera tarda más en recuperarse entre preparaciones, la misma
referencia produce un agua más fría. Sostener el objetivo consiste en mover la
referencia para que el agua siga llegando a lo que el barista fijó.

**El sostenimiento cambia la referencia, nunca el objetivo.**

Tampoco es el controlador PI con otro nombre. El PI mantiene el cuerpo de la
caldera en su referencia durante la extracción, con lo que mide el sensor. El
sostenimiento mueve esa referencia entre preparaciones para que el agua, que
ningún sensor mide, llegue al objetivo, y para eso necesita el modelo. Por RD-004
el modelo no entra en el lazo, así que las dos capas quedan separadas: el PI en
el dispositivo, con su periodo de control, y el sostenimiento en la plataforma,
una vez por preparación.

### 4.2 La política

La política es única para todo el sistema, la fijan los autores y tiene versión.
Declara:

| Campo | Contenido |
|---|---|
| Propiedades corregibles | Temperatura. La presión queda fuera hasta la etapa 2 de su modelado (§ 4.7). El tiempo máximo nunca: es un criterio de terminación (ADR-005 § 4.3), no una referencia |
| Corrección máxima | Distancia máxima entre la referencia corregida y la del objetivo, por propiedad |
| Condiciones de aviso | Cuándo el sistema deja de corregir y pasa a proponer (§ 4.6) |
| Versión, autores y fecha | Qué versión rige y desde cuándo |

Cambiar un umbral es publicar una versión nueva, con su justificación. Ninguna
versión se modifica después de haber regido, porque las correcciones hechas con
ella la citan.

### 4.3 Qué decide el barista

El barista activa el objetivo por cualquiera de las tres vías de ADR-017, y ese
acto es la decisión de la que cuelga toda corrección posterior. No aprueba nada
adicional.

Conserva, además, la facultad de **excluir una configuración** del
sostenimiento. La exclusión es un acto registrado, con autor y fecha, y rige
hasta que él la retire. Existe para el barista que quiere ver cómo responde la
máquina sin ayuda, no como condición para que el sostenimiento funcione.

El protocolo experimental puede, por su parte, **suspender** el sostenimiento
mientras una serie comparable esté abierta (ADR-017 § 11). La suspensión la
deciden los investigadores y también queda registrada.

### 4.4 Qué no hace nunca

- cambiar el objetivo ni seleccionar otra configuración;
- corregir molienda, dosis o proporción, que ejecuta el barista (ADR-017 § 4.6);
- iniciar, detener o prolongar una preparación (ADR-005 § 4.2);
- aplicar una corrección durante una preparación (ADR-017 § 4.4);
- superar la corrección máxima, aunque el modelo prediga que haría falta;
- eludir la validación local ni el guardián de actuación.

### 4.5 Una cuarta vía de activación

La corrección entra por una cuarta vía, que se añade a las tres de ADR-017 § 4.1:

| Vía | Acto | Cuándo se usa |
|---|---|---|
| **Sostenimiento del objetivo** | El sistema deriva una referencia corregida dentro de la política vigente | Entre preparaciones de un mismo objetivo, cuando la anterior se desvió de él |

Produce una configuración aceptada como las demás y baja por el mismo canal. El
dispositivo la valida igual y puede rechazarla con causa. Su procedencia
declara, además de lo que exige ADR-017 § 4.2, la activación del barista que
fijó el objetivo, la versión de la política, la preparación cuya desviación la
motivó y la versión del modelo que calculó la corrección.

### 4.6 Cuándo no corrige

El sistema no corrige, y convierte la situación en una recomendación ordinaria
cuando hay algo que proponer, si ocurre cualquiera de estas condiciones:

1. la corrección necesaria supera la corrección máxima de la política;
2. la desviación persiste tras un número de correcciones seguidas;
3. el modelo que calcula la corrección está fuera de su tolerancia frente a lo
   medido (`outside-model-tolerance` o `insufficient-coverage` en el documento
   de comparación);
4. la medida de la preparación anterior no es apta para regular, o la
   temperatura del agua no está disponible porque no existe todavía la
   transformación de ADR-019;
5. **hay un episodio de ajuste abierto**: el barista registró un ajuste y la
   preparación en que se evalúa todavía no tiene resultado;
6. la configuración está excluida por el barista o el sostenimiento está
   suspendido por el protocolo (§ 4.3).

La condición 5 responde al argumento de atribución de ADR-004 § 12.4: si la
máquina corrigiera entre el ajuste del barista y su evaluación, una diferencia
en la bebida no podría atribuirse a ninguno de los dos. Se renuncia a corregir
en esa preparación para que el episodio quede limpio.

El sostenimiento que funciona no empeora la atribución, la mejora. Lo que llega
al sabor es la temperatura del agua, no la referencia; si el agua se mantiene
entre preparaciones aunque cambie la cocina, un ajuste de molienda se evalúa sin
que el entorno se cuele en la comparación. La condición 5 cubre el caso
contrario, que el modelo se equivoque justo durante el episodio.

### 4.7 Umbrales de la política vigente

| Umbral | Valor y cómo se obtiene | Carácter |
|---|---|---|
| Corrección máxima de temperatura | **2,8 °C.** Se simula el modelo de la caldera en los extremos razonables de temperatura ambiente y de agua de entrada. La diferencia que producen en la temperatura del agua es lo máximo que el entorno puede explicar; una corrección mayor apunta a una falla y no a una perturbación (§ 4.7.1) | **Provisional**: reproducible por simulación, pero calculado sobre el nodo del modelo que no está identificado (§ 4.7.1); se recalcula con la transformación de ADR-019 |
| Corrección máxima de presión | Mismo procedimiento sobre el modelo hidráulico, cuando la etapa 2 de su modelado lo permita. Hasta entonces la presión no se corrige | Reservado a la etapa 2 |
| Correcciones seguidas antes de avisar | Dos correcciones seguidas tras las cuales la preparación sigue fuera de la tolerancia del comparador (`DEFAULT_MODEL_TOLERANCES` en `comparison.py`) | **Provisional**: no hay de dónde derivarlo; se revisa con los registros de la puesta en servicio |

Si el barista llega a responder la
§ 18 de
las preguntas pendientes, su respuesta se incorpora como versión nueva de la
política, con la procedencia declarada. Hasta entonces los umbrales son
criterio de los autores, y la monografía los atribuye así, nunca al barista.

#### 4.7.1 Derivación de la corrección máxima de temperatura

Los extremos son las normales climatológicas 1991-2020 del IDEAM en el
aeropuerto de Popayán: la mínima media más baja, 12,5 °C, y la máxima media más
alta, 26,4 °C, ambas en agosto. Son valores al aire libre, más amplios que los
de una cocina, y por eso el resultado es conservador. El agua del tanque se toma
a la misma temperatura que el ambiente.

Se simuló `Pruebas.ComparacionControlPI`, con el PI regulando el sensor a
93 °C, y se midió el nodo caliente de la caldera durante la extracción
(`umbral_correccion_ambiente.py`):

| Momento de la extracción | Diferencia media entre extremos | En el punto más bajo |
|---|---|---|
| 450 s tras encender en frío | 2,83 °C | 3,51 °C |
| 1500 s tras encender en frío | 0,77 °C | 1,56 °C |

Se adopta la diferencia media del primer caso. Preparar poco después de
encender es uso normal, y el umbral debe cubrir todo lo que el entorno puede
explicar en uso normal.

**Por qué es provisional.** La magnitud medida es el nodo caliente del modelo de
dos nodos, que nunca se midió. Con una sola señal de temperatura, la
conductancia interna y la capacidad del nodo del sensor no quedan determinadas
(procedencia de las ecuaciones,
P-04 bis), de modo que la diferencia entre el cuerpo y el agua que el modelo
produce puede estar muy lejos de la real. La cifra se conserva como umbral
provisional porque es reproducible y conservadora, y se recalcula cuando exista
la transformación entre cuerpo y agua de
[ADR-019](ADR-019-temperatura-del-agua-por-transformacion-medida.md).

Con la máquina estabilizada, el efecto del entorno (0,77 °C) queda por debajo
del error del propio modelo térmico (RMSE de calibración de 2,047 °C). En ese
régimen el modelo no distingue todavía una cocina más fría de su propio error,
y una corrección calculada con él seguiría ruido. Es una razón más para el
reajuste del modelo con cada preparación (§ 4.8), y la condición 3 de § 4.6
impide corregir mientras el modelo esté fuera de su tolerancia.

### 4.8 De dónde sale la corrección

La corrección la calcula el gemelo: el modelo físico, reajustado con las series
de las preparaciones anteriores, predice qué referencia devolvería el agua al
objetivo. Un modelo que no se reajusta calcula siempre la misma corrección para
la misma desviación, y no distingue entre una máquina que cambió y una
perturbación pasajera.

El reajuste del modelo queda fuera del alcance de esta decisión. Sus
condiciones mínimas son que cada versión de parámetros sea identificable y que
la corrección declare con cuál se calculó.

## 5. Justificación

1. **El argumento del experto se conserva entero.** El sistema no decide a qué
   sabe la bebida. La distinción entre fijar y sostener es una conclusión de
   los autores, respaldada por el barista en la entrevista al aprobar un
   sistema que aprenda de la respuesta humana (transcripción, 00:39:57 a
   00:40:14). Debe citarse así, como validación por el informante, y no como
   respuesta suya a esta decisión.
2. **La decisión del barista sigue en el origen de cada corrección.** Toda
   referencia vigente cuelga de una activación suya del objetivo. ADR-004
   § 4.11 exige una decisión explícita, y la activación lo es; los límites
   dentro de los cuales se sostiene son de los autores y constan con su versión.
3. **Nada nuevo entra en el lazo de control.** La corrección ocurre entre
   preparaciones y llega como una referencia más. El lazo local sigue siendo
   el mismo PI sobre una referencia fija durante la extracción, como exige
   RD-004.
4. **Los episodios de ajuste quedan atribuibles.** La condición 5 de § 4.6
   impide que una corrección se mezcle con el ajuste que el barista está
   evaluando.
5. **Es el único tramo que falta para la clasificación.** Con el sostenimiento
   activo, el flujo virtual → físico es automático en su ámbito, que es lo que
   distingue al gemelo digital de la sombra digital.

### 5.1 Por qué corregir es lo predeterminado

Las reglas del proyecto piden que, cuando nadie ha decidido, el sistema haga lo
más conservador. Aquí sí hubo decisión: el barista activó un objetivo, y
sostenerlo es cumplir esa decisión. Lo conservador frente a un objetivo
decidido no es dejar que el agua se aleje de él, sino mantenerlo dentro de
límites que impiden corregir más de lo que el entorno explica.

Donde no hay decisión, el sistema sigue siendo conservador: sin objetivo
activado no hay nada que sostener, y cualquier duda sobre el modelo o la medida
detiene la corrección (§ 4.6).

## 6. Consecuencias

### 6.1 Consecuencias favorables

- El sistema pasa a ser un gemelo digital en el ámbito del sostenimiento, sin
  modificar la frontera de autoridad.
- La consistencia mejora entre preparaciones de un mismo objetivo, donde más se
  pierde.
- La comparación con sostenimiento activo y suspendido pasa a ser un resultado
  evaluable del trabajo.
- Una sola política, versionada, es más fácil de construir, probar y defender
  que una por objetivo.

### 6.2 Consecuencias desfavorables

- La plataforma genera configuraciones sin una aceptación por cada una. La
  procedencia debe bastar para reconstruir por qué rigió cada una.
- Los límites son de los autores y no del oficio del barista. Deben
  justificarse por derivación o declararse provisionales, como en § 4.7.
- Se pierde la corrección en la preparación que cierra cada episodio de ajuste.
- La calidad de la corrección depende del modelo y del estimador de la
  temperatura del agua, ninguno implementado todavía.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| El modelo sigue ruido y corrige en exceso | Corrección máxima y aviso tras correcciones seguidas (§ 4.6) |
| Una corrección enmascara una avería | La persistencia de la desviación convierte la situación en aviso, en lugar de seguir compensando |
| Confundir una corrección con una decisión del barista | La procedencia declara la vía, la activación de origen, la versión de la política y la preparación que la motivó |
| Una corrección se mezcla con un ajuste del barista | No se corrige con un episodio de ajuste abierto (§ 4.6, condición 5) |
| Correcciones durante una serie comparable | Suspensión por el protocolo mientras la serie esté abierta |
| Un umbral cambia y altera correcciones pasadas | Las versiones no se modifican después de haber regido |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Umbral de corrección máxima de temperatura | **Provisional desde el 2026-09-30**: 2,8 °C (§ 4.7.1). Se recalcula con la transformación de ADR-019 |
| Umbral de correcciones seguidas | Revisión del valor provisional con los registros de la puesta en servicio |
| ~~Nota en ADR-004 § 4.11 y ADR-005 § 4.5~~ | **Hecha el 2026-09-30**: ADR-004 § 13 y ADR-005 § 12 |
| ~~Requisito que exige ADR-005 § 11~~ | **Añadido el 2026-09-30**: RF-030. La autorización académica quedó concedida por los autores el mismo día (ADR-005 § 12.2) |
| Reajuste del modelo por preparación | Decisión propia, con versionado de parámetros |
| Subida del primer uso con reposos de más de 30 min | Por decisión del usuario del 2026-10-22 no se mide: la regla se queda en el valor de 30 min y la decisión lo declara (§ 12.3) |
| Estimación de la temperatura del agua | Decidida en ADR-019. Mientras no haya transformación, la temperatura del agua es «no disponible» y el sostenimiento no corrige temperatura (§ 4.6, condición 4) |
| ~~Registro de la política, de la exclusión, de la suspensión y de la cuarta vía en la plataforma~~ | **Hecho el 2026-10-09**, con las siete verificaciones de § 9 como pruebas de la API |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. sin objetivo activado, ninguna configuración entra por la cuarta vía;
2. con un objetivo activado, una desviación dentro de los límites produce una
   configuración corregida que rige en la preparación siguiente;
3. una corrección que supera el máximo no se aplica y aparece como
   recomendación pendiente;
4. con un episodio de ajuste abierto no se produce ninguna corrección;
5. una configuración excluida, o un sostenimiento suspendido, no recibe
   correcciones;
6. la configuración corregida declara la activación de origen, la versión de la
   política, la preparación que la motivó y la versión del modelo;
7. ninguna corrección entra durante una preparación ni modifica el tiempo
   máximo.

## 10. Trazabilidad

- [ADR-004](ADR-004-distribucion-datos-ontologia-aprendizaje.md) § 4.11, § 12 y
  § 13: frontera de autoridad, atribución de los episodios de ajuste y lectura
  de la decisión explícita.
- [ADR-005](ADR-005-limites-regulacion-permanente.md) § 4.1, § 4.2, § 4.5, § 11 y
  § 12: regulación sin decisión autónoma, recorrido de una recomendación y
  condiciones del cambio autónomo de referencias.
- [ADR-017](ADR-017-activacion-de-la-configuracion-efectiva.md): vías,
  procedencia, precedencia y entrada en vigor.
- Marco conceptual del gemelo digital § 6 y § 6.1.
- Requisitos: RF-029, RF-030, RD-002 y RD-004.

## 11. Criterios de reconsideración

La decisión deberá revisarse si el barista, al usar el sistema, prefiere
aprobar cada corrección; si su respuesta a la § 18 contradice los umbrales
derivados; si alguna corrección necesaria resulta ser en realidad un cambio de
objetivo; o si la evaluación muestra que el sostenimiento no mejora la
consistencia frente a su suspensión.

## 12. Enmienda del 2026-10-22 — subida de caldera del primer uso

### 12.1 Por qué la corrección después del café no corrige

La § 4.6 corrige la referencia tras una preparación, con el agua estimada desde
el cuerpo. Desde el acta v6 esa estimación es la misma recta que usa la placa
para fijar la caldera ([ADR-019](ADR-019-temperatura-del-agua-por-transformacion-medida.md),
enmienda del 2026-10-18). Con la caldera en su referencia al pulsar, el agua
estimada da el objetivo por construcción, y la decisión sería siempre «dentro
de tolerancia».

El café frío de la máquina real es otro: el **primero tras un reposo**, de 1,8
a 3,9 °C por debajo de los siguientes, con la caldera marcando bien
(evidencia `temperatura-cierre-2026-10-17.md`, «Ensayo de reposo»). La recta no
lo ve, y una corrección después de ese café llegaría al segundo, que ya sale
bien. Además, con el objetivo en 92 °C solo quedan 0,48 °C por encima antes del
tope de agua que acepta la placa (92,48 °C).

### 12.2 Decisión

**Se decide (usuario, 2026-10-22)** que el sostenimiento actúe **antes** del café:
la plataforma calcula, con el reposo que dice la telemetría, cuánto subir la
caldera para el próximo uso, y la placa lo aplica por el campo `boilerOffsetC`
de [ADR-009](ADR-009-control-local-temperatura-pi-ciclo-integral.md), enmienda
del 2026-10-19: solo en la preparación siguiente y de vuelta a cero al
terminarla.

- **No usa la cuarta vía** (§ 4.5). La subida viaja con la configuración que ya
  rige, en la respuesta de la sincronización. Una versión de perfil por cada
  tramo de reposo llenaría el catálogo de configuraciones que nadie eligió. La
  trazabilidad la dan los documentos `first-use-decision`: cada cambio de lo
  pedido, con el reposo, el último uso visto, la configuración que regía, la
  regla con su versión y el motivo.
- **No la acota la política de § 4.7.** El 2,8 °C limita el agua pedida; la
  subida es de caldera y la acota el máximo firmado en el acta de temperatura
  (4,0 °C, acta v7). Una regla que pidiera más no se puede crear.
- **Respeta lo que deciden las personas**: con el sostenimiento suspendido o la
  configuración excluida no pide nada, y si el perfil declara la subida a mano,
  manda la del perfil.
- **Sigue con un episodio de ajuste abierto** (decisión del usuario,
  2026-10-22). La § 4.6, condición 5, renuncia a corregir mientras el barista
  no evalúa su ajuste, para que un error del modelo no se cuele en esa
  evaluación. La subida no sale del modelo sino de lo medido, y mantener el agua
  en el objetivo es lo que deja limpia la comparación: apagarla haría salir frío
  el primer café tras el ajuste, y ese frío se le atribuiría al ajuste.
- **No cambia lo pedido durante un uso** (§ 4.4): con una preparación o un
  purgado en curso mantiene lo último pedido.
- **Se pide desde el encendido** (2026-10-22). Durante unas horas se pidió solo
  con la máquina en «lista» (0afcb11), porque el arranque rápido del acta v8
  actuaba hasta 103 °C y con la subida la referencia pasa de ahí. El acta v9
  sube ese tope a 106 °C, probado desde frío con 105,6 °C (pico tras el corte
  101,8 °C, «lista» a los 8 min 5 s), y la subida vuelve a pedirse desde el
  encendido: una sola «lista», sin la recalentada de unos 2 min.
- **El reposo lo da la placa**: desde el último uso visto hasta la última
  muestra recibida, con su reloj. El fin de un uso sale del resumen de la
  preparación (`finishedAt`), porque el firmware de operación no envía
  telemetría durante el café, o de las muestras en `manual-purge` de un
  purgado, que no deja resumen. Solo se mira la ventana de 30 min que la regla
  distingue.
- **No supone nada que no consta**: con un hueco de más de 60 s en la
  telemetría de esa ventana no pide nada, porque pudo haber un uso que la
  plataforma no vio.

### 12.3 La regla `primer-uso-v1`

| Reposo | Subida de caldera | Origen (medido el 2026-10-19) |
|---|---|---|
| hasta 10 min | 0 | tras 10 min el primer uso no sale frío |
| 20 min | 2,7 °C | faltan 1,8 °C de agua (uso 315 frente a 316–319), a 0,66 °C de agua por °C de caldera |
| 30 min | 3,7 °C | con +2,9 °C faltaron 0,70 °C y con +4,0 °C sobraron 0,20 °C; interpolado |

Entre los puntos, recta. El reposo se redondea hacia abajo a tramos de 5 min:
la subida cambia pocas veces durante una espera y, si yerra, se queda corta.

**Más allá de 30 min se queda en 3,7 °C**, marcada como `beyond-measured`. Con
los déficits medidos a 40 min y a 3 h 13 min (2,8–2,9 °C) el café saldría unos
0,5 °C corto (inferido), nunca de más. El usuario decidió el 2026-10-22 no medir
reposos más largos.

**Por qué una regla medida y no el modelo.** El modelo del agua tras un reposo
(«Pe», `models/modelica/Documentacion/agua-portafiltro-reposo.md`) pidió de menos
en los tres reposos de 30 min: con lo que pedía, el primer uso quedó 0,7–0,8 °C
corto. La regla sale de esos mismos ensayos y su punto de 30 min es el que
cumplió el criterio de 0,5 °C.

### 12.4 Qué queda fuera

Comprobar en cada café del barista que el agua salió bien: en el uso normal no
hay sensor de agua. La comprobación es de los ensayos de puesta en servicio,
con la sonda en el portafiltro. Tampoco compensa los cafés siguientes al
primero, que ya salen bien.

## 13. Enmienda del 2026-10-22 — se retira la corrección después del café

**Decisión del usuario, 2026-10-22.** La corrección de las §§ 4.1–4.8 —mover la
referencia tras cada preparación, por la cuarta vía, dentro de la política de
§ 4.7— se retira del sistema. El sostenimiento queda con la subida del primer
uso (§ 12), la exclusión del barista y la suspensión del protocolo (§ 4.3).

**Por qué.** En la máquina real no podía corregir nunca, y no por falta de
datos sino por construcción (§ 12.1). El agua estimada tras el café sale de la
recta del acta v6, la misma con la que la placa fija la caldera, y con la
caldera en su referencia da el objetivo. Las 224 decisiones que dejó en la
plataforma entre el 2026-10-09 y el 2026-10-22 fueron todas «no corrige». Para
corregir de verdad tendría que conocer el agua de cada café, y en el uso normal
no hay sensor de agua. El café que sí sale frío, el primero tras un reposo, lo
cubre la subida. Conservarla obligaba a mantener y explicar un mecanismo que no
actúa, y su decisión por preparación no informaba nada.

**Qué se quita.** El paquete `espresso_sustainment.decision` y `.policy`, la
decisión tras cada preparación (`sustainment-decision`), la publicación de la
política v1, la cuarta vía de activación (§ 4.5) y el panel de la decisión en
el detalle de cada café, que ahora muestra la subida que la placa aplicó. Los
documentos ya guardados se conservan como registro. Con la política se retiran
sus umbrales: la corrección máxima de 2,8 °C y las dos correcciones seguidas
(guion 11.3), y la corrección máxima de presión (11.4) queda sin objeto en este
ADR.

**Qué queda.** De la § 4: el objetivo y la referencia como cosas distintas
(§ 4.1), lo que decide el barista (§ 4.3) y lo que el sistema no hace nunca
(§ 4.4). La § 9 se verifica sobre la subida (guion 11.5). La § 6.1 del marco
conceptual se apoya en la subida, que es el flujo automático de vuelta.

## 14. Enmienda del 2026-10-27 — la subida la decide el modelo

**Decisión del usuario** (2026-10-27): que el gemelo participe en el ajuste
dinámico. Desde esta enmienda la subida del primer uso de la § 12 la decide el
modelo del agua tras un reposo (Pe v7), y la regla `primer-uso-v1` queda de
respaldo.

**Cómo decide** (`primer-uso-pared-v1`, `espresso_sustainment.first_use`). La
plataforma simula con el gemelo, solo con lo que ya pasó, la pared del recorrido
por el grupo y el portafiltro. Compara la de ahora con la media de la pared al
pulsar los tres últimos cafés. Cuanto más fría está, más falta al primer café:
`D` = −1,243 − 0,390 · x °C, y la subida es `D` / 0,66, redondeada hacia abajo a
0,5 °C y con el tope de 4,0 °C del acta v7. Los números salen de los cinco reposos
del 19 de octubre. La ganancia es la medida en los usos 325 y 335.

**Por qué esta forma y no otra.** Se probaron trece formas, cada una con su
criterio fijado antes de correrla (`agua-portafiltro.md` § 10):

- predecir los cafés futuros no cumple, porque la caldera de esos cafés no se
  puede saber antes de pulsar (§§ 10.3–10.12);
- la pared relativa cumple los tres criterios (§ 10.19): 0,35 °C de error en los
  reposos del 19 frente a 0,41 °C de la regla, 0,22 °C en la comprobación ciega del
  30b, y 0,81 frente a 0,88 °C en los reposos ciegos del 18.

**Tras un arranque en frío** no hay tres cafés con que comparar. Si la primera
lectura tras el último hueco de la telemetría está bajo 50 °C, la máquina se
encendió fría, y la pared se compara con la típica de los cafés recuperados del
19, 75,0 °C (`agua-portafiltro.md` § 10.20). En el único arranque medido pide el
tope y deja el primer café 3,4 °C frío en vez de 6,0 °C, sin pasarse.

**Cuándo decide la regla.** Si la plataforma no puede calcular la pared: un hueco
de más de 60 s en la telemetría con la máquina caliente, o menos de tres cafés en
las últimas 2 h sin un arranque en frío.

**Lo que no cambia:** las decisiones de personas, que van primero (§ 12); el
café en curso, que mantiene lo pedido; y la placa, que acota la subida con su
acta. Con el modelo, además, la subida no baja hasta el café siguiente. Subirla
calienta el metal y con él la pared, y sin esa retención el modelo pediría menos
justo antes del café para el que pidió.

**Pendiente:** confirmarlo con reposos nuevos (§ 10.13 del estudio) y verificarlo
en la máquina con la plataforma de la Raspberry.


> **Hecho el 2026-10-27.** Los reposos nuevos de la § 10.23 del estudio no
> cumplieron el límite fijado antes por 0,04 °C; el usuario lo adoptó igual, por
> el empate con la regla en los reposos y el arranque en frío. Desplegado en la
> Raspberry y comprobado con un café (§ 10.24).

## 15. Enmienda del 2026-10-27 — el modelo pide el refuerzo en lugar de la subida

**Por qué.** El déficit del primer café está en la segunda mitad del chorro, y la
subida, que actúa antes de pulsar, no llega ahí. Con más prealimentación durante
ese café (ADR-009, enmienda del 2026-10-27), en bloques cruzados de 25 min el
déficit baja de 3,25 a 0,06 °C de media, y con el mismo refuerzo queda −0,28 °C
tras 15 min y 0,90 °C tras 45 min (estudio, § 10.26).

**Se decide (usuario, 2026-10-27)** el modelo `primer-uso-pared-v2`: la misma
pared de la § 14, y en lugar de la subida, el refuerzo.

- Si el déficit que estima el modelo llega a **2,0 °C**, pide
  `firstUseFeedforwardGain` = **1,25** para el próximo café y ninguna subida. Si
  no llega, no pide nada. El umbral es el déficit que el modelo estimó para el
  reposo más corto en que el refuerzo no se pasó (15 min, 2,02 °C); la ganancia,
  la del ensayo. Los dos son provisionales hasta la validación.
- **Tras un arranque en frío sigue pidiendo la subida**, como en la § 14: el
  refuerzo no se midió ahí.
- Igual que la subida: las decisiones de personas van primero, el café en curso
  mantiene lo pedido y, una vez pedido, el refuerzo no se retira antes del café.
  Cada cambio se guarda con su motivo (`model-feedforward`).

**Lo que no hace.** No gradúa el refuerzo: es uno solo, el medido. Si el modelo
tuviera que decidir cuánto, haría falta medir otras ganancias.

**Pendiente:** el máximo en el acta de temperatura, el firmware con él y una
validación a ciegas con reposos de 15, 25 y 45 min.

> **Hecho el 2026-10-28.** La validación de la v2 no cumplió (estudio, § 10.27): el
> refuerzo solo sube el primer café unos 2,3 °C. Desde el 28-oct rige
> `primer-uso-pared-v3`, que pide en el mismo café el refuerzo y la subida de la
> § 14. Tras 25 min deja el primer café 1,21 y 1,14 °C bajo los siguientes (§ 10.28 y
> § 10.29), sin cumplir el límite de 1,0 °C, y mejora 4,9 °C el arranque en frío. El
> usuario decidió dejarlo así, sin más ajustes.
