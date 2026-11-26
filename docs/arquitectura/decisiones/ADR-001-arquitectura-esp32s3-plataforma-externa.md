# ADR-001 — Arquitectura ESP32-S3 y plataforma externa local

**Estado:** Aceptada, 2026-09-09.

## 1. Contexto

La adquisición, la regulación y la seguridad deben responder en poco tiempo y
seguir funcionando sin comunicación. La aplicación web, el historial, el modelo
de Modelica exportado como FMU y la ontología necesitan más memoria,
almacenamiento y bibliotecas de las que ofrece un microcontrolador. Esos
componentes deben poder cambiar sin tocar el firmware que protege la cafetera.

## 3. Alternativas consideradas

| Alternativa | Razón |
|---|---|
| ESP32-S3 sola | Obliga a simplificar Modelica y la ontología y comparte recursos con el control. Descartada. |
| Varios microcontroladores | Añade puntos de fallo sin resolver la carga de Modelica. Descartada. |
| ESP32-S3 y plataforma externa local | Mantiene el control en el dispositivo y lleva a Linux lo demás. **Seleccionada.** |

## 4. Decisión

El sistema usa una ESP32-S3 como controlador de campo y una plataforma externa
local, una Raspberry Pi 5 junto a la cafetera, para supervisión, datos,
simulación y conocimiento. La plataforma no depende de Internet.

### 4.1 Responsabilidades de la ESP32-S3

La ESP32-S3 adquiere y valida las medidas, ejecuta la máquina de estados,
regula temperatura y presión, acciona el calentador y la bomba y conserva
configuración, calibraciones y una cola local limitada. Recibe propuestas de la plataforma, pero valida su formato,
límites, vigencia y estado. Ningún mensaje externo inicia una preparación,
acciona un actuador ni reemplaza una protección.

### 4.2 Responsabilidades de la plataforma externa

La plataforma externa sirve la aplicación web y conserva el historial y las
series de las preparaciones. Gestiona cafés, perfiles y evaluaciones
sensoriales. Ejecuta la FMU y compara las mediciones con los objetivos y con la
simulación. Ejecuta la ontología, la validación SHACL y el razonamiento, y
construye análisis y recomendaciones trazables. Sus modelos cambian solo por
versiones aprobadas, según
[ADR-004 § 12](ADR-004-distribucion-datos-ontologia-aprendizaje.md). Envía sus
propuestas como mensajes pendientes de aceptación local.

Si la plataforma se desconecta, la ESP32-S3 conserva el control, la seguridad
y los datos pendientes de sincronización.

### 4.3 Distribución dentro de la ESP32-S3

| Núcleo | Responsabilidades |
|---|---|
| Núcleo 1 | Funciones críticas: adquisición, regulación, temporización, máquina de estados, supervisión y puerta de actuación. |
| Núcleo 0 | Funciones no críticas: wifi de ESP-IDF, persistencia, sincronización y diagnóstico, con prioridad inferior. |

## 5. Justificación

El control queda junto a sensores y actuadores, sin depender de una red. La
plataforma ejecuta la FMU sin reducir el modelo y la ontología sin convertirla
en reglas fijas, y ambas se actualizan sin tocar el firmware. Para una sola
cafetera, una Raspberry Pi cuesta menos que simplificarlas.

## 6. Comunicación y límites de autoridad

La comunicación es local y usa un contrato versionado. Cada mensaje identifica
como mínimo el dispositivo, el tipo y versión del mensaje, un número de
secuencia, el tiempo monotónico de la ESP32, la preparación asociada, la
calidad de los datos, las versiones relevantes y, cuando corresponde, la
expiración y la confirmación.

La ESP32-S3 es la autoridad de control, seguridad y límites; la plataforma, del
historial, la simulación y las recomendaciones. Ningún resultado de Modelica o de la
ontología llega directamente a los actuadores.

## 7. Consecuencias

El control funciona sin Internet ni Raspberry Pi. A cambio, hay un equipo y un
canal más que mantener, y sin conexión se pierden la simulación y las
recomendaciones hasta recuperarla. La
FMU se recompiló para `aarch64` y reproduce la referencia sin diferencia; la
Raspberry Pi necesita refrigeración activa para no perder velocidad.

La decisión se reconsidera si la comunicación afecta los plazos críticos.
