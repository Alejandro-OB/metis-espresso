# ADR-013 — Fuente de hora y fechado de la evidencia

**Estado:** Aceptada, 2026-09-21

## 1. Contexto

Cada medición y cada preparación deben llevar fecha. La ESP32-S3 no
tiene reloj con respaldo y obtiene la fecha de la red, pero la máquina debe
preparar café aunque la red falle. El firmware no fecha lo que no conoce y rechaza
instantes anteriores a 2020. Antes de esta decisión tomaba la hora de un servidor
de Internet y retenía una sola preparación sin fechar.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Hora de un servidor de Internet | Falla justo cuando la red cae y la máquina debe seguir operando. Descartada. |
| Reloj con respaldo en la ESP32-S3 | Añade un componente al nodo crítico para un dato que la plataforma ya tiene. Descartada. |
| Plataforma que sirve su hora sin validarla | Propagaría una fecha falsa a la evidencia. Descartada. |
| Fechar con una hora aproximada | Contamina la evidencia. Descartada. |
| Retener solo la última preparación sin fechar | Pierde cafés tras un arranque sin red. Descartada. |
| Hora de la plataforma y retención con instante interno | **Seleccionada.** |

## 4. Decisión

Ninguna preparación se registra con una fecha inventada, y la falta de hora no
impide preparar café.

### 4.1 La plataforma es la fuente de hora

El dispositivo pide la hora a la plataforma, que está a un salto en la misma red
y es un solo sitio que diagnosticar.

### 4.2 Hora de la plataforma

La plataforma se sincroniza por Internet cuando puede y tiene un reloj con
batería para el arranque tras un corte.

### 4.3 Sin hora fiable no se sirve hora

Si su reloj no está validado, la plataforma declara que no tiene hora. Una fecha
falsa parece válida; una ausente se detecta.

### 4.4 La hora viaja en la respuesta de sincronización

El dispositivo toma la fecha de la respuesta de sincronización
([ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md)), sin
servicios ni protocolos nuevos.

### 4.5 Retención de preparaciones sin fechar

Las preparaciones sin hora se conservan con su instante interno y se fechan por
resta al conocer la diferencia con la hora real.

## 6. Consecuencias

La evidencia no se pierde por preparar café sin hora. El firmware debe sellar
preparaciones después de cerrarlas, la plataforma debe declarar la fiabilidad de
su reloj y el reloj con batería es un componente más que mantener.

## 7. Límite declarado

Si el dispositivo se reinicia antes de obtener la hora, su tiempo interno vuelve
a cero y las preparaciones retenidas no pueden fecharse. Se pierden y la pérdida
se informa; no se inventa su instante.

## 13. Estado de implementación

El registrador retiene tantos registros como admite la cola persistente. Al
llenarse descarta el más reciente, cuenta el descarte y expone los registros en
espera y los perdidos. Cada registro se sella con su instante de cierre más el
desfase medido entre el reloj monótono y la hora real.

El firmware toma la hora de la respuesta de sincronización, solo en UTC terminada
en `Z`, con un módulo puro probado en el anfitrión. Una hora ilegible se rechaza
(§ 4.3). Se eliminó así un fallo del servicio de hora externo, que tras un primer
intento sin respuesta no se recuperaba sin reiniciar. Sobre la placa, la hora
llegó a los nueve segundos del arranque.

La plataforma omite la hora mientras su reloj no está sincronizado
(`platform/web/backend/src/aplicacion_web/application/clock.py`). El reloj con
batería de § 4.2 no está instalado.
