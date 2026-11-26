# ADR-003 — Aplicación web única en la plataforma externa local

**Estado:** Aceptada, revisada  
**Fecha de decisión original:** 2026-08-23  
**Fecha de revisión:** 2026-09-09  
**Ámbito:** interfaz web, conectividad, experiencia de uso y frontera de despliegue  
**Reemplaza:** la decisión que distribuía la interfaz web entre la ESP32-S3 y la plataforma externa

## 1. Contexto

Según ADR-001 y ADR-005, la ESP32-S3 conserva la operación crítica y el barista
conserva el mando físico para operar la máquina, sin depender de la red ni de
la aplicación web.

La aplicación web necesita consultar el estado, registrar información y
presentar el historial, las curvas, el gemelo digital, la ontología, el
razonamiento y los modelos de aprendizaje. Estas funciones pertenecen a la
plataforma externa local, candidata a Raspberry Pi.

## 2. Problema de decisión

Se debe definir si la ESP32-S3 alojará una segunda interfaz web o si existirá
una única aplicación web en la plataforma externa. La decisión debe evitar
duplicar interfaces y lógica, conservar la autoridad del firmware y establecer
qué ocurre cuando la plataforma externa no está disponible.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Interfaz web mínima en la ESP32-S3 y aplicación completa externa | Ofrece supervisión web independiente, pero duplica frontend, autenticación, pruebas, mantenimiento y parte del contrato de datos. |
| Aplicación web única en la plataforma externa local | Reduce la complejidad y concentra la experiencia web. La operación física y la seguridad continúan disponibles aunque la plataforma externa falle. Alternativa seleccionada. |
| Toda la aplicación en la ESP32-S3 | Consume recursos del controlador y mezcla la operación crítica con historial, análisis y aprendizaje. No se selecciona. |

## 4. Decisión

La ESP32-S3 no alojará una interfaz web. La única aplicación web se ejecutará
en la plataforma externa local, inicialmente candidata a Raspberry Pi. La
ESP32-S3 expondrá únicamente los servicios de comunicación y diagnóstico que
sean necesarios para intercambiar datos con esa aplicación.

La distribución de la lógica crítica y la autoridad del firmware se mantienen
según ADR-001 y ADR-005. La aplicación web no reproducirá esa lógica: consultará
el estado publicado y enviará únicamente datos o propuestas dentro de los
límites definidos por el firmware.

La aplicación web será responsable de:

- consultar el estado actual, alarmas, mediciones, referencias y preparación en curso;
- registrar dosis, masa final de bebida, TDS y evaluación sensorial;
- consultar, buscar, filtrar y comparar el historial;
- visualizar curvas, indicadores, desviaciones y resultados simulados;
- mantener el gemelo digital, la ontología, el razonamiento y los modelos de aprendizaje;
- presentar análisis y recomendaciones con su procedencia, versión e incertidumbre;
- permitir aceptar, rechazar o modificar recomendaciones;
- administrar perfiles y configuraciones como propuestas pendientes;
- gestionar usuarios, permisos, auditoría, copias de respaldo y sincronización.

La aplicación web no podrá iniciar ni detener extracciones, escribir
directamente en los actuadores, participar en los lazos de control, modificar
límites protegidos ni activar automáticamente una recomendación.

## 5. Funcionamiento degradado

Si la Raspberry Pi, la red o la aplicación web no están disponibles:

- la cafetera continuará operando mediante el panel físico;
- la adquisición, regulación, temporización y seguridad continuarán en la ESP32-S3;
- los registros se conservarán en la cola local definida en ADR-002;
- no estarán disponibles la consulta web, el historial completo ni el análisis externo;
- la pérdida de comunicación o de disponibilidad de la plataforma no podrá
  iniciar, detener automáticamente ni modificar una extracción.

La ausencia de la aplicación web limita la supervisión y el análisis, pero no
convierte la plataforma externa en una dependencia del control de la cafetera.

## 6. Frontera de comunicación

La aplicación web se comunicará con la ESP32-S3 mediante un contrato versionado
de mensajes. La ESP32-S3 será la fuente autorizada del estado instantáneo, las
mediciones válidas, las referencias activas y las causas de finalización. La
plataforma externa será la fuente del historial consolidado, los análisis, el
conocimiento y las recomendaciones.

Las propuestas provenientes de la plataforma externa se identificarán,
validarán localmente y permanecerán pendientes hasta que el barista las acepte
y el firmware confirme que cumplen sus límites. La respuesta o reconexión de la
plataforma no tendrá autoridad sobre los actuadores.

## 7. Consecuencias

### 7.1 Consecuencias favorables

- Se mantiene una sola experiencia web y un solo frontend que desarrollar.
- La ESP32-S3 conserva recursos para control, adquisición y seguridad.
- Se evita duplicar lógica de presentación, autenticación y sincronización.
- La aplicación completa puede evolucionar sin actualizar el firmware por cada cambio visual.
- La cafetera mantiene su operación física cuando la plataforma web no está disponible.

### 7.2 Consecuencias desfavorables

- La supervisión web depende de que la Raspberry Pi y su red local estén disponibles.
- Durante una falla de la plataforma externa no habrá consulta web del estado.
- El panel físico debe comunicar las acciones operativas básicas y las condiciones de fallo definidas para el prototipo.
- Se requiere un contrato estable entre la aplicación web y el firmware.

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| API y contrato de datos | Validar mensajes, estados, unidades, versiones, autenticación y errores. |
| Despliegue en Raspberry Pi | Confirmar que la aplicación completa, Modelica, persistencia y servicios de conocimiento pueden ejecutarse con recursos aceptables. |
| Roles y permisos | Definir los permisos para consulta, registro y configuración propuesta. |
| Diagnóstico sin aplicación web | Definir las señales del panel, registro serial y procedimiento de diagnóstico del firmware. |
| Actualización independiente | Probar que actualizar la aplicación web no modifica la lógica crítica ni deja propuestas activas sin validar. |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. operación mediante el panel físico con la Raspberry Pi apagada;
2. conservación y posterior sincronización de registros durante la desconexión;
3. consulta del estado actual y del historial desde la aplicación web;
4. rechazo de intentos de inicio o acceso directo a los actuadores;
5. validación local de perfiles y recomendaciones recibidos;
6. la parada normal se confirma exclusivamente mediante el mando físico;
7. actualización de la aplicación web sin afectar adquisición, regulación ni seguridad;
8. ejecución conjunta de la aplicación web, persistencia, Modelica y servicios de conocimiento en la Raspberry Pi candidata.

## 10. Trazabilidad

- Requisitos funcionales: RF-003–RF-006, RF-013–RF-029.
- Requisitos no funcionales: RNF-001, RNF-006–RNF-009.
- Restricciones de diseño: RD-001–RD-005.

## 11. Criterios de reconsideración

La decisión deberá revisarse si la Raspberry Pi no puede ejecutar la aplicación
completa con los recursos disponibles, si la comunicación impide consultar el
estado con la frecuencia necesaria o si el panel físico no permite operar y
diagnosticar la cafetera de forma suficiente durante una falla de la plataforma
externa. Cualquier interfaz web futura deberá conservar la autoridad única de
la ESP32-S3 sobre el control y la seguridad.
