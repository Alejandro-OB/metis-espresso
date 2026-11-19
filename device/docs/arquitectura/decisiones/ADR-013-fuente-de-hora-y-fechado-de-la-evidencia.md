# ADR-013 — Fuente de hora y fechado de la evidencia

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Ámbito:** origen de la hora en la instalación, comportamiento del registro cuando no se dispone de ella y fechado de las preparaciones pendientes

## 1. Contexto

RNF-004 exige que cada medición indique cuándo se tomó, y RF-014 que cada
preparación tenga un registro único con sus instantes. La ESP32-S3 no dispone de
reloj con respaldo: al arrancar desconoce la fecha y debe obtenerla de la red.

RNF-001 establece que la adquisición, la regulación y la seguridad deben seguir
funcionando aunque fallen la red o los servicios externos. La cafetera, por
tanto, prepara café en situaciones en las que puede no conocer la hora.

La plataforma externa dispone normalmente de salida a Internet; la operación de
la cafetera no depende de ella. La ausencia de conectividad es un estado
transitorio previsto, no una condición permanente.

### 1.1 Situación comprobada

Comprobado el 2026-09-15 sobre el firmware:

| Elemento | Situación |
|---|---|
| Origen de la hora | Servidor público de Internet, fijado en la compilación |
| Comportamiento sin hora | El registro **no se serializa**: el firmware no inventa una fecha |
| Retención del registro sin fechar | **Una sola preparación**; la siguiente sobrescribe a la anterior |
| Base temporal disponible | El firmware conserva el tiempo transcurrido desde el arranque y la diferencia con la hora real una vez obtenida |
| Defensa frente a fechas falsas | Se rechaza cualquier instante anterior a 2020 |
| Fecha en la respuesta de sincronización | La plataforma ya la devuelve en cada respuesta; hoy se descarta |

El principio implementado es correcto —no se fecha lo que no se conoce—, pero su
tolerancia es de una preparación. Tras un corte de energía con la red aún no
disponible, un barista que prepare varios cafés seguidos conservará solo el
último.

## 2. Problema de decisión

Se debe definir de dónde obtiene la hora la instalación, qué hace la plataforma
cuando su propia hora no es fiable, y cómo se conservan las preparaciones
realizadas antes de disponer de una base temporal.

## 3. Alternativas consideradas

### 3.1 Origen de la hora para el dispositivo

| Alternativa | Evaluación |
|---|---|
| Servidor público de Internet | Situación actual. Funciona cuando hay conectividad y falla justo en los estados degradados que RNF-001 obliga a soportar. |
| La plataforma externa como fuente de hora | Está en la misma red, a distancia de un salto, y dispone de hora tanto si hay salida a Internet como si no. Una sola fuente que diagnosticar. **Seleccionada.** |
| Reloj con respaldo en la ESP32-S3 | Resolvería el caso de forma autónoma, pero añade componente y montaje al nodo crítico para un dato que la plataforma ya posee. |

### 3.2 Hora de la plataforma

| Alternativa | Evaluación |
|---|---|
| Sincronizar con Internet cuando esté disponible | Necesario, pero insuficiente en el arranque posterior a un corte, cuando la red aún no está establecida y el dispositivo ya pregunta. |
| Añadir respaldo de reloj con batería | Elimina esa ventana y mantiene la hora sin conectividad. Coste reducido. **Seleccionada, junto con la anterior.** |
| Servir la hora del sistema sin comprobar su validez | Propagaría una fecha falsa a la evidencia sin que nadie lo advierta. Descartada. |

### 3.3 Preparaciones realizadas sin hora

| Alternativa | Evaluación |
|---|---|
| Fecharlas con una hora aproximada | Contradice el principio ya implementado y contamina la evidencia. |
| Retener solo la última, como hoy | Pierde preparaciones en el escenario más probable: varios cafés tras un arranque sin red. |
| Retener cada preparación con su instante interno y fecharlas al conocer la diferencia | Reconstruye la fecha real por resta, sin inventar nada. **Seleccionada.** |

## 4. Decisión

1. **La plataforma externa es la fuente de hora de la instalación.** El
   dispositivo solicitará la hora a la plataforma y no a un servidor externo.
2. **La plataforma mantendrá su hora mediante sincronización con Internet cuando
   disponga de ella y respaldo de reloj con batería.**
3. **La plataforma no servirá una hora que no considere fiable.** Ante un reloj
   sin validar, declarará que no dispone de hora en lugar de entregar una.
4. **El dispositivo aprovechará la fecha que ya recibe** en la respuesta de
   sincronización como origen alternativo, sin añadir servicios ni protocolos.
5. **Las preparaciones realizadas sin hora se conservarán con su instante
   interno** y se fecharán cuando se conozca la diferencia con la hora real. No
   se limita a una.

Se mantiene sin cambios el principio vigente: ninguna preparación se registra
con una fecha inventada, y la falta de hora nunca impide preparar café.

## 5. Justificación

1. **El estado que hay que proteger es el transitorio.** La cafetera debe operar
   con la red caída, según RNF-001; es precisamente entonces cuando hoy se pierde
   evidencia.
2. **Una fecha falsa es peor que una fecha ausente.** Un registro mal fechado
   parece válido y nadie lo revisa; uno ausente se detecta.
3. **La resta es exacta y no inventa.** El firmware ya calcula la diferencia
   entre su tiempo interno y la hora real; aplicarla a preparaciones anteriores
   produce su instante verdadero.
4. **La fuente más cercana es la más diagnosticable.** Si la hora falla, hay un
   único sitio donde mirar, dentro de la instalación.
5. **El respaldo aprovecha lo que ya circula.** La fecha de la respuesta de
   sincronización llega en cada intercambio y hoy se descarta.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- La evidencia deja de perderse por preparar café antes de conocer la hora.
- La instalación conserva una base temporal coherente con y sin conectividad.
- El diagnóstico de un problema de hora se concentra en la plataforma.

### 6.2 Consecuencias desfavorables

- El firmware debe conservar varias preparaciones pendientes de fechar y sellarlas
  posteriormente, lo que hoy no hace.
- La plataforma asume una responsabilidad adicional y debe declarar la fiabilidad
  de su propio reloj.
- El respaldo de reloj con batería es un componente más que mantener.

## 7. Límite declarado

Si el dispositivo se reinicia antes de obtener la hora, su tiempo interno vuelve
a cero y las preparaciones retenidas **no pueden fecharse**. Esos registros se
pierden de forma irrecuperable y debe informarse la condición. No existe forma de
reconstruir su instante sin inventarlo, y no se hará.

## 8. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Plataforma con hora incorrecta que se propaga a la evidencia | La plataforma declara la fiabilidad de su reloj y se abstiene si no la tiene; el dispositivo conserva el rechazo de instantes anteriores a 2020. |
| Acumulación de preparaciones sin fechar | Informar la condición en el estado del dispositivo y acotar la retención. |
| Deriva del reloj interno durante la espera | La corrección se aplica con la diferencia medida en el momento de obtener la hora; la deriva en minutos u horas es despreciable frente a la resolución del registro. |
| Reinicio con registros sin fechar | Límite declarado en § 7; se informa como pérdida, no se sustituye por una estimación. |

## 9. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Servicio de hora en la plataforma | Configurarlo, con respaldo de batería, y declarar la fiabilidad del reloj. |
| ~~Retención múltiple y sellado posterior en el firmware~~ | **Implementado el 2026-09-15**; véase § 13. |
| Origen alternativo desde la respuesta de sincronización | Utilizar la fecha ya recibida. |
| Señalización de la condición | Mostrar en el estado del dispositivo cuántas preparaciones esperan fecha. |

## 10. Verificación de la decisión

Se verificará, como mínimo:

1. arranque de la instalación sin conectividad exterior y obtención de hora desde
   la plataforma;
2. varias preparaciones consecutivas antes de disponer de hora, todas fechadas
   correctamente al obtenerla;
3. plataforma sin hora fiable: no entrega hora y el dispositivo no fecha;
4. reinicio del dispositivo con registros pendientes: se informa la pérdida sin
   sustituir la fecha;
5. coincidencia entre la fecha reconstruida y la real dentro de la resolución
   del registro.

## 11. Trazabilidad

- [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md) § 6: contrato e
  instantes de los mensajes.
- [ADR-002](ADR-002-persistencia-interna-y-almacenamiento-externo.md): cola local
  y condiciones de pérdida de evidencia.
- [ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md): enlace
  por el que circulan los registros y la fecha de respuesta.
- Requisitos: RF-014, RF-015, RNF-001, RNF-004 y RNF-006.

## 12. Criterios de reconsideración

La decisión deberá revisarse si el dispositivo incorpora reloj con respaldo
propio, si la instalación pasa a operar de forma permanente sin plataforma
externa, o si la precisión exigida a los instantes supera la que puede ofrecer
la reconstrucción por diferencia.

## 13. Estado de implementación

**Retención múltiple y fechado hacia atrás — implementado el 2026-09-15.**

El registrador de extracciones conserva ahora varios registros cerrados a la
espera de hora, en lugar de uno solo:

| Aspecto | Resolución |
|---|---|
| Capacidad | Alineada con la cola persistente: no se retiene más de lo que la cola podrá aceptar, porque hacerlo solo aplazaría la pérdida |
| Política al llenarse | Se descarta el registro más reciente y se conservan los que ya esperaban, igual que hace la cola; el descarte se cuenta y se informa en el estado |
| Fechado | Cada registro se sella con la hora correspondiente a **su propio instante de cierre**, aplicando el desfase medido entre el reloj monótono y la hora real |
| Estado expuesto | Número de registros en espera y número de registros perdidos por capacidad |

El cambio corrige además una imprecisión anterior: el registro se fechaba con el
instante de serialización y no con el de finalización de la preparación. Con un
registro retenido durante minutos u horas, esa diferencia dejaba de ser
despreciable.

Verificación realizada: compilación completa del firmware para ESP32-S3 y
ejecución de las cinco baterías de pruebas host del dispositivo, todas
correctas. El escenario de varias preparaciones antes de obtener la hora debe
comprobarse sobre el equipo, según § 10.

El límite declarado en § 7 se mantiene sin cambios: un reinicio previo a la
obtención de la hora sigue impidiendo fechar los registros retenidos.

**La hora la entrega la plataforma — implementado y verificado el 2026-09-15.**

Hasta esta fecha el firmware consultaba un servidor de hora de Internet, que es
lo que § 4.1 descarta: la decisión es que la fuente de hora de la instalación
sea la plataforma. El dato ya viajaba en la respuesta de sincronización y se
descartaba, exactamente como anticipaba § 5.5.

La comprobación con el dispositivo real añadió un motivo que la decisión no
había previsto. El servicio de hora externo no se cerraba tras un intento sin
respuesta, de modo que quedaba a medio iniciar y ningún reintento posterior
podía arrancarlo: **un único fallo inicial dejaba al dispositivo sin poder
fechar nada hasta reiniciarlo**, con todas las preparaciones de esa sesión
retenidas. Se observó en la placa antes de corregirlo. La dependencia
desaparece con el cambio, y con ella ese modo de fallo.

| Aspecto | Resolución |
|---|---|
| Petición | La sincronización de siempre, sin mensajes que entregar; no añade servicio ni protocolo (§ 4.4) |
| Lectura | En módulo puro, verificada en el anfitrión |
| Formato admitido | Únicamente UTC terminada en `Z` |
| Ante una hora ilegible | Se rechaza y el dispositivo sigue sin hora; interpretarla desplazaría en silencio la fecha de toda la evidencia (§ 4.3) |

Verificación sobre la placa: hora obtenida de la plataforma a los nueve segundos
del arranque, y la preparación siguiente archivada con su instante real. El
detalle está en la
validación del enlace entre el dispositivo y la plataforma
§ 4.3.

**Pendiente del lado de la plataforma.** El punto 3 de § 4 exige que la
plataforma no sirva una hora que no considere fiable, y que declare no disponer
de ella antes que entregar una dudosa. Hoy la plataforma responde siempre con su
hora, sin calificarla. El dispositivo ya trata la ausencia como caso normal, de
modo que el cambio no le afecta.
