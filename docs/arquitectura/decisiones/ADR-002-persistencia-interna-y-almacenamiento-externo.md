# ADR-002 — Persistencia interna mínima y almacenamiento histórico externo asíncrono

**Estado:** Aceptada (2026-08-23)
**Ámbito:** persistencia local, recuperación, historial y sincronización

## 1. Contexto

La cafetera debe extraer sin Internet ni servidor. Lo indispensable para operar
—configuración efectiva, límites de seguridad, calibraciones, estado de
recuperación e identidad— ocupa poco y cabe en la memoria no volátil de la
ESP32-S3-N16R8 (16 MB de flash, 8 MB de PSRAM). Las series, el historial y el
análisis aportan valor, pero no son precondición de una extracción.

## 2. Alternativas consideradas

| Alternativa | Razón |
|---|---|
| microSD como almacén principal | Descartada: añade ranura, bus y fallos de medio removible innecesarios para extraer. |
| Servidor sin persistencia local | Descartada: una desconexión perdería preparaciones. |
| Flash interna con cola acotada e historial externo | **Seleccionada.** |
| Flash externa soldada | Reservada por si la flash del módulo resulta insuficiente. |

## 3. Decisión

La NVS (256 KiB) guarda identidad, credenciales, configuración, límites,
calibraciones, estado de recuperación y una cola de 32 sobres de hasta 1536
bytes. El resto de la flash aloja el firmware en dos particiones de 3 MiB
(ADR-021). La RAM y la PSRAM guardan solo la extracción en curso. El historial,
las series, las evaluaciones, la ontología y la simulación residen en la
plataforma externa local, una Raspberry Pi.

La flash no es un historial: la cola solo tolera desconexiones. En la tarjeta se
verificaron escritura, lectura en orden, deduplicación y borrado selectivo por
confirmación (`ACK`).

Cada preparación recibe un identificador estable antes de iniciarse y pasa por
los estados `en curso`, `pendiente de sincronización`, `sincronizada`,
`incompleta` y `error de persistencia`. Reenviar el mismo identificador no la
duplica.

Al terminar, el registro se cierra y entra por lotes a la cola; ninguna muestra
se escribe suelta en NVS. Conserva la configuración aplicada, la causa de
finalización, el tiempo, las series y los eventos. Dosis, masa final y TDS se
añaden después a mano, con unidad y procedencia, y no participan en el control.

Sin servicio externo, la cafetera sigue extrayendo y los registros esperan en la
cola. Si la cola se llena o la partición falla, el control y la seguridad
siguen, el sistema advierte que no garantiza el historial y no presenta la
preparación como evidencia completa.

La ESP32-S3 no guarda credenciales administrativas: sincroniza con una API de
ingestión que valida el esquema y limita los permisos de cada dispositivo.

## 4. Consecuencias

**Favorables.** Desaparecen la microSD y sus fallos. La extracción no depende de
la red. El historial y el análisis corren donde hay recursos, y los reintentos
son seguros.

**Desfavorables.** Pocas preparaciones caben localmente; una desconexión larga
puede agotar la cola. Hay que vigilar el desgaste de la flash. El historial
depende de que la plataforma esté disponible periódicamente.

## 5. Verificación

1. Extracción con servidor e Internet desconectados.
2. Recuperación tras reinicio de una preparación pendiente.
3. Sincronización repetida sin duplicados.
4. Corte de energía durante la persistencia.
5. Cola llena o error de escritura sin perder la supervisión de actuadores.
6. Ninguna respuesta del servidor inicia una extracción ni cambia una
   referencia activa.

## 6. Criterios de reconsideración

Se revisa si el protocolo exige guardar más series de las que admite la cola, si
la conectividad impide sincronizar a tiempo o si la flash no basta en capacidad
o vida útil. Cualquier ampliación queda fuera del lazo de control.
