# ADR-017 — Qué activa la configuración efectiva de la cafetera

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Ámbito:** vías por las que una configuración llega a regir en el dispositivo, procedencia de cada una y precedencia entre ellas

## 1. Contexto

ADR-001 § 6, ADR-003 § 6 y ADR-004 § 4.11 establecen la misma cadena: la
plataforma propone, el barista decide y el dispositivo valida. RD-002 permite a
la aplicación **proponer configuraciones válidas** y le prohíbe iniciar
preparaciones o escribir sobre los actuadores.

Esa cadena estaba descrita en un solo escenario: una recomendación que el
barista acepta. El marco conceptual la representa así en su § 5, y el tramo de
vuelta figuraba como el eslabón ausente del ciclo.

### 1.1 Situación comprobada

| Elemento | Situación |
|---|---|
| Transporte de la configuración al dispositivo | Implementado y verificado ([ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md)) |
| Validación local antes de admitirla | Implementada, con once causas tipificadas de rechazo |
| Criterio de bajada | La plataforma envía las configuraciones marcadas como aceptadas, sea cual sea su origen |
| Procedencias de recomendación activas | **Ninguna**: el aprendizaje se abstiene por falta de modelo aprobado y el razonador no está integrado ([ADR-004 § 11.4](ADR-004-distribucion-datos-ontologia-aprendizaje.md)) |

De aquí se sigue el problema. Si la única vía prevista fuera la recomendación,
**el ciclo no podría recorrerse entero ni una sola vez** mientras ninguna
procedencia esté activa: existiría completo sobre el papel y sin ejercitar. Y
además no describiría el uso corriente, en el que un barista elige el perfil que
corresponde al café que va a preparar, sin que nadie le sugiera nada.

## 2. Problema de decisión

Se debe definir qué actos pueden hacer que una configuración pase a regir en el
dispositivo, cómo se distingue su procedencia y cuál prevalece cuando concurren.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Solo una recomendación aceptada | Fiel al escenario del marco conceptual, pero deja el ciclo inejercitable mientras no exista una procedencia de recomendación, y no cubre el uso diario del barista. |
| Cualquier configuración marcada como efectiva, sin decisión registrada | Permitiría que una configuración llegara a la máquina sin que conste quién lo decidió, rompiendo la cadena de autoridad que exige ADR-004 § 4.11. |
| Tres vías, todas con decisión humana explícita y procedencia declarada | Cubre el uso corriente, permite ejercitar el ciclo desde el primer día y conserva la trazabilidad. **Seleccionada.** |

## 4. Decisión

### 4.1 Vías de activación

Una configuración pasa a regir por una de estas tres vías, y por ninguna otra:

| Vía | Acto | Cuándo se usa |
|---|---|---|
| **Selección** | El barista elige una configuración ya existente | Uso corriente: un perfil por café o por tueste |
| **Edición** | El barista crea o modifica una configuración y la da por buena | Cuando decide cambiar una referencia por criterio propio |
| **Recomendación aceptada** | El barista acepta o modifica una recomendación del sistema | Cuando el sistema tiene algo que proponer |

Las tres producen el mismo resultado: una configuración marcada como aceptada,
que baja por el canal ya existente. El dispositivo **no distingue la vía**: la
valida siempre igual y puede rechazarla con causa. Ninguna de ellas es una
orden, y ninguna inicia una preparación: eso sigue siendo del mando físico.

### 4.2 Procedencia

Toda configuración aceptada declara de dónde salió: la vía, el autor y la fecha
y, cuando proceda de una recomendación, su identificador y el de la decisión que
la aprobó.

Es lo que convierte tres vías en flexibilidad y no en desorden. Sin ese dato,
una configuración vigente sería indistinguible de otra y no podría reconstruirse
por qué la máquina estaba ajustada así.

### 4.3 Precedencia

Rige la última configuración aceptada. El historial conserva el orden y el
instante de cada aceptación, de modo que siempre puede determinarse cuál estaba
vigente en una preparación concreta.

### 4.4 Momento de entrada en vigor

Una configuración aceptada empieza a regir **en cuanto la máquina no esté
preparando**. Si hay una preparación en curso, espera a que termine; si no la
hay, rige de inmediato.

No entra en mitad de una preparación porque el resumen guarda la configuración
con la que se abrió la sesión: cambiarla dejaría un registro que declara unas
referencias mientras el lazo siguió otras, evidencia que se contradice a sí
misma y que ninguna otra regla de esta decisión impediría.

Tampoco se exige que la máquina esté apagada. Protegería un poco más y estorbaría
mucho: convertiría cada ajuste en un ritual de apagado y encendido, y un barista
que no puede aplicar un cambio cuando lo decide termina no usando el sistema.

La espera no obliga al dispositivo a retener nada: la plataforma ofrece la
configuración vigente en cada intercambio, de modo que la que no entró ahora
entra en el siguiente. El dispositivo tampoco reactiva la que ya rige, que se
reconoce por identificador y versión.

**Lo que esto implica para el barista.** Un cambio aceptado durante una
preparación se aplica al café siguiente, no al que está en la taza. La interfaz
debe decirlo, porque la alternativa es que lo deduzca de un resultado que no
esperaba.

### 4.5 Una configuración incompleta no se activa

La plataforma comprueba, **en el momento de activar**, que la configuración trae
lo que el dispositivo necesita para gobernar la máquina: referencia de
temperatura, referencia de presión y tiempo máximo. Si falta alguna, la
activación se rechaza con la causa y nombrando lo que falta.

No es una comprobación redundante con la del dispositivo, que sigue siendo la
autoridad y conserva su derecho de veto (§ 4.1). Es una comprobación en otro
sitio y con otro fin: **el único momento en que el barista puede corregirlo es
mientras está activando**. Dejar pasar una configuración que la máquina va a
rechazar no es activar nada; es crear la apariencia de haberlo hecho.

Se decidió tras observarlo: durante la validación del 2026-09-15 se activó una
configuración sin tiempo máximo, la plataforma la dio por buena, el dispositivo
la rechazó con causa y el rechazo apareció únicamente en la consola de
diagnóstico, que en la instalación definitiva nadie mira.

La comprobación es de completitud, no de límites. Si los valores caben en el
contrato pero la máquina no puede sostenerlos, eso lo dice el dispositivo y solo
él, porque conoce sus propios límites y la plataforma no.

### 4.6 Orden de implementación

1. **Selección**, porque permite recorrer el ciclo completo desde el primer día
   sin depender de piezas ausentes.
2. **Edición**.
3. **Recomendación aceptada**, cuando exista alguna procedencia activa. Para los
   ajustes registrados tras una preparación, solo derivan configuración los que
   afectan a una propiedad que el dispositivo gobierna —temperatura, presión y
   tiempo máximo—: la molienda, la dosis y la proporción las ejecuta el barista
   y fabricar una configuración para ellas anotaría un cambio que la máquina no
   puede hacer.

## 5. Justificación

1. **Un ciclo que no puede recorrerse no puede demostrarse.** Con la
   recomendación como única vía, el tramo de vuelta quedaría construido y sin
   ejercitar hasta que existiera un modelo aprobado o un razonador integrado.
2. **El uso corriente no es recibir sugerencias.** Un barista elige el perfil
   que corresponde al café que tiene delante; describir solo el caso asistido
   dejaría fuera lo que ocurre todos los días.
3. **La autoridad se conserva en las tres.** En todas hay una decisión humana
   explícita y registrada, y en todas el dispositivo mantiene su derecho de veto.
4. **La procedencia hace innecesario restringir las vías.** Lo que debe ser
   trazable es de dónde vino cada configuración, no cuántas formas existen de
   proponerla.
5. **Ampliar después es más difícil que empezar abierto y declarado.** Una vía
   añadida más tarde sin procedencia rompería la trazabilidad de todo lo
   anterior.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- El ciclo completo puede verificarse de extremo a extremo sin esperar al
  aprendizaje ni al razonador.
- El sistema describe el trabajo real del barista y no solo el caso asistido.
- Cuando una procedencia de recomendación se active, entrará por un camino ya
  probado.
- Cada preparación puede explicar con qué configuración se hizo y por qué regía.

### 6.2 Consecuencias desfavorables

- La plataforma pasa a **generar** configuraciones y no solo a transportarlas.
- Con varias vías, un cambio de configuración a mitad de una serie experimental
  es más fácil, y compromete la comparabilidad si no se declara.
- Cada vía debe registrar su procedencia, lo que añade información a conservar.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Configuración cambiada a mitad de una serie comparable | Cada preparación conserva la configuración vigente y su procedencia; una serie que atraviese un cambio queda identificable en lugar de pasar inadvertida. |
| Dos aceptaciones concurrentes | Rige la última; el orden y el instante quedan registrados. |
| Confundir una configuración elegida con una recomendada | La procedencia distingue la vía de forma explícita. |
| Interpretar la activación como una orden | Sin cambios: el dispositivo valida y puede rechazar con causa, y la preparación solo empieza por el mando físico. |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| ~~Vía de selección~~ | **Implementada y verificada el 2026-09-15** en la plataforma y sobre el dispositivo real: la configuración elegida bajó, fue aceptada y gobernó la preparación siguiente. |
| ~~Vía de edición~~ | **Implementada el 2026-09-15**: la activación declara su vía y la de recomendación se comprueba contra una decisión registrada. |
| ~~Derivación desde un ajuste aceptado~~ | **Implementada el 2026-09-15**: un ajuste sobre temperatura, presión o tiempo máximo, con recomendación aceptada detrás, produce la configuración derivada y la deja rigiendo. Los ajustes de molienda, dosis y proporción no derivan nada. |
| Registro del ciclo completo | Conservar quién propuso, quién aprobó, qué validó el dispositivo y desde cuándo rige. |
| ~~Momento de entrada en vigor~~ | **Decidido el 2026-09-15** (§ 4.4): rige en cuanto la máquina no esté preparando. Queda que la interfaz lo diga, porque un cambio aceptado durante una preparación se aplica al café siguiente. |
| ~~Activación de una configuración incompleta~~ | **Decidido el 2026-09-15** (§ 4.5): la plataforma rechaza al activar la configuración a la que le falte temperatura, presión o tiempo máximo, nombrando lo que falta. |

### 8.1 Cuándo empieza a regir una configuración aceptada — resuelto en § 4.4

Esta decisión fija quién puede activar una configuración y por qué vías, pero no
**cuándo** empieza a regir, y la comprobación con el dispositivo real mostró que
la omisión tiene consecuencias.

Una configuración que entrase en mitad de una preparación dejaría un registro
que declara unas referencias mientras el lazo siguió otras, porque el resumen
guarda la configuración con la que se abrió la sesión. Sería evidencia que se
contradice a sí misma, y contra eso no protege ninguna de las reglas de § 4.

El firmware ya aplicaba la lectura conservadora —no activar durante una
preparación— y esa es la que quedó decidida en § 4.4, por ser la única de las
tres que protege la evidencia sin convertir cada ajuste en un ritual de apagado
y encendido.

### 8.2 Una configuración incompleta se activa sin que nadie se entere — resuelto en § 4.5

Durante la validación del 2026-09-15 se activó una configuración que no
declaraba tiempo máximo de extracción. Ambas partes se comportaron como debían:
la plataforma no inventó el dato ausente y el dispositivo rechazó una
configuración que no basta para gobernar la máquina.

Pero el barista no se enteró de nada. La plataforma dio la activación por buena
y el rechazo apareció solo en la consola del dispositivo, que en la instalación
definitiva nadie mira. De ahí § 4.5: la comprobación de completitud ocurre donde
se activa y se dice en ese momento, que es el único en que el barista puede
corregirlo.

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. una configuración elegida en la aplicación llega al dispositivo, se valida y
   rige en la preparación siguiente;
2. una configuración fuera de los límites de la máquina se rechaza con causa y
   no llega a regir;
3. la preparación resultante declara qué configuración usó y por qué vía se
   activó;
4. dos aceptaciones sucesivas dejan vigente la última, con el orden registrado;
5. la activación no inicia ninguna preparación.

### 9.1 Verificación realizada el 2026-09-15

El punto 1 queda verificado sobre el dispositivo real, y de la forma más
concluyente posible: no solo se comprobó que la configuración llegó, sino que
mandó. Se activó una configuración con un tiempo máximo de 27 s frente a los
30 s de la que regía, y la preparación siguiente terminó a los 26 998 ms por
límite de tiempo alcanzado. Tras reiniciar, el dispositivo recuperó esa misma
configuración y declaró cuál regía.

El punto 2 queda verificado de forma parcial y no buscada: una configuración sin
tiempo máximo fue rechazada por el dispositivo, aunque la causa no llegó a
nadie (§ 8.2). Los puntos 3, 4 y 5 siguen pendientes.

El registro completo está en la
validación del enlace entre el dispositivo y la plataforma
§ 3 y § 5.

## 10. Trazabilidad

- [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md) § 6 y
  [ADR-003](ADR-003-interfaz-web-unica-en-plataforma-externa.md) § 6: autoridad y
  frontera de comunicación.
- [ADR-004](ADR-004-distribucion-datos-ontologia-aprendizaje.md) § 4.11 y § 11.4:
  frontera de autoridad y procedencias de recomendación.
- [ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md): canal,
  contrato de la respuesta y validación local.
- Marco conceptual del gemelo digital § 5 y § 6.
- Requisitos: RF-001, RF-029, RD-002 y RD-004.

## 11. Criterios de reconsideración

La decisión deberá revisarse si aparece una vía de activación que no pueda
declarar su procedencia, si el número de configuraciones vigentes deja de ser
determinable en un instante dado, o si el protocolo experimental exige congelar
la configuración durante una serie, en cuyo caso la activación tendría que poder
bloquearse mientras la serie esté abierta.
