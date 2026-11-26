# ADR-016 — Arranque, actualización y reversión de la plataforma externa

**Estado:** Aceptada el 2026-09-21.

## 1. Contexto

La plataforma externa queda instalada junto a la cafetera y debe volver a operar
sola tras un corte de energía. Cada resultado debe declarar las
versiones que lo generaron, y eso exige saber qué versión está desplegada. Los
servicios tenían la política de reinicio de los contenedores, pero ninguna
etiqueta de versión ni una unidad de arranque de la instalación.

## 2. Alternativas consideradas

- **Confiar en la política de reinicio de los contenedores.** Descartada: un
  servicio detenido a mano sigue detenido tras reiniciar el equipo.
- **Arrancar a mano tras cada corte.** Descartada: contradice la operación
  desatendida.
- **Actualizar automáticamente imágenes o sistema operativo.** Descartada:
  cambiaría el equipo que guarda la evidencia sin decisión ni respaldo, quizá a
  mitad de una campaña.
- **Una sola etiqueta para todas las versiones.** Descartada: impide saber qué
  está desplegado y revertir con certeza.

## 3. Razones

- Reiniciar contenedores conserva el último estado, incluida una detención
  accidental; la unidad restablece el estado deseado.
- Sin versión identificable, revertir es reconstruir a ciegas.
- En un equipo que guarda evidencia experimental, todo cambio necesita fecha,
  motivo y respaldo previo.
- Un cambio de versión a mitad de campaña introduce una variable no declarada.

## 4. Decisión

1. **La instalación arranca con una unidad del sistema** que lleva los
   servicios al estado declarado en cada inicio
   ([`espresso-plataforma.service`](../../../platform/web/systemd/espresso-plataforma.service)).
2. **Las imágenes se etiquetan con fecha y versión del código.**
3. **La versión anterior se conserva** hasta comprobar que la nueva opera, para
   volver atrás sin reconstruir.
4. **Toda actualización es manual y deliberada**, precedida de respaldo según
   [ADR-015](ADR-015-almacenamiento-retencion-y-respaldo.md) y seguida del
   recorrido de aceptación. No hay actualización automática de imágenes ni del
   sistema operativo.
5. **Las migraciones de base de datos solo van hacia adelante.** Revertir la
   aplicación no revierte el esquema.
6. **No se actualiza durante una campaña experimental**, porque una serie de
   preparaciones comparables no puede atravesar un cambio de versión sin
   declararlo.

### Enmienda del 2026-10-30: la unidad no exige el disco externo

El punto 1 exigía montar el disco externo antes de arrancar, cuando los datos
iban a residir en él. ADR-015 los dejó en la tarjeta y convirtió el disco en
destino de copia. Exigirlo solo impediría arrancar cuando el disco de copias
falla o se desconecta. La unidad arranca sin él y deja un aviso en el registro
del sistema.

## 5. Consecuencias

- La instalación vuelve sola tras un corte y se sabe qué versión opera.
- La vuelta atrás es inmediata mientras se conserve la versión anterior.
- Las actualizaciones de seguridad requieren una intervención planificada; el
  aislamiento de red de [ADR-014](ADR-014-exposicion-de-la-plataforma-en-la-red.md)
  atenúa ese riesgo.
- Conservar la versión anterior ocupa espacio adicional.

## 6. Verificación de la decisión

1. Tras un corte de energía, la instalación arranca con todos los servicios y
   sus datos. Verificado el 2026-10-30: seis servicios operativos y
   `/health/ready` en 200.
2. Sin el disco de copias, los servicios arrancan y queda el aviso en el
   registro. Verificado el 2026-10-30.
3. La versión desplegada se identifica desde el equipo.
4. Una actualización completa puede revertirse a la versión anterior.
5. Un servicio detenido a mano vuelve a operar tras reiniciar.
