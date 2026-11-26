# ADR-003 — Aplicación web única en la plataforma externa local

**Estado:** Aceptada el 2026-09-09.

## 1. Contexto

Según ADR-001 y ADR-005, la ESP32-S3 conserva la operación crítica y el barista
opera la máquina con el mando físico, sin depender de la red. La aplicación web
consulta el estado, registra información y presenta el historial, las curvas,
el gemelo digital, la ontología y el razonamiento. Hay que decidir si la
ESP32-S3 aloja además una interfaz web propia.

## 2. Alternativas consideradas

- **Interfaz mínima en la ESP32-S3 y aplicación completa externa.** Descartada:
  duplica interfaz, autenticación, pruebas y parte del contrato de datos.
- **Toda la aplicación en la ESP32-S3.** Descartada: consume recursos del
  controlador y mezcla la operación crítica con el historial y el análisis.
- **Aplicación única en la plataforma externa.** Seleccionada.

## 3. Decisión

La ESP32-S3 no aloja una interfaz web. La única aplicación web se ejecuta en la
plataforma externa local, una Raspberry Pi. La ESP32-S3 expone solo los
servicios de comunicación y diagnóstico necesarios para intercambiar datos con
ella.

La aplicación consulta el estado, las alarmas y las mediciones; registra dosis,
masa de bebida, TDS y evaluación sensorial; consulta y compara el historial;
mantiene el gemelo digital, la ontología y el razonamiento; presenta
recomendaciones con su procedencia; y administra perfiles, usuarios y respaldos.

La aplicación no inicia ni detiene extracciones, no escribe en los actuadores,
no participa en los lazos de control, no modifica límites protegidos y no
activa por sí misma una recomendación.

## 4. Razones

Una sola aplicación evita duplicar la presentación, la autenticación y la
sincronización. La ESP32-S3 reserva sus recursos para el control, la
adquisición y la seguridad, y la aplicación puede cambiar sin actualizar el
firmware.

## 5. Funcionamiento degradado

Si la plataforma, la red o la aplicación no están disponibles, la cafetera sigue
operando con el panel físico; la adquisición, la regulación y la seguridad
siguen en la ESP32-S3; y los registros esperan en la cola local de ADR-002. Se
pierden la consulta web y el análisis. La pérdida de comunicación no inicia, no
detiene ni modifica una extracción.

## 6. Frontera de comunicación

La aplicación se comunica con la ESP32-S3 mediante un contrato de mensajes
versionado. La ESP32-S3 es la fuente autorizada del estado instantáneo, las
mediciones válidas, las referencias activas y las causas de finalización. La
plataforma es la fuente del historial consolidado, los análisis y las
recomendaciones.

Una propuesta de la plataforma se identifica, se valida localmente y queda
pendiente hasta que el barista la acepta y el firmware confirma que cumple sus
límites. Ni la respuesta ni la reconexión de la plataforma tienen autoridad
sobre los actuadores.

## 7. Consecuencias

- Hay una sola interfaz que desarrollar y mantener.
- La supervisión web depende de que la Raspberry Pi y la red local estén
  disponibles.
- El panel físico debe comunicar las acciones básicas y las condiciones de
  fallo.
- La aplicación y el firmware necesitan un contrato estable.
