# ADR-012 — Canal de comunicación entre la ESP32-S3 y la plataforma externa

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Ámbito:** transporte, contenido, cadencia, direccionamiento e identidad del enlace entre el controlador local y la plataforma externa

## 1. Contexto

ADR-001 § 6 establece que la comunicación será local y utilizará un contrato
versionado, y que la ESP32-S3 conserva la autoridad sobre la adquisición, el
control y la seguridad. ADR-002 define la cola local acotada como garantía de
que la evidencia no se pierde durante una desconexión. ADR-003 § 6 fija que la
plataforma externa es la fuente del historial consolidado y la ESP32-S3 la del
estado instantáneo.

Los requisitos añaden dos exigencias distintas sobre este enlace. RF-016 pide
sincronizar los registros pendientes sin duplicarlos cuando exista conectividad.
RF-013 pide que la aplicación web permita **consultar estado, alarmas, tiempo,
mediciones y referencias**, lo que implica conocer la máquina mientras opera y
no solo después.

### 1.1 Situación comprobada

Comprobado el 2026-09-15 sobre el firmware y los contratos del repositorio:

| Elemento | Situación |
|---|---|
| Transporte | Implementado: `POST` HTTP con credencial en cabecera `Authorization`, hacia `/api/v1/sync` |
| Cola local | 32 sobres de hasta 1536 bytes en la partición no volátil |
| Cadencia de entrega | Cada 15 s, configurable en la compilación |
| Confirmación | El dispositivo elimina de la cola únicamente los mensajes que la plataforma reconoce |
| Tipos de mensaje contratados | `telemetry.sample`, `operational.event`, `extraction.completed` y `profile.proposal` |
| Mensajes que el firmware emite hoy | Solo `extraction.completed`; `telemetry.sample` y `operational.event` se añadieron el 2026-09-15 |
| Contenido de ese mensaje | Resumen con estadísticas, número de muestras y referencia a la serie; **no transporta las muestras** |
| Capacidad de la serie local | Hasta 1200 muestras por extracción |
| Dirección de la plataforma | **Constante de compilación**, con valor actual apuntando al equipo de desarrollo |
| Credencial del dispositivo | **Credencial de barista de desarrollo**, compilada en el firmware |

De aquí se desprende que el enlace diferido está resuelto y que **no existe
ningún camino por el que la plataforma conozca el estado durante una
preparación**.

## 2. Problema de decisión

Se debe confirmar el transporte del enlace, decidir cómo se satisface la
consulta de estado durante la operación sin comprometer la evidencia acumulada
en la cola, y resolver cómo el dispositivo localiza y se identifica ante la
plataforma.

## 3. Alternativas consideradas

### 3.1 Transporte

| Alternativa | Evaluación |
|---|---|
| HTTP con entrega periódica desde el dispositivo | Ya implementado y verificado contra la API, con confirmación por mensaje. No añade servicios a la plataforma. **Seleccionada.** |
| MQTT con intermediario en la plataforma | Añade un servicio permanente más al equipo desplegado y obliga a reescribir el transporte del firmware, para resolver un reparto entre múltiples suscriptores que este sistema no tiene. |
| WebSocket persistente | Mantiene una conexión abierta que debe restablecerse y vigilarse, sin ventaja frente a la entrega periódica para una supervisión humana. |

### 3.2 Estado durante la preparación

| Alternativa | Evaluación |
|---|---|
| No enviar estado; la plataforma solo conoce extracciones terminadas | Incumple RF-013, que exige consultar mediciones y alarmas durante la operación. |
| Encolar las muestras de telemetría junto con la evidencia | A una muestra por segundo, la cola de 32 sobres se agota en 32 s y desplaza precisamente los registros que debe conservar. |
| Enviar telemetría por el mismo canal, sin persistirla, y descartarla si no hay enlace | Satisface la consulta de estado sin comprometer la evidencia. **Seleccionada.** |
| Exponer un servicio consultable en la ESP32-S3 | Convierte al controlador en servidor y añade carga y superficie de exposición al nodo que debe conservar la operación crítica. |

### 3.3 Direccionamiento e identidad

| Alternativa | Evaluación |
|---|---|
| Dirección y credencial como constantes de compilación | Situación actual: cualquier cambio de red o rotación de credencial exige recompilar y regrabar el firmware. |
| Dirección y credencial como configuración conservada en el dispositivo | Coherente con ADR-002, que ya sitúa la configuración en la memoria no volátil local. **Seleccionada.** |

## 4. Decisión

1. **El transporte del enlace es HTTP**, con entrega iniciada por el
   dispositivo y confirmación por mensaje. No se incorporará un intermediario de
   mensajería a la plataforma externa.
2. **El dispositivo emitirá muestras de telemetría durante la preparación**,
   utilizando el tipo de mensaje ya contratado, con una **cadencia inicial de
   una muestra por segundo** durante la extracción y sin emisión sostenida en
   reposo. La cadencia es un valor inicial, revisable con las mediciones de la
   plataforma.
3. **La telemetría en vivo no ocupará la cola local del dispositivo.** Se
   entrega en el momento y se descarta si no hay enlace; la cola queda reservada
   a la evidencia: extracciones terminadas y eventos operativos.
   **En la plataforma, en cambio, las muestras recibidas se conservan** asociadas
   a su preparación: constituyen la serie medida con la que se compara el modelo
   virtual (§ 4.1).
4. **La dirección de la plataforma y la credencial del dispositivo serán
   configuración conservada en el dispositivo**, no constantes de compilación.
   El dispositivo tendrá identidad y credencial propias, distintas de las de
   cualquier persona usuaria.

La autoridad no cambia: la telemetría es informativa, ninguna respuesta de la
plataforma inicia o detiene una preparación, y las propuestas de perfil siguen
sujetas a validación local según ADR-001 § 6.

### 4.1 La telemetría es también la serie medida

La comparación entre el proceso físico y el virtual, exigida por RF-020 y
RF-028, se construye con las muestras que la plataforma conserva de cada
preparación. Esas muestras son las que llega por este canal: no existe otra vía
por la que las mediciones de alta frecuencia alcancen la plataforma, porque la
cola del dispositivo transporta resúmenes y no series.

De ello se siguen dos consecuencias:

- **La telemetría en vivo es evidencia, no solo presentación.** La plataforma la
  persiste asociada a la preparación, con su número de orden.
- **La cadencia acota la comparación.** El comparador utiliza hasta mil muestras
  por preparación: a una por segundo cubre dieciséis minutos de extracción,
  mientras que a diez por segundo se agotaría en menos de dos. La cadencia
  elegida no es solo una cuestión de carga.

**No debe confundirse con el registro del lazo de control.** ADR-007 § 3.6 exige
un registro de la regulación que no dependa de la plataforma externa durante la
extracción, y por eso sale por el puerto serie a alta frecuencia, para análisis
fuera de línea. La telemetría de este canal es otra cosa: baja frecuencia,
contrato versionado y destino en la plataforma. Son dos vías separadas y deben
seguir siéndolo; convertir una en fuente de la otra ataría el registro del lazo a
la disponibilidad de la red.

Una desconexión durante la extracción deja huecos en esa serie. El comparador ya
contempla esa situación mediante un umbral mínimo de cobertura, por debajo del
cual declara que no hay información suficiente en lugar de comparar con datos
incompletos.

**Enmienda del 2026-10-19 — la serie lleva delante el minuto previo a pulsar.**
Pedida por la plataforma y aprobada por el usuario el 2026-10-19.

La § 4.4 de la monografía define la estabilidad térmica de la máquina sobre el
cuerpo de la caldera **en el minuto previo a pulsar**, con la máquina lista:
dentro de ±0,8 °C de su referencia. La serie medida empezaba al pulsar, así que
la plataforma (`packages/espresso_stability/machine.py`) no tenía con qué
calcularla y declaraba «sin muestras previas». Desde esta enmienda,
`extraction.series` lleva delante de las muestras de la preparación las del
minuto anterior, con `elapsedMs` negativo, sobre la misma rejilla de 500 ms y
con la misma forma: temperatura y presión, cada una con su calidad.

| Aspecto | Decisión |
|---|---|
| Dónde se guarda | Un anillo puro (`pre_start_history`) con una muestra cada 500 ms mientras la máquina está **lista**. Al abrir la preparación, la sesión copia las del último minuto con el instante relativo al de apertura, el mismo origen que las de la preparación |
| Qué es «minuto previo» | La racha continua de máquina lista que termina al pulsar. Se vacía en cada cambio de estado aceptado y cuando pasa más de 1 s sin observarla; lo anterior a un hueco no fue máquina lista y haría salir la fracción dentro de la banda mejor de lo que fue |
| Sin historia | Se manda la que haya: recién llegada a «lista», tras un purgado o tras un reinicio la serie lleva menos de un minuto, o nada. No se rellena |
| Muestra no apta | Se guarda y se emite marcada (`null` con su calidad), como el resto de la serie |
| Origen | El anillo no mezcla medido y simulado, y la preparación solo toma el minuto si su origen coincide con el suyo al abrir |
| Lo que no toca | No cuenta en `sampleCount` ni en los indicadores del resumen, ni abre la ventana de estabilidad de RF-019: describe la máquina antes de la preparación, no la preparación |
| Contrato | `elapsedMs` admite desde −60 000. Quien calcule algo **de la preparación** debe filtrar `elapsedMs ≥ 0` |

Alternativas descartadas, con su razón:

- **Leer el anillo al cerrar, sin copia en la sesión.** Ahorra 1,9 KB, pero la
  evidencia de una preparación dependería del estado vivo de otra fase en el
  instante de serializarla, y del orden entre el cierre y el vaciado del anillo.
  La serie se compone desde la sesión precisamente para no depender de eso.
- **Guardar el minuto a la cadencia de registro (100 ms) y seleccionar al
  pulsar.** Cinco veces la memoria para acabar conservando lo mismo.
- **Anclar la rejilla previa al instante de pulsar.** Ese instante no existe
  cuando se guarda; la rejilla queda anclada al comienzo de la racha y los
  instantes negativos no son múltiplos exactos de 500. Interpolar para que lo
  fueran fabricaría valores que nadie midió.
- **Una muestra por segundo.** La plataforma la admite y costaría la mitad de
  cola y de memoria, pero la serie tendría dos densidades bajo un solo
  `intervalMs`, que dejaría de decir la verdad sobre la mitad de ella.
- **Contar también el calentamiento o el purgado.** No es máquina lista, que es
  la condición que la § 4.4 pone.

Consecuencias, medidas en el anfitrión:

- **Tramos de nueve muestras, no de diez.** Con diez, cualquier serie simulada
  con minuto previo se salía del hueco de 1536 bytes —sus instantes de −10 000 a
  −60 000 ocupan seis caracteres y la calidad «simulated» cuatro más que
  «valid»— y se perdía entera. El peor tramo posible ocupa ahora 1460 bytes
  (simulado) y 1386 (medido). Con diez, una simulación de más de 100 s ya se
  salía antes de esta enmienda.
- **Más cola por preparación.** Un café de 30 s pasa de 6 tramos a 20 (120
  muestras previas y 60 propias, de nueve en nueve). Con los 32 huecos de la
  cola, sin enlace cabe una preparación completa y parte de la siguiente donde
  antes cabían tres. El orden de § 9.1 protege la ficha frente a su propia
  serie, no frente a las series de preparaciones anteriores: con la cola llena,
  la ficha de una preparación posterior tampoco entra. Con enlace la cola se
  vacía al ritmo de la sincronización y esto no ocurre.
- **Memoria.** 3,9 KB más en RAM estática: 1956 bytes el anillo y 1940 la copia
  en la sesión (121 muestras de 16 bytes cada una).

## 5. Justificación

1. **Lo construido funciona.** El transporte, la cola, la confirmación y los
   contratos existen y están verificados; sustituirlos exigiría rehacer firmware
   probado sin resolver ninguna carencia real.
2. **Separar evidencia de observación protege la evidencia.** La cola tiene 32
   posiciones y existe para que no se pierda lo que ocurrió. Mezclar en ella
   muestras efímeras la agotaría en medio minuto.
3. **La cadencia se elige para un observador humano.** Una extracción de un
   minuto genera 60 mensajes a una muestra por segundo, frente a 600 a diez por
   segundo, sin diferencia apreciable para quien mira una pantalla. La
   regulación no depende de este canal: es local, según RNF-001.
4. **La configuración en red no puede exigir regrabar el firmware.** Una
   dirección compilada convierte cualquier cambio de red en una intervención
   sobre el controlador de la máquina.
5. **La identidad del dispositivo debe poder revocarse por separado.** Compartir
   credencial con una persona usuaria impide retirar una sin afectar a la otra.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- La plataforma puede mostrar el estado durante la preparación, como exige RF-013.
- La evidencia conserva íntegra su capacidad de cola.
- El equipo desplegado no incorpora servicios adicionales.
- La instalación tolera cambios de red sin intervenir el firmware.

### 6.2 Consecuencias desfavorables

- Se añade una vía de emisión en el firmware que hoy no existe y debe escribirse.
- La plataforma debe conservar y exponer un último estado conocido, con su
  antigüedad declarada.
- Una desconexión durante la extracción deja huecos en la vista en vivo; la
  evidencia posterior los cubre, pero la vista no es un registro continuo.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Interpretar la vista en vivo como registro completo | Declarar su antigüedad y su carácter no persistente; la evidencia procede siempre de la extracción confirmada. |
| Saturación del enlace o de la plataforma | Cadencia acotada, medida en la campaña de dimensionamiento y ajustable. |
| Telemetría que compite con las tareas críticas | Emisión en el núcleo no crítico y con prioridad inferior, según ADR-001 § 4.3. |
| Pérdida de evidencia por cola llena | Sin cambios respecto a ADR-002: la telemetría no consume posiciones de cola. |
| Credencial del dispositivo expuesta | Identidad propia, revocable, y enlace restringido a la red local. |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Emisión de telemetría en el firmware | Implementarla sin persistencia y sin afectar a las tareas críticas. |
| Último estado conocido y serie de la preparación | Conservar las muestras recibidas asociadas a la preparación y exponer el último estado con su antigüedad. |
| ~~Lectura de dirección y credencial desde la configuración del dispositivo~~ | **Implementado el 2026-09-15**: el valor compilado es la puesta en servicio y el guardado lo desplaza; el diagnóstico de arranque declara cuál está en vigor. |
| ~~Eventos de condición sin cambio de estado~~ | **Implementado el 2026-09-15**: el registrador observa el veredicto del guardián y publica al entrar y al salir de una condición insegura, distinguiendo avería de inhibición normal de servicio. |
| Vía para modificar la configuración en operación | El dispositivo ya lee lo guardado, pero todavía no existe forma de escribirlo sin programar: falta una orden por consola o un mensaje de la plataforma. Hasta entonces, cambiar de dirección sigue exigiendo regrabar. |
| Cadencia definitiva | Confirmar una muestra por segundo con las mediciones de la plataforma externa. |
| ~~Contrato de la respuesta de sincronización~~ | **Cerrado el 2026-09-15**: definido en `sync-response.schema.json` y consumido por el dispositivo, que lee la propuesta, la valida contra sus límites y la activa o la rechaza con causa. |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. entrega de una extracción terminada, con confirmación y sin duplicados;
2. consulta del estado durante una preparación en curso desde la aplicación web;
3. desconexión durante una extracción: la vista en vivo se interrumpe y la
   evidencia llega completa al restablecerse el enlace;
4. cola con capacidad íntegra tras una preparación completa con telemetría
   emitida;
5. cambio de dirección de la plataforma sin regrabar el firmware;
6. operación normal de la cafetera con la plataforma apagada.

### 9.1 Verificación realizada el 2026-09-15

Primera comprobación con el dispositivo real contra la plataforma. Quedan
verificados **los puntos 1, 2, 3, 5 y 6**. El punto 4 —capacidad íntegra de la
cola tras una preparación completa— sigue pendiente de comprobarse de forma
explícita.

Los puntos 3, 5 y 6 se verificaron a la vez y sin tocar la plataforma: se apuntó
el dispositivo a una dirección inalcanzable con la orden de servicio que
implementa § 4.4, **la máquina preparó café igual** —que es RNF-001—, la
evidencia quedó retenida mientras las entregas se rechazaban sin borrar nada, y
al devolver la dirección llegaron los siete registros pendientes de una vez, con
sus instantes reales.

Hasta esa campaña, el punto 5 era incumplible: `device_config` sabía guardar la
dirección desde el principio, pero no existía ninguna vía para pedírselo. El registro completo está en la
validación del enlace entre el dispositivo y la plataforma.

La campaña destapó un defecto que afecta directamente a esta decisión y que
conviene conservar aquí, porque nace de una propiedad del canal y no de un
descuido puntual.

**Un identificador de preparación repetido hace desaparecer la evidencia sin
avisar.** El número de preparación del dispositivo volvía a uno en cada
arranque, de modo que el primer registro posterior a un reinicio reutilizaba el
identificador del primero anterior. La plataforma lo reconoce como un reenvío
—que es lo correcto, porque reenviar tras un corte de red es el caso que § 4
quiere permitir— y responde que ya lo tiene; el dispositivo lo borra de la cola
por entregado. Y sus muestras, que llevan identificadores propios, sí entran y
quedan colgando de la preparación anterior: **evidencia de una sesión atribuida
a otra**. Se comprobó con 16 muestras archivadas bajo una preparación del día
anterior.

La corrección está en el dispositivo, que ahora conserva el número entre
arranques. Pero el episodio deja una propiedad del canal anotada: la
confirmación dice *«lo tengo»*, no *«es nuevo»*, y el dispositivo no puede
distinguir un reenvío legítimo de una colisión de identificadores. Mientras esa
distinción no exista, la unicidad del identificador es una obligación del
dispositivo y no algo que el canal pueda comprobar por él.

Queda un límite: el identificador no incorpora el del equipo que lo registró,
de modo que dos dispositivos con contadores independientes volverían a
colisionar. Hoy hay uno solo.

**La telemetría perdida deja la preparación sin serie.** § 4 decide que la
telemetría no ocupe la cola y se pierda si no hay enlace, y así ocurrió: la
preparación hecha con la plataforma inalcanzable se archivó completa —con su
instante real, su duración, su configuración y sus estadísticos— pero **sin
ninguna de sus 270 muestras**. Su resumen declara 270 y la serie tiene cero.

La decisión sigue siendo defendible para el estado en vivo, que no tiene sentido
diferido. Pero desde que § 4.1 declara que esa misma telemetría **es la serie
medida**, perderla deja la preparación fuera de la comparación físico-virtual,
que es lo que el sistema existe para hacer.

**Resuelto el 2026-09-15 separando los dos papeles.** El estado en vivo sigue
viajando en `telemetry.sample`, sigue sin ocupar la cola y sigue perdiéndose sin
enlace, que es lo correcto para lo que es. La serie medida pasa a componerse al
cerrar la preparación y a viajar por la cola persistente, en `extraction.series`,
de modo que sobrevive a un corte igual que el resumen.

| Aspecto | Resolución |
|---|---|
| Rejilla conservada | 500 ms, que es la que declara el plan de comparación físico-virtual para alinear el modelo sobre la serie medida. El dispositivo registra más denso; lo que sobra no se compara con nada y sí ocuparía cola |
| Orden de encolado | Después del resumen, para que ante una cola llena se pierda la serie y nunca la ficha de la preparación |
| Criterio de conservación | Selección, no promedio ni interpolación: cada punto es una muestra realmente tomada en el instante que declara |
| En la plataforma | La serie **sustituye** a las muestras provisionales de su preparación; conservar ambas dejaría dos versiones y un recuento que no cuadra con el resumen |
| Límite | Sin hora al cerrar, la serie no puede componerse ni retenerse: sus muestras viven en la sesión, que la preparación siguiente sobrescribe. Es el límite de ADR-013 § 7 aplicado a la serie |

De paso apareció que la telemetría salía cada segundo, la mitad de la resolución
que la comparación espera.

Verificado en la placa: con enlace, 54 muestras cada 500 ms frente a las 19 de
antes; sin enlace, la preparación llegó completa con sus 54 muestras al
restablecer la dirección, donde antes llegaban cero.

## 10. Trazabilidad

- [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md) § 6:
  contrato versionado y límites de autoridad.
- [ADR-002](ADR-002-persistencia-interna-y-almacenamiento-externo.md):
  cola local, capacidad y política de confirmación.
- [ADR-003](ADR-003-interfaz-web-unica-en-plataforma-externa.md) § 6:
  frontera de comunicación y fuentes autorizadas.
- Alcance de producción de la plataforma externa.
- Requisitos: RF-013, RF-014, RF-015, RF-016 y RNF-001.

## 11. Criterios de reconsideración

La decisión deberá revisarse si el sistema incorpora más dispositivos o
consumidores del flujo en tiempo real, si la cadencia acordada resulta
insuficiente para la supervisión o excesiva para la plataforma, o si la
consulta de estado pasa a requerir garantías de continuidad que una entrega sin
persistencia no puede ofrecer.
