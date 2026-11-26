# ADR-016 — Arranque, actualización y reversión de la plataforma externa

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Ámbito:** puesta en marcha automática de la instalación, procedimiento de actualización, identificación de la versión desplegada y vuelta atrás

## 1. Contexto

La plataforma externa queda instalada junto a la cafetera y debe volver a estar
operativa tras un corte de energía sin intervención. ADR-015 sitúa la plataforma
de contenedores en un disco externo, de modo que el arranque depende de que ese
disco esté disponible.

RNF-007 exige que cada resultado calculado declare las versiones que lo
generaron, lo que presupone poder saber qué versión está desplegada. El
procedimiento de actualización y respaldo ya documentado en la operación de la
aplicación establece construir sin caché, validar la composición, ejecutar el
recorrido de aceptación, conservar el volcado previo y aplicar migraciones solo
hacia adelante.

### 1.1 Situación comprobada

Comprobado el 2026-09-15 sobre el despliegue:

| Elemento | Situación |
|---|---|
| Política de reinicio de los servicios | «Reiniciar salvo detención deliberada», declarada en los seis servicios permanentes |
| Contenedor de migraciones | No se reinicia, como corresponde a una tarea de arranque |
| Identificación de la versión desplegada | **Ninguna**: las imágenes construidas se despliegan sin etiqueta de versión |
| Arranque de la instalación | No existe: depende de que alguien haya ejecutado la puesta en marcha previamente |
| Dependencia del disco externo | No declarada |

## 2. Problema de decisión

Se debe definir cómo vuelve la instalación a estar operativa tras un corte, cómo
se identifica lo que está desplegado, y cómo se actualiza y se revierte sin
comprometer la evidencia acumulada.

## 3. Alternativas consideradas

### 3.1 Arranque

| Alternativa | Evaluación |
|---|---|
| Confiar en la política de reinicio de los contenedores | Insuficiente: un servicio detenido a mano permanece detenido tras reiniciar el equipo, y nada garantiza que el disco esté montado antes de arrancar los servicios. |
| Unidad del sistema que declara el estado deseado y la dependencia del disco | Devuelve la instalación al estado declarado en cada arranque y no se ejecuta sin el disco disponible. **Seleccionada.** |
| Arranque manual tras cada corte | Obliga a intervenir para recuperar la supervisión y contradice la operación desatendida. |

### 3.2 Actualización

| Alternativa | Evaluación |
|---|---|
| Actualización automática de imágenes o del sistema | Cambiaría el equipo que sostiene la evidencia sin decisión ni respaldo previo, y podría hacerlo durante una campaña experimental. Descartada. |
| Actualización manual, deliberada y con respaldo previo | Mantiene el control sobre cuándo cambia la instalación y permite volver atrás. **Seleccionada.** |

### 3.3 Identificación de la versión

| Alternativa | Evaluación |
|---|---|
| Etiqueta única para todas las versiones | Situación actual: impide saber qué está desplegado y revertir con certeza. |
| Etiqueta con fecha y versión del código, conservando la anterior | Permite identificar lo desplegado y volver a la versión previa sin reconstruirla. **Seleccionada.** |

## 4. Decisión

1. **La instalación arranca mediante una unidad del sistema** que lleva los
   servicios al estado declarado en cada inicio y que **declara su dependencia
   del disco externo**: no se ejecuta si el disco no está montado.
2. **Las imágenes desplegadas se etiquetan con fecha y versión del código.** La
   etiqueta genérica deja de usarse para identificar lo desplegado.
3. **La versión anterior se conserva** hasta comprobar que la nueva opera
   correctamente, de modo que volver atrás no exija reconstruir.
4. **Toda actualización es manual y deliberada**, precedida de respaldo según
   ADR-015 y seguida del recorrido de aceptación. No se habilita ninguna forma de
   actualización automática, ni de las imágenes ni del sistema operativo.
5. **Las migraciones de base de datos se aplican solo hacia adelante.** Revertir
   la aplicación no revierte el esquema; una vuelta atrás que exija cambiar el
   esquema requiere una migración comprobada.
6. **No se actualiza durante una campaña experimental en curso.** Una serie de
   preparaciones comparables no puede atravesar un cambio de versión sin
   declararlo.

### Enmienda del 2026-10-30: la unidad no exige el disco externo

El punto 1 se escribió cuando los datos iban a residir en el disco externo. La
revisión de ADR-015 del 2026-09-15 los dejó en la tarjeta y convirtió el disco en
destino de copia. Exigir el disco ya no protege los datos: solo impediría arrancar
la plataforma cuando falle o se desconecte el disco de copias, que es lo contrario
de volver sola tras un corte. La unidad
([`espresso-plataforma.service`](../../../platform/web/systemd/espresso-plataforma.service))
arranca sin él y deja el aviso en el registro del sistema. Instalada en la Raspberry
el 2026-10-30. **§ 9.1 verificada el mismo día:** tras desenchufar la Raspberry, arrancó
a las 20:56:56, la unidad terminó a las 20:57:26 con los seis servicios operativos,
`/health/ready` en 200 y los 99 050 mensajes de la base. La § 9.2 pasa a ser
«arranque con el disco ausente: los servicios se inician y queda el aviso»,
**verificada el mismo día**: apagada con `systemctl poweroff` (la base cerró limpia, «database
system was shut down»), sin el disco arrancó con los seis servicios, `/health/ready` en 200 y el
aviso «Sin el disco de copias en /mnt/datos…» en el registro de la unidad.

## 5. Justificación

1. **Reiniciar contenedores no es arrancar la instalación.** La política actual
   conserva el último estado, incluida una detención accidental, en lugar de
   restablecer el estado deseado.
2. **El orden importa.** Con los datos en un disco externo, arrancar antes de
   montarlo produce servicios sin datos, que es peor que no arrancar.
3. **Revertir exige saber qué había antes.** Sin versión identificable, la vuelta
   atrás es una reconstrucción a ciegas.
4. **Una actualización automática es un cambio no decidido.** En un equipo que
   sostiene evidencia experimental, cualquier cambio debe tener fecha, motivo y
   respaldo previo.
5. **La comparabilidad de los datos depende de la estabilidad de la versión.** Un
   cambio a mitad de campaña introduce una variable que nadie declaró.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- La instalación vuelve sola tras un corte, con sus datos disponibles.
- Se puede saber en cualquier momento qué versión está operando.
- La vuelta atrás es inmediata mientras se conserve la versión anterior.
- Ningún cambio entra en la instalación sin decisión previa.

### 6.2 Consecuencias desfavorables

- Las actualizaciones de seguridad del sistema dejan de aplicarse solas y pasan a
  requerir una intervención planificada.
- Conservar la versión anterior ocupa espacio adicional, irrelevante frente a la
  capacidad disponible.
- La unidad de arranque es un elemento más que mantener y documentar.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Arranque sin el disco montado | La unidad declara la dependencia; se verifica con un corte de energía real. |
| Actualización fallida sin vuelta atrás | Respaldo previo, versión anterior conservada y recorrido de aceptación antes de dar por buena la nueva. |
| Equipo sin actualizaciones de seguridad | Mitigado por el aislamiento de red de ADR-014; revisión planificada fuera de campaña. |
| Cambio de versión a mitad de una serie experimental | Prohibido por § 4.6; si ocurre, debe declararse en los datos afectados. |
| Servicio detenido a mano que no vuelve | El arranque restablece el estado declarado. |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Unidad de arranque | **Cerrado el 2026-10-30:** escrita, instalada y verificada con un corte real y con el disco ausente (enmienda de § 4). |
| Etiquetado de imágenes | Incorporar fecha y versión del código al construir. |
| Lugar de construcción de las imágenes | Medir la construcción en la plataforma externa; si resulta impracticable, construir en el equipo de desarrollo y transferir. |
| Procedimiento de actualización documentado | Incorporar el etiquetado y la conservación de la versión anterior al procedimiento vigente. |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. corte de energía y arranque posterior con todos los servicios operativos y
   con sus datos;
2. arranque con el disco ausente: los servicios no se inician y la condición es
   evidente;
3. identificación de la versión desplegada desde el equipo;
4. actualización completa seguida de vuelta a la versión anterior;
5. servicio detenido manualmente que vuelve a estar operativo tras reiniciar.

## 10. Trazabilidad

- [ADR-014](ADR-014-exposicion-de-la-plataforma-en-la-red.md): aislamiento que
  atenúa el riesgo de no actualizar automáticamente.
- [ADR-015](ADR-015-almacenamiento-retencion-y-respaldo.md): disco externo y
  respaldo previo a cualquier actualización.
- Operación, diagnóstico y recuperación:
  procedimiento vigente de actualización y reversión.
- Requisitos: RNF-006 y RNF-007.

## 11. Criterios de reconsideración

La decisión deberá revisarse si la instalación pasa a requerir disponibilidad
continua, si el número de equipos crece hasta justificar una gestión
centralizada, o si la exposición de red cambia y las actualizaciones de seguridad
dejan de poder aplazarse.
