# ADR-005 — Regulación permanente, autoridad de interacción y seguridad operacional

**Estado:** Aceptada, revisada  
**Fecha de decisión original:** 2026-08-23  
**Fecha de revisión:** 2026-09-09; 2026-09-30 (§12: cambio de referencias para sostener el objetivo)
**Ámbito:** operación, regulación, interacción, seguridad y recomendaciones  
**Reemplaza:** la formulación inicial del mismo ADR que permitía iniciar desde una interfaz no determinada y no diferenciaba suficientemente regulación automática de decisión autónoma

## 1. Contexto

El sistema debe mantener estables la temperatura y la presión, pero no debe
convertir la cafetera en un equipo que inicie preparaciones, ejecute recetas
completas, cambie referencias o aplique recomendaciones de manera autónoma. El
término «modo de control» generó ambigüedad porque sugería que la regulación
podía habilitarse o deshabilitarse como una función opcional.

También se debe diferenciar el ajuste automático de la salida de un controlador
—necesario para alcanzar una referencia ya autorizada— de las decisiones que
modifican el objetivo o inician el proceso. La finalización automática por el
tiempo máximo previamente configurado tampoco equivale a una decisión autónoma
basada en inferencias. En el prototipo inicial la masa y el TDS se miden con
instrumentos externos, se introducen manualmente y no gobiernan los actuadores.

La primera versión no dispone de comprobaciones automáticas suficientes para
confirmar presencia del barista, portafiltro y recipiente, ni incorpora
detección automática de falta de agua. Permitir un inicio desde una interfaz de
red aumentaría los riesgos sin aportar una capacidad necesaria para validar los
objetivos de la tesis.

IEC 60335-2-15:2024 incluye las cafeteras dentro de los aparatos para calentar
líquidos e incorpora requisitos específicos para operación remota. Esta
referencia no constituye por sí sola una evaluación de conformidad del
prototipo, pero confirma que un arranque por red debe tratarse como una función
de seguridad y no únicamente de interfaz.

## 2. Problema de decisión

Se debe establecer:

- cuándo actúan los lazos de regulación;
- qué diferencia existe entre regulación y autonomía;
- qué medio autoriza el inicio y las diferentes formas de parada;
- qué componente decide las transiciones de estado;
- qué eventos finalizan una extracción;
- y qué límites separan el control local de las interfaces, la ontología, el
  razonador, el gemelo digital y el aprendizaje.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Regulación habilitada mediante un «modo de control» | Permite activación explícita, pero contradice la exigencia de estabilidad permanente y crea estados ambiguos. |
| Inicio desde cualquier interfaz autenticada | Facilita la interacción, pero amplía la superficie de ataque y no confirma que la máquina esté físicamente preparada. |
| Inicio desde la aplicación web con confirmación lógica | Reduce el alcance remoto, pero sigue dependiendo del navegador y de comprobaciones físicas que el prototipo no puede verificar. |
| Inicio y parada normal físicos, y desconexión manual del enchufe para intervenir | Mantiene la presencia del barista y conserva el control de la cafetera en el mando físico. El botón del panel solo cambia el estado lógico. Alternativa seleccionada para el prototipo supervisado. |
| Preparación autónoma por receta o recomendación | Automatiza el proceso completo, pero excede el alcance y desplaza la autoridad del barista. |

## 4. Decisión

### 4.1 Regulación automática sin decisión autónoma

No existirá un modo de control seleccionable. La regulación formará parte del
comportamiento normal de la cafetera y actuará según el estado autorizado:

| Estado | Regulación y actuadores |
|---|---|
| Seguro o inactivo | Bomba y calentador permanecen en la condición segura definida. |
| Calentamiento o preparada | El controlador regula la temperatura alrededor de la referencia autorizada; la bomba de extracción permanece inactiva. |
| Extracción activa | El sistema regula temperatura y presión hasta que el barista solicite una parada normal, se alcance el tiempo máximo o ocurra una causa de seguridad. |
| Alarma | El supervisor inhibe las acciones no permitidas y conduce los actuadores a la condición segura correspondiente. |

La máquina de estados del firmware será la autoridad operativa. Los
controladores podrán modificar la señal aplicada al calentador o a la bomba
para reducir el error respecto de una referencia autorizada. Esta actuación no
les permite:

- modificar la referencia;
- seleccionar o crear un perfil;
- iniciar una extracción;
- elegir una recomendación;
- cambiar el criterio de terminación;
- ni eludir una protección de seguridad.

### 4.2 Autoridad de las interfaces

Se adopta la siguiente matriz para la primera versión:

| Medio o componente | Iniciar extracción | Parada normal anticipada | Desconexión para intervención |
|---|---:|---:|---:|
| Mando físico de la cafetera | Sí | Sí | No. |
| Desconexión manual del enchufe | No | No; retira físicamente la alimentación | Sí, para intervención eléctrica. |
| Aplicación web externa | No | No | No. |
| Servicios externos de análisis, conocimiento y aprendizaje | No | No | No. |
| Ontología, grafo, razonador, gemelo digital o modelo | No | No | No. |
| Supervisor de seguridad | No | No; produce una terminación por protección | No sustituye la desconexión manual del enchufe; lleva las salidas a estado seguro mientras dispone de energía. |

El inicio de una extracción requerirá siempre una acción deliberada del
barista sobre un mando físico ubicado en la cafetera. La máquina de estados
validará que el sistema se encuentre preparado antes de activar la bomba.
Ningún temporizador, perfil, interfaz web, API, servidor, modelo,
recomendación, sincronización, actualización o reconexión podrá iniciar la
extracción.

La aplicación web externa es histórica y asíncrona. Solo presentará
información, perfiles y recomendaciones como objetos pendientes. La parada
normal anticipada se realizará mediante el mando físico de la cafetera y será
procesada por la máquina de estados y el supervisor de seguridad. La acción y
su origen físico quedarán registrados.

La desconexión eléctrica para intervenir se realizará retirando manualmente el
enchufe de la cafetera. El prototipo no incorpora un pulsador dedicado de
parada de emergencia, un contactor `K_SAFE` ni un relé intermedio. El botón de
encendido del panel solo cambia el estado lógico y no corta la red. Por tanto,
esta arquitectura no ofrece una parada eléctrica de emergencia durante la
operación; el prototipo debe permanecer supervisado y desconectarse del
enchufe antes de cualquier intervención eléctrica.

### 4.3 Criterios, causas y prioridad de terminación

Una extracción podrá terminar por una de estas causas, que deberán registrarse
sin ambigüedad:

1. `maximum-extraction-time-reached`: se alcanza el tiempo máximo configurado;
2. `manual-normal-stop`: parada solicitada por el barista mediante
   el mando físico;
3. `safety-protection-trip`: actuación automática de una protección o del
   supervisor de seguridad.

La precedencia será `safety-protection-trip` → `manual-normal-stop` →
`maximum-extraction-time-reached`. La causa registrada será la primera
condición de mayor prioridad aceptada por la máquina de estados; no se inferirá
después a partir de los valores finales.

Retirar el enchufe puede interrumpir la energía antes de que el firmware
escriba una causa final. Por ello la desconexión eléctrica no se modela como
una cuarta causa normal de terminación. En el siguiente arranque, un registro
abierto se marca como `unexpected-power-loss` y se conserva como preparación
incompleta.

**Implementación en la placa, 2026-10-12.** Hasta esa fecha la preparación
abierta vivía solo en RAM y un corte la borraba sin rastro; lo encontró la
revisión previa a la prueba de la § 8, punto 10, que por eso no se hizo. Al
abrirla se guarda en NVS una marca (`open_extraction.h`) con el perfil, el
origen de la medida, la calibración, el firmware y la hora de pared de ese
momento, en la misma escritura que su número; al cerrarla se borra. El
arranque que la encuentra envía el registro con `finishedAt` y `durationMs`
nulos, cero muestras y sin indicadores: **no consta cuándo se cortó la
energía**, y la hora del rearranque afirmaría una duración que nadie midió. Se
descartó reescribir la marca durante la preparación para acotar esa duración:
cada escritura en flash detiene la caché, justo mientras regula el lazo de
presión. El envío queda tras `CAFE_REPORT_INTERRUPTED_EXTRACTIONS`, activado desde que
el contrato v1 admite esos nulos solo con esa causa; una plataforma anterior
rechazaría el lote entero, porque la cola se entrega por lotes.

Toda configuración tendrá un tiempo máximo. No se utilizarán masa, TDS,
rendimiento de extracción, recomendaciones, inferencias ni evaluaciones
sensoriales como criterios directos de terminación. El barista podrá observar
una gramera externa y solicitar la parada normal; después introducirá
manualmente la masa final junto con los demás valores medidos externamente.

### 4.4 Perfiles y configuración efectiva

El sistema mantendrá una configuración predeterminada, previamente validada,
con referencias de temperatura y presión y tiempo máximo. Opcionalmente podrá
incluir una masa esperada de bebida para comparación posterior, sin utilizarla
para detener la bomba. La selección de un perfil
será opcional. Cuando el barista no seleccione uno, la extracción utilizará los
valores predeterminados.

Las configuraciones alternativas deberán validarse localmente antes de
activarse. Si una configuración es inválida, la configuración efectiva
permanecerá en el último perfil válido o en los valores predeterminados.
Ninguna configuración constituye una secuencia autónoma de fases ni autoriza
el inicio de la extracción.

La ausencia de un perfil, metadatos del café, masa de café seco, masa final de bebida, TDS,
evaluación sensorial, ontología, razonador o servidor no bloqueará una
extracción. Sí podrán impedirla o detenerla una condición de seguridad, una
precondición operacional obligatoria o la invalidez de una medición crítica
para regular.

### 4.5 Recomendaciones y cambios de referencia

Una recomendación puede proceder de un procedimiento documentado, del
razonador basado en evidencia o, posteriormente, de un modelo validado. Ninguna
de ellas constituye una orden.

El recorrido autorizado será:

```text
recomendación
    → decisión explícita del barista
    → configuración pendiente
    → validación local
    → activación explícita
    → inicio físico de una extracción posterior
    → evaluación del resultado
```

Aceptar una recomendación no la activa. Activarla no inicia una extracción y
no demuestra que el ajuste sea eficaz. La configuración resultante conservará
su procedencia y el resultado deberá evaluarse en una preparación posterior.

### 4.6 Prioridad de seguridad y degradación

Las protecciones de seguridad tendrán prioridad sobre mandos, interfaz,
regulación ordinaria, gemelo digital, ontología, razonador, aprendizaje y
recomendaciones. Durante encendido, reinicio, actualización o alarma, los
actuadores permanecerán en la condición segura definida.

La pérdida de servidor, aplicación externa, navegador o Wi-Fi no modificará la
configuración efectiva ni dejará actuadores sin supervisión. La pérdida de una
dependencia local necesaria para regular, como una medición crítica válida,
producirá la respuesta segura definida y quedará registrada.

## 5. Consecuencias

### 5.1 Consecuencias favorables

- La presencia del barista se expresa mediante una acción física antes del inicio.
- La regulación permanece automática sin adquirir autoridad para cambiar objetivos.
- La parada normal depende del mando físico.
- Una falla de red no elimina los medios físicos de parada.
- La aplicación externa y los componentes de conocimiento permanecen fuera del control.
- Las tres causas de terminación controlada y la recuperación tras una pérdida de energía pueden analizarse sin fingir un registro que no pudo persistirse.

### 5.2 Consecuencias desfavorables

- El barista debe encontrarse físicamente junto a la cafetera para iniciar.
- La aplicación web no permite iniciar aun cuando el usuario esté autenticado.
- Retirar el enchufe apaga también la lógica, por lo que la causa se recupera después del reinicio y no siempre puede registrarse antes de la desconexión.
- La solución es adecuada solo para un prototipo supervisado y no equivale a una parada de emergencia certificada.
- La regulación debe permanecer disponible durante la operación normal y cumplir las tolerancias experimentales.
- Cualquier ampliación hacia inicio por red o control autónomo exigirá revisar requisitos, riesgos y pruebas.

## 6. Riesgos y medidas de tratamiento

| Riesgo | Tratamiento requerido |
|---|---|
| Inicio involuntario | Inicio exclusivamente físico, validación de estado, tratamiento de rebote y ausencia de arranque por reinicio o reconexión. |
| Solicitud web de actuación | La aplicación web no expone acciones directas sobre la cafetera; el firmware valida cualquier dato recibido. |
| Confusión entre parada normal y desconexión eléctrica | El mando denomina “parada normal” a la terminación controlada; retirar el enchufe es una desconexión completa para intervención. |
| Dependencia de la aplicación web | El mando físico y la lógica local mantienen la operación cuando la aplicación web no está disponible. |
| Referencia fuera de límites | Validación local y límites protegidos no modificables desde una sesión ordinaria. |
| Fallo de sensor o tarea crítica | Transición a estado seguro y registro de la causa. |
| Confusión entre recomendación y orden | Estado pendiente, activación explícita y separación entre aceptación, aplicación y evaluación. |
| Error de transcripción en un valor medido externamente | Validación de unidad y rango, confirmación visible y registro del origen manual; el dato nunca modifica una extracción activa. |
| Confusión entre masa esperada y criterio de parada | La interfaz y el esquema de datos la identifican como referencia informativa para comparación posterior. |

## 7. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Tolerancias de estabilidad térmica y de presión | Caracterización de la planta y aprobación del protocolo experimental. |
| Ganancias, filtro definitivo y límites experimentales del control de presión | ADR-007 cerró PI unilateral y PSM como estrategia y mecanismo. Los valores numéricos se obtienen durante la puesta en marcha y deben validarse antes de operar el lazo presurizado. |
| Rangos autorizados de temperatura, presión y tiempo máximo | Validación técnica y experimental. |
| Valores predeterminados de temperatura, presión y tiempo máximo | Definición dentro de los rangos autorizados y validación antes de operar el prototipo. |
| Mando físico de inicio y parada normal | Selección eléctrica, tratamiento de rebote, ubicación y pruebas de interacción. |
| Procedimiento de desconexión manual y recuperación tras pérdida de energía | Verificar que retirar el enchufe elimina la alimentación de lógica, calentador y bomba y que un registro abierto se recupera como incompleto. |
| Tabla completa de transiciones | Eventos, guardas, tiempos máximos y salidas seguras documentados en la arquitectura lógica. |
| Precondición manual de agua | Procedimiento visible y verificable mientras no exista detección automática. |

La autoridad de las interfaces ya no es un aspecto pendiente: el inicio es
físico, la parada normal se solicita mediante el mando físico y la
desconexión eléctrica para intervención se realiza manualmente retirando el
enchufe.

## 8. Verificación de la decisión

Se probará, como mínimo:

1. regulación térmica en calentamiento, preparada y extracción activa;
2. regulación de presión únicamente durante una extracción activa;
3. rechazo de todo intento de inicio desde la aplicación web, aplicación
   externa, API, recomendación, reconexión o reinicio;
4. inicio físico únicamente cuando la máquina de estados se encuentre preparada;
5. finalización automática por tiempo máximo;
6. parada normal desde el mando físico, con origen físico registrado;
7. ingreso manual validado de dosis, masa final y TDS después de la extracción;
8. comprobación de que esos valores no detienen la bomba ni cambian referencias activas;
9. pérdida de navegador o Wi-Fi sin impedir las paradas físicas ni el tiempo máximo local;
10. desconexión manual del enchufe sin depender del software de interfaz y recuperación del registro incompleto al volver a energizar;
11. actuación de protecciones ante sensores inválidos, sobretemperatura o fallo crítico;
12. precedencia correcta cuando coincidan dos causas de terminación;
13. rechazo de configuraciones o recomendaciones inválidas sin alterar la configuración efectiva;
14. imposibilidad de que el grafo, el razonador, el modelo o el servidor escriban actuadores o referencias activas;
15. recuperación controlada después de cada causa de terminación o alarma.

## 9. Trazabilidad

- Requisitos funcionales: RF-001–RF-006 y RF-014–RF-018.
- Requisitos no funcionales: RNF-002–RNF-007.
- Restricciones de diseño: RD-001–RD-005.
- Decisiones relacionadas: ADR-001, ADR-003, ADR-004, ADR-006 y ADR-007.

## 10. Referencias de apoyo

- [IEC 60335-2-15:2024 — Particular requirements for appliances for heating liquids](https://webstore.iec.ch/en/publication/70372). Incluye cafeteras e incorpora requisitos de operación remota; la conformidad exige consultar la norma completa aplicable.
- [NIST IoT Device Cybersecurity Capabilities Catalog — Logical Access to Interfaces](https://pages.nist.gov/IoT-Device-Cybersecurity-Requirement-Catalogs/technical/logical/). Orienta la autenticación, autorización, restricción de interfaces y control de acciones remotas.

## 11. Criterios de reconsideración

El inicio desde una interfaz solo podrá reconsiderarse si se aprueba un nuevo
alcance y se dispone de análisis formal de riesgos, comprobaciones suficientes
de preparación física, autenticación robusta, protección contra repetición,
armado o confirmación local, pruebas de pérdida de red y validación de
seguridad.

La ejecución de fases automáticas, el cambio autónomo de referencias o la
aplicación automática de recomendaciones requerirán igualmente nuevos
requisitos, autorización académica y validación antes de implementarse. La
prohibición de acceso directo de interfaces o servicios externos a los
actuadores permanecerá vigente.

## 12. Revisión · Cambio de referencias para sostener el objetivo

**Fecha:** 2026-09-30
**Motivo:** [ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md)
permite que la plataforma cambie referencias entre preparaciones para sostener
el objetivo que el barista activó, dentro de una política fijada por los
autores. Es exactamente el «cambio autónomo de referencias» que la § 11 somete a
condiciones.

### 12.1 Qué cambia y qué no

| Sección | Efecto |
|---|---|
| § 4.1 | Sin cambios. La restricción se refiere a los controladores del firmware, que siguen sin poder modificar la referencia. La corrección ocurre fuera del lazo, entre preparaciones |
| § 4.2 | Sin cambios. El sostenimiento no inicia, detiene ni prolonga una extracción |
| § 4.5 | El recorrido admite una variante: para las correcciones de sostenimiento, la decisión explícita es la activación del objetivo, y los límites los fija la política de los autores. Validación local, entrada en vigor tras la preparación en curso y evaluación posterior se mantienen |
| § 8, punto 14 | Sin cambios. El modelo no escribe referencias activas: la corrección baja como configuración y el dispositivo la valida |

### 12.2 Condiciones de la § 11

La § 11 exige, antes de implementar un cambio autónomo de referencias, nuevos
requisitos, autorización académica y validación. Su estado es:

| Condición | Estado |
|---|---|
| Nuevos requisitos | **Cumplida el 2026-09-30**: RF-030 |
| Autorización académica | **Concedida el 2026-09-30** por los autores al aprobar ADR-018. Decidieron que su aprobación basta y que no se requiere la de la dirección del trabajo |
| Validación | Definida en ADR-018 § 9; se ejecuta sobre la implementación |

El requisito y la autorización están cumplidos, y la validación se ejecutará
sobre la implementación. El sostenimiento puede construirse ya; su activación
sobre la máquina depende, además, de la puesta en servicio que exige el
guardián de actuación.
