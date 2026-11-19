# ADR-001 — Arquitectura ESP32-S3 y plataforma externa local

**Estado:** Aceptada  
**Fecha de decisión:** 2026-09-09  
**Fecha de revisión:** 2026-09-15 (§12: precisión sobre entrenamiento e inferencia)  
**Ámbito:** distribución de responsabilidades entre la cafetera, la ESP32-S3 y la plataforma externa

## 1. Contexto

El sistema combina dos tipos de trabajo con necesidades diferentes. La
adquisición de sensores, la regulación y la seguridad deben responder en poco
tiempo y continuar funcionando aunque se pierda la comunicación. La aplicación
web, el historial, Modelica, la ontología, el análisis y el aprendizaje
requieren más memoria, almacenamiento, librerías y capacidad de procesamiento.

El gemelo digital utiliza modelos de OpenModelica que se exportan como FMU o
código compatible con FMI. Para ejecutarlos se necesita un entorno de ejecución
que resuelva el modelo y conserve sus resultados. La ontología completa también
requiere almacenar, validar, consultar y razonar sobre información que no es
conveniente incorporar al firmware de la cafetera.

La ESP32-S3 puede realizar el control local y mantener una representación
reducida para funciones de respaldo, pero no debe asumir toda la carga de
Modelica, razonamiento semántico, historial y aprendizaje. Además, modificar
continuamente esos componentes debería ser posible sin cambiar ni interrumpir
el firmware que protege y controla la cafetera.

La plataforma externa será inicialmente una Raspberry Pi candidata instalada
junto a la cafetera. Su uso no dependerá de Internet ni de servicios en la
nube. El computador personal seguirá siendo el entorno de desarrollo,
compilación, validación y respaldo.

## 2. Problema de decisión

Se debe definir qué funciones permanecen en la ESP32-S3 y cuáles se ejecutan en
la plataforma externa, de manera que el sistema conserve respuesta local,
seguridad, fidelidad del gemelo digital, flexibilidad para el conocimiento del
barista y facilidad para mantener la aplicación.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| ESP32-S3 monolítica | Reduce el hardware y evita comunicación entre equipos, pero obliga a simplificar Modelica y la ontología, comparte recursos con el control y dificulta actualizar el conocimiento, el historial y el aprendizaje. |
| ESP32-S3 más plataforma externa local | Mantiene el control y la seguridad en el dispositivo, y permite ejecutar en Linux la web, el historial, Modelica, la ontología, el análisis y el aprendizaje. Añade comunicación y un equipo local que deben validarse. **Seleccionada.** |
| Control distribuido entre varios microcontroladores | Puede separar funciones, pero aumenta componentes, firmware, protocolos y puntos de fallo sin resolver la carga de Modelica y conocimiento. |

## 4. Decisión

El sistema utilizará una arquitectura híbrida formada por una **ESP32-S3 como
controlador de campo** y una **plataforma externa local, candidata a Raspberry
Pi, como plataforma de supervisión, datos, simulación y conocimiento**.

### 4.1 Responsabilidades de la ESP32-S3

La ESP32-S3 será responsable de:

- adquirir y validar temperatura, presión y tiempo;
- ejecutar la máquina de estados de la cafetera;
- regular temperatura y presión;
- aplicar la temporización y el tiempo máximo de la preparación;
- supervisar condiciones inseguras y llevar las salidas a un estado seguro;
- controlar directamente el calentador y la bomba;
- conservar configuración, calibraciones, recuperación y una cola local limitada;
- mantener la operación crítica aunque la plataforma externa no esté disponible.

La ESP32-S3 puede recibir una propuesta, perfil o solicitud de la plataforma
externa, pero debe validar localmente su formato, límites, vigencia y estado.
Ningún mensaje externo puede iniciar una preparación, modificar directamente
un actuador o reemplazar una protección.

### 4.2 Responsabilidades de la plataforma externa

La plataforma externa será responsable de:

- servir la aplicación web completa;
- conservar el historial y las series de preparaciones;
- gestionar cafés, perfiles, evaluaciones sensoriales y comparaciones;
- ejecutar Modelica y la FMU del modelo virtual;
- comparar mediciones con objetivos y resultados simulados;
- ejecutar la ontología, RDF, validación SHACL y razonamiento;
- construir análisis, hipótesis y recomendaciones trazables;
- entrenar o ejecutar modelos de aprendizaje contextual;
- conservar versiones, datos de entrenamiento, resultados y limitaciones;
- enviar propuestas validadas como mensajes pendientes de aceptación local.

La plataforma externa no es necesaria para iniciar o finalizar localmente una
preparación. Si se desconecta, la ESP32-S3 conserva el control, la seguridad y
los datos pendientes de sincronización.

### 4.3 Distribución dentro de la ESP32-S3

| Núcleo | Responsabilidades |
|---|---|
| Núcleo 1 | Funciones críticas: adquisición, regulación, temporización, máquina de estados, supervisión y puerta de actuación. |
| Núcleo 0 | Funciones locales no críticas: Wi-Fi, persistencia, sincronización, API de comunicación y tareas de diagnóstico. La aplicación web se ejecuta en la plataforma externa. El Wi-Fi de ESP-IDF permanece en este núcleo. |

La Raspberry Pi no participa en esta distribución de núcleos.

## 5. Justificación

La arquitectura híbrida fue seleccionada por las siguientes razones:

1. **Respuesta y seguridad:** el control de temperatura, presión, tiempos y
   salidas permanece cerca de los sensores y actuadores, sin depender de una
   red o de un proceso externo.
2. **Capacidad para el gemelo digital:** la plataforma externa puede ejecutar
   el entorno de Modelica y la FMU sin reducir el modelo para ajustarlo a los
   recursos de la ESP32-S3.
3. **Conocimiento completo:** la ontología puede conservar OWL/RDF, validación,
   consultas y razonamiento sin convertirse en tablas o reglas rígidas dentro
   del firmware.
4. **Flexibilidad:** el conocimiento del barista, los modelos, la aplicación y
   los datos pueden actualizarse sin reflashear la ESP32-S3.
5. **Mantenibilidad:** cada parte se modifica y prueba en el entorno más
   adecuado para su función.
6. **Costo para el prototipo:** para una sola cafetera, el costo y consumo
   adicionales de una Raspberry Pi son aceptables frente al esfuerzo de
   simplificar Modelica y la ontología para ejecutarlos en la ESP32-S3.
7. **Autonomía local:** la Raspberry Pi permite operar la plataforma sin
   Internet, mientras la ESP32 mantiene la operación básica si la Raspberry Pi
   falla.

## 6. Comunicación y límites de autoridad

La comunicación entre la ESP32-S3 y la plataforma externa será local y utilizará
un contrato versionado. Cada mensaje deberá identificar, como mínimo, el
dispositivo, el tipo y versión del mensaje, un número de secuencia, el tiempo
monotónico de la ESP32, la preparación asociada, la calidad de los datos, las
versiones relevantes y, cuando corresponda, la expiración y confirmación.

La ESP32-S3 será la autoridad de adquisición, control, seguridad, límites y
finalización local. La plataforma externa será la autoridad del historial, la
simulación, el conocimiento, el análisis y las recomendaciones. Ningún
resultado de Modelica, la ontología o el aprendizaje tendrá acceso directo a
los actuadores.

## 7. Consecuencias

### 7.1 Consecuencias favorables

- El control continúa funcionando sin Internet ni Raspberry Pi.
- Modelica puede conservar un modelo más fiel y ejecutarse con su entorno
  correspondiente.
- La ontología y el aprendizaje pueden evolucionar sin modificar el control.
- La aplicación web y el historial disponen de recursos adecuados.
- La separación de responsabilidades facilita las pruebas y el mantenimiento.

### 7.2 Consecuencias desfavorables

- Se añade un equipo local y un canal de comunicación que deben mantenerse.
- La plataforma externa requiere validación de arquitectura ARM, memoria,
  almacenamiento y temperatura.
- La pérdida de comunicación limita el historial, la simulación y las
  recomendaciones hasta recuperar la conexión.
- La integración completa requiere comprobar la compatibilidad de la FMU y de
  todos los servicios con la Raspberry Pi seleccionada.

## 8. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Pérdida de la Raspberry Pi o de la comunicación | La ESP32 conserva control, seguridad, última configuración válida y cola local. |
| Mensaje vencido, duplicado o inválido | Secuencia, expiración, confirmación, validación local e idempotencia. |
| Sobrecarga de la ESP32 por servicios locales | Prioridad inferior, recursos limitados y separación entre funciones críticas y no críticas. |
| FMU incompatible con Raspberry Pi | Confirmar arquitectura ARM, recompilar si es necesario y validar la ejecución numérica y el rendimiento. |
| Resultado externo interpretado como orden | La ESP32 acepta únicamente propuestas validadas; ningún servicio externo escribe actuadores. |
| Falta de recursos en la Raspberry Pi | Medir memoria, CPU, almacenamiento y temperatura con los servicios activos; disponer de un modo reducido. |

## 9. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| ~~Selección de la Raspberry Pi~~ | **Cerrada el 2026-09-16.** Raspberry Pi 5 Model B rev. 1.1 con 8 GiB, Raspberry Pi OS Lite de 64 bits sobre Debian 13, tarjeta de 16 GB dimensionada y fuente de 5 V/3 A. Veredicto **apto con condiciones** en `device/tests/informe-capacidad-plataforma-externa.md`; las condiciones están en su § 2.1, y la primera —refrigeración activa— no es opcional: sin ella el equipo pierde el 40 % de su velocidad. |
| Comunicación ESP32–Raspberry Pi | **Primera prueba del enlace el 2026-09-16**: la cafetera entrega su telemetría y sus preparaciones a la plataforma, con 40 ms de latencia extremo a extremo, y recupera lo encolado tras quedarse sin servidor. Resultados en `device/tests/integracion/resultados-integracion.md`. Queda por ejercitar el contrato completo —expiración y comandos— y por hacerlo con medición real. |
| Aplicación web y servicios | **Comprobado el 2026-09-16** para web, API, base de datos y servicios semánticos, que operan en el equipo dentro de todos los criterios. El aprendizaje no se ejercitó: no hay modelo aprobado que ejecutar. |
| ~~Modelica/FMU~~ | **Cerrada el 2026-09-16.** Recompilada para `aarch64` desde las fuentes que el propio artefacto contiene, en menos de 15 s, con **diferencia exactamente cero** frente a la referencia sobre 300 muestras. Queda abierto el registro de un artefacto por arquitectura (ADR-011 § 4.2). |
| Sensores reales | Confirmar plazos, *jitter*, estabilidad y recuperación con adquisición y control reales. |

## 10. Verificación y reconsideración

La prueba de banco confirmó la asignación de tareas del firmware: las funciones
críticas se ejecutan en el núcleo 1, las funciones locales no críticas en el
núcleo 0 y el Wi-Fi en el núcleo 0. La evidencia detallada se conserva en
Validación de la máquina de estados y afinidad.

La decisión se mantiene mientras la ESP32-S3 cumpla los requisitos de operación
crítica y la plataforma externa pueda ejecutar sus servicios sin alterar esa
operación. Debe reconsiderarse si la comunicación afecta los plazos críticos,
si la Raspberry Pi no dispone de recursos suficientes o si la FMU y los
servicios esenciales no pueden ejecutarse de forma reproducible en la
plataforma seleccionada.

## 11. Trazabilidad

- [Requisitos oficiales del sistema](../../requisitos/requisitos_sistema.md): RF-003–RF-029,
  RNF-001–RNF-009 y RD-001–RD-005.
- Arquitectura lógica.
- Arquitectura de despliegue.
- Evidencia de la prueba de la máquina de estados.

## 12. Revisión · Entrenamiento e inferencia de los modelos de aprendizaje

**Fecha:** 2026-09-15

La § 4.2 atribuye a la plataforma externa «entrenar o ejecutar modelos de
aprendizaje contextual», sin distinguir ambas actividades. La distinción resultó
necesaria al definir qué reside en el equipo desplegado.

Se precisa que la plataforma externa **ejecuta la inferencia** como parte del
ciclo de propuesta al barista, mientras que **el entrenamiento se realiza en el
entorno de desarrollo** mientras el protocolo experimental no esté aprobado, y
podrá trasladarse después sin alterar el régimen de actualización.

El régimen —reentrenamiento por lotes, con modelo versionado y aprobación
humana explícita, excluida la actualización continua automática— y su
justificación están en
[ADR-004 § 12](ADR-004-distribucion-datos-ontologia-aprendizaje.md).

Esta precisión no modifica la distribución de responsabilidades ni la frontera
de autoridad establecidas en este ADR.
