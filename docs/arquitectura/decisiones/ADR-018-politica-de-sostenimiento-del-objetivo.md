# ADR-018 — Política de sostenimiento del objetivo aprobado

**Estado:** Aceptada el 2026-09-30.

## 1. Contexto

[ADR-017](ADR-017-activacion-de-la-configuracion-efectiva.md) define tres vías
de activación, cada una con una decisión humana registrada. El tramo virtual →
físico depende así de la autorización del barista, y ese tramo es lo que separa
una sombra digital de un gemelo digital.

La frontera de autoridad se sostiene en que la máquina no puede probar el café.
Ese argumento le impide decidir a qué debe saber la bebida, pero no le impide
cumplir lo que el barista ya decidió. Volver a los 91 °C fijados cuando la
cocina está más fría no cambia la receta: la sostiene.

La política que acota esa corrección es única para todo el sistema y la fijan
los autores. El barista ya decidió al activar el objetivo, y aprobar además la
política sería un trámite sin contenido, como lo sería aprobar que un control
de crucero mantenga la velocidad en una subida.

## 2. Alternativas consideradas

| Alternativa | Resultado |
|---|---|
| Aprobación caso por caso | Descartada: deja el sistema como sombra digital y pierde consistencia entre preparaciones de un mismo objetivo. |
| Corrección automática sin política | Descartada: nada acota la corrección ni explica por qué rigió cada referencia. |
| Corrección de cualquier propiedad, incluido el objetivo | Descartada: desplaza al barista en lo único que el sistema no puede juzgar. |
| Política aprobada por el barista para cada objetivo | Descartada: con límites fijados por los autores, su aprobación no añade nada. |
| Política única de los autores que sostiene el objetivo activado | **Seleccionada.** |

## 3. Qué rige

La corrección después de cada café (§§ 4.5–4.7) se retiró el 2026-10-22
(§ 13): en la máquina real no podía corregir nunca. El sostenimiento consiste
en la compensación del primer uso tras un reposo (§§ 12, 14 y 15), la exclusión
del barista y la suspensión del protocolo (§ 4.3). Siguen vigentes §§ 4.1, 4.3 y
4.4.

## 4. Decisión

### 4.1 Objetivo y referencia son cosas distintas

El **objetivo** es lo que fija el barista: la temperatura del agua sobre el
café, la presión de extracción y el tiempo máximo. La **referencia** es el valor
que regula el dispositivo para alcanzarlo, por ejemplo la temperatura del cuerpo
de la caldera. Ambos coinciden solo si la máquina no tiene pérdidas. Sostener el
objetivo es mover la referencia para que el agua siga llegando al objetivo.
**El sostenimiento cambia la referencia, nunca el objetivo.**

No es el PI con otro nombre. El PI mantiene la caldera en su referencia durante
la extracción, en el dispositivo. El sostenimiento mueve esa referencia entre
preparaciones, en la plataforma, y para eso necesita el modelo, que no entra en
el lazo de control.

### 4.3 Qué decide el barista

El barista activa el objetivo por una de las vías de ADR-017, y de ese acto
cuelga toda corrección posterior. Puede además **excluir una configuración** del
sostenimiento, con un acto registrado que rige hasta que la retire. El
protocolo experimental puede **suspender** el sostenimiento mientras una serie
comparable esté abierta (ADR-017 § 11), y la suspensión también se registra.

### 4.4 Qué no hace nunca

- cambiar el objetivo ni seleccionar otra configuración;
- corregir molienda, dosis o proporción (ADR-017 § 4.6);
- iniciar, detener o prolongar una preparación (ADR-005 § 4.2);
- aplicar una corrección durante una preparación (ADR-017 § 4.4);
- eludir la validación local ni el guardián de actuación.

### 4.5 Cuarta vía de activación (retirada por § 13)

La corrección después del café entraba por una cuarta vía, añadida a las tres de
ADR-017 § 4.1: una configuración derivada dentro de la política, validada por el
dispositivo como las demás, cuya procedencia declaraba la activación de origen,
la versión de la política, la preparación que la motivó y la versión del modelo.

### 4.6 Cuándo no corregía (retirada por § 13)

El sistema no corregía, y pasaba a recomendar, si:

1. la corrección superaba la corrección máxima de la política;
2. la desviación persistía tras dos correcciones seguidas;
3. el modelo estaba fuera de su tolerancia (`outside-model-tolerance` o
   `insufficient-coverage` en la comparación);
4. la medida anterior no era apta, o la temperatura del agua no estaba
   disponible (ADR-019);
5. había un episodio de ajuste abierto, para que una diferencia en la bebida
   pudiera atribuirse al ajuste del barista y no a la máquina (ADR-004 § 12.4);
6. la configuración estaba excluida o el sostenimiento suspendido.

### 4.7 Política y umbrales (retirados por § 13)

La política era versionada y no se modificaba después de haber regido. La
corrección máxima de temperatura era 2,8 °C: la diferencia media que producen en
el nodo caliente del modelo de la caldera los extremos climatológicos de Popayán
(12,5 y 26,4 °C, normales 1991-2020 del IDEAM) en una extracción a 450 s de
encender en frío. Era provisional, porque ese nodo nunca se midió. La presión no
se corregía.

## 5. Razones

1. El sistema no decide a qué sabe la bebida; solo sostiene lo que el barista
   activó.
2. Toda referencia vigente cuelga de una activación del barista, que es la
   decisión explícita que exige ADR-004 § 4.11.
3. Nada entra en el lazo de control: la corrección llega entre preparaciones
   como una referencia más.
4. Con el sostenimiento activo, el flujo virtual → físico es automático en su
   ámbito, que es lo que distingue al gemelo de la sombra digital.

Corregir es lo predeterminado porque aquí sí hubo decisión: frente a un objetivo
activado, lo conservador es mantenerlo dentro de límites. Sin objetivo activado
no hay nada que sostener.

## 6. Consecuencias

- La plataforma cambia lo que pide a la placa sin una aceptación por cada
  cambio; cada decisión se guarda con su motivo para reconstruir por qué rigió.
- Los límites son de los autores y se justifican por medida o se declaran
  provisionales.
- La calidad de la compensación depende del modelo y de lo medido; en uso normal
  no hay sensor de agua que la compruebe.

## 9. Verificación de la decisión

Se verifica que: sin objetivo activado no se pide nada; con objetivo activado,
la compensación rige en la preparación siguiente; una compensación por encima
del máximo no se aplica; una configuración excluida o un sostenimiento
suspendido no reciben nada; cada decisión declara su origen, su regla y su
motivo; y nada cambia durante una preparación ni modifica el tiempo máximo.
Desde § 13 estas comprobaciones se aplican a la compensación del primer uso.

## 12. Enmienda del 2026-10-22: subida de caldera del primer uso

### 12.1 Por qué la corrección después del café no corrige

El agua se estima desde la caldera con la misma recta que usa la placa para
fijarla ([ADR-019](ADR-019-temperatura-del-agua-por-transformacion-medida.md)).
Con la caldera en su referencia, el agua estimada da el objetivo por
construcción. El café que sí sale frío es el primero tras un reposo, de 1,8 a
3,9 °C por debajo de los siguientes, con la caldera marcando bien
([evidencia](../../engineering/evidencias/temperatura-cierre-2026-10-17.md),
«Ensayo de reposo»). Una corrección posterior llegaría al segundo café, que ya
sale bien.

### 12.2 Decisión

La plataforma calcula, con el reposo que muestra la telemetría, cuánto subir la
caldera para el próximo uso. La placa lo aplica con `boilerOffsetC`
([ADR-009](ADR-009-control-local-temperatura-pi-ciclo-integral.md), enmienda del
2026-10-19) solo en la preparación siguiente.

- No usa la cuarta vía: la subida viaja con la configuración vigente en la
  respuesta de la sincronización, para no llenar el catálogo de versiones que
  nadie eligió. Cada cambio queda en un documento `first-use-decision`.
- La acota el máximo del
  [acta de temperatura v7](../../engineering/actas/acta-temperatura-20261019.json)
  (4,0 °C).
- Con el sostenimiento suspendido o la configuración excluida no pide nada; si
  el perfil declara una subida a mano, manda la del perfil.
- Sigue con un episodio de ajuste abierto: la subida sale de lo medido, y
  apagarla haría salir frío el primer café tras el ajuste, frío que se
  atribuiría al ajuste.
- Con una preparación o un purgado en curso mantiene lo último pedido.
- Se pide desde el encendido.
- El reposo va del último uso visto (`finishedAt`, o las muestras en
  `manual-purge`) a la última muestra recibida. Con un hueco de más de 60 s en
  la telemetría no pide nada, porque pudo haber un uso no visto.

### 12.3 La regla `primer-uso-v1`

| Reposo | Subida de caldera | Origen (medido el 2026-10-19) |
|---|---|---|
| hasta 10 min | 0 | tras 10 min el primer uso no sale frío |
| 20 min | 2,7 °C | faltan 1,8 °C de agua, a 0,66 °C de agua por °C de caldera |
| 30 min | 3,7 °C | con +2,9 °C faltaron 0,70 °C y con +4,0 °C sobraron 0,20 °C; interpolado |

Entre puntos, recta; el reposo se redondea hacia abajo a tramos de 5 min, para
que, si yerra, se quede corta. Más allá de 30 min se queda en 3,7 °C
(`beyond-measured`): con los déficits medidos a 40 min y a 3 h 13 min el café
saldría unos 0,5 °C corto, nunca de más. Se prefirió una regla medida al modelo
del agua tras un reposo, que en los reposos de 30 min dejó el primer uso
0,7–0,8 °C corto.

### 12.4 Qué queda fuera

No comprueba en cada café que el agua salió bien, porque en uso normal no hay
sensor de agua. No compensa los cafés siguientes al primero.

## 13. Enmienda del 2026-10-22: se retira la corrección después del café

La corrección de §§ 4.5–4.7 se retira: por construcción no podía corregir
(§ 12.1), y las 224 decisiones que tomó entre el 2026-10-09 y el 2026-10-22
fueron todas «no corrige». Se quitan `espresso_sustainment.decision` y
`.policy`, la decisión `sustainment-decision`, la política v1, la cuarta vía y
sus umbrales. Los documentos ya guardados se conservan como registro. Quedan
§§ 4.1, 4.3 y 4.4, y § 9 se verifica sobre la subida.

## 14. Enmienda del 2026-10-27: la subida la decide el modelo

La subida la decide el gemelo, y `primer-uso-v1` queda de respaldo. Con la regla
`primer-uso-pared-v1` (`espresso_sustainment.first_use`), la plataforma simula
la pared del recorrido por el grupo y el portafiltro
(`espresso_simulation.water_after_rest`) y compara la de ahora con la media al
pulsar los tres últimos cafés. El déficit es `D` = −1,243 − 0,390 · x °C, y la
subida es `D` / 0,66, redondeada hacia abajo a 0,5 °C y con el tope de 4,0 °C.
Los coeficientes salen de los cinco reposos del 19 de octubre.

Se eligió entre trece formas, cada una con su criterio fijado antes. Predecir
los cafés futuros no cumple, porque la caldera de esos cafés no se conoce antes
de pulsar. La pared relativa dio 0,35 °C de error frente a 0,41 °C de la regla en
los reposos del 19, y 0,81 frente a 0,88 °C en los reposos ciegos del 18.

Tras un arranque en frío (primera lectura bajo 50 °C) la pared se compara con
75,0 °C, la típica de los cafés recuperados. La regla decide si la plataforma no
puede calcular la pared: un hueco de más de 60 s con la máquina caliente, o
menos de tres cafés en las últimas 2 h sin arranque en frío. Una vez pedida, la
subida no baja hasta el café siguiente, porque calienta la pared y el modelo
pediría menos justo antes de ese café.

## 15. Enmienda del 2026-10-27: refuerzo de la prealimentación

El déficit del primer café está en la segunda mitad del chorro, adonde la
subida previa no llega. Con más prealimentación durante ese café
(ADR-009, enmienda del 2026-10-27), el déficit baja de 3,25 a 0,06 °C de media en
reposos de 25 min. La regla `primer-uso-pared-v2` pedía
`firstUseFeedforwardGain` = 1,25 cuando el déficit estimado llegaba a 2,0 °C, y
la subida solo tras un arranque en frío.

La validación de la v2 no cumplió: el refuerzo solo sube el primer café unos
2,3 °C. Desde el 2026-10-28 rige `primer-uso-pared-v3`, que pide en el mismo café
el refuerzo y la subida de § 14. Tras 25 min deja el primer café 1,21 y 1,14 °C
bajo los siguientes, sin cumplir el límite de 1,0 °C, y mejora 4,9 °C el
arranque en frío. Los autores lo adoptaron sin más ajustes. El refuerzo no se
gradúa: es el único valor medido.
