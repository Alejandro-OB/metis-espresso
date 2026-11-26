# ADR-012 — Canal de comunicación entre la ESP32-S3 y la plataforma externa

**Estado:** Aceptada (2026-09-21)
**Ámbito:** transporte, contenido, cadencia, direccionamiento e identidad del enlace entre el controlador local y la plataforma externa

## 1. Contexto

La ESP32-S3 conserva la autoridad sobre adquisición, control y seguridad
(ADR-001 § 6), y una cola local acotada evita perder evidencia durante una
desconexión (ADR-002). El dispositivo ya enviaba `POST` HTTP a `/api/v1/sync`
cada 15 s, con credencial en la cabecera `Authorization`, y borraba de la cola
solo lo confirmado. Pero la aplicación web debe permitir consultar estado, alarmas y mediciones
mientras la máquina opera, y no había forma de hacerlo. Además, la dirección de
la plataforma y la credencial eran constantes de compilación.

## 3. Alternativas consideradas

| Alternativa | Razón |
|---|---|
| HTTP con entrega desde el dispositivo | **Seleccionada.** Ya funciona, confirma por mensaje y no añade servicios. |
| MQTT con intermediario | Descartada: añade un servicio permanente para repartir entre suscriptores que no existen. |
| WebSocket persistente | Descartada: hay que vigilar una conexión abierta, sin ventaja para un observador humano. |
| Telemetría encolada con la evidencia | Descartada: a una muestra por segundo agota la cola de 32 sobres en 32 s. |
| Telemetría sin persistir por el mismo canal | **Seleccionada.** |
| Servidor consultable en la ESP32-S3 | Descartada: añade carga y exposición al nodo crítico. |
| Dirección y credencial compiladas | Descartada: cada cambio de red obliga a regrabar. |

## 4. Decisión

1. El transporte es HTTP, iniciado por el dispositivo, con confirmación por
   mensaje y sin intermediario de mensajería.
2. Durante la preparación, el dispositivo emite `telemetry.sample` a una
   muestra por segundo. Fuera de la preparación emite una cada 15 s, lo justo
   para mantener el estado al día. Ambas cadencias se configuran en el firmware.
3. La telemetría en vivo no ocupa la cola y se descarta si no hay enlace. La
   cola queda para la evidencia: resúmenes, series (§ 4.1) y eventos.
4. La dirección de la plataforma y la credencial del dispositivo son
   configuración guardada en el dispositivo, no constantes de compilación, de
   modo que cambiarlas no exige regrabar. El dispositivo tiene identidad y
   credencial propias, distintas de las de cualquier persona.

La autoridad no cambia: ninguna respuesta de la plataforma inicia o detiene una
preparación, y el dispositivo valida cada propuesta contra sus límites antes de
activarla o rechazarla (`platform/contracts/device/sync-response.schema.json`).

### 4.1 La serie medida viaja por la cola

La comparación entre el proceso físico y el virtual usa las
muestras de cada preparación. La telemetría en vivo no sirve para eso porque se
pierde sin enlace. Por eso, al cerrar la preparación, el dispositivo compone la
serie medida y la encola en `extraction.series`
(`platform/contracts/device/extraction-series.schema.json`), después del
resumen. Así sobrevive a un corte, y con la cola llena se pierde la serie, nunca
la ficha.

La serie usa una rejilla de 500 ms, la de la comparación. Sus puntos se
seleccionan, sin promediar ni interpolar, de modo que cada uno se tomó en el
instante que declara. En la plataforma sustituye a las muestras provisionales
de telemetría. Sin hora al cerrar no puede componerse (ADR-013 § 7).

Esta serie no es el registro del lazo de control, que sale por el puerto serie a
alta frecuencia (ADR-007 § 3.6). Alimentar uno desde el otro ataría el registro
del lazo a la red.

**Minuto previo a pulsar (2026-10-19).** La estabilidad térmica se evalúa sobre
el cuerpo de la caldera en el minuto previo a pulsar, con la máquina lista,
dentro de ±0,8 °C de su referencia
(`platform/packages/espresso_stability/machine.py`). Por eso la serie lleva
delante ese minuto, con `elapsedMs` negativo (desde −60 000) y la misma rejilla.
Un anillo puro (`pre_start_history`) guarda la racha continua de máquina lista;
se vacía en cada cambio de estado o tras más de 1 s sin observarla. Al abrir la
preparación, la sesión copia ese minuto si su origen, medido o simulado,
coincide con el de la preparación. Si hay menos de un minuto, se envía lo que
haya, sin rellenar. Esas muestras no cuentan en `sampleCount` ni en los
indicadores: quien calcule algo de la preparación filtra `elapsedMs ≥ 0`.

Se descartó leer el anillo al cerrar, porque la evidencia dependería del estado
vivo de otra fase. También se descartó interpolar la rejilla al instante de
pulsar, porque fabricaría valores no medidos. Con el minuto previo, los tramos
pasan a nueve muestras para caber en 1536 bytes, un café de 30 s ocupa 20 tramos
en vez de 6 y la RAM estática crece 3,9 KB.

## 5. Justificación

1. El transporte y la cola funcionan; sustituirlos no resolvería nada.
2. La cola existe para no perder lo ocurrido; las muestras efímeras la
   agotarían.
3. Para quien mira la pantalla, una muestra por segundo basta. La regulación es
   local.
4. Una dirección compilada convierte cada cambio de red en una intervención
   sobre el controlador.
5. Una credencial propia del dispositivo se revoca sin afectar a ninguna
   persona.

## 6. Consecuencias

La plataforma muestra el estado durante la preparación sin servicios nuevos. La
vista en vivo no es un registro continuo, y la plataforma debe declarar la
antigüedad del último estado conocido.

La confirmación dice «lo tengo», no «es nuevo»: el dispositivo no distingue un
reenvío de una colisión de identificadores. Por eso la unicidad del
identificador es obligación del dispositivo, que conserva su contador entre
arranques. El identificador no incluye el del equipo, así que dos dispositivos
colisionarían; el sistema tiene uno.

## 7. Verificación

1. Una extracción se entrega con confirmación y sin duplicados.
2. La aplicación consulta el estado durante una preparación.
3. Tras una desconexión, la evidencia llega completa al volver el enlace.
4. La cola conserva su capacidad tras una preparación con telemetría.
5. La dirección cambia sin regrabar el firmware.
6. La cafetera opera con la plataforma apagada.

Los puntos 1, 2, 3, 5 y 6 se verificaron con el dispositivo real. Con la
plataforma inalcanzable la máquina preparó café y, al devolver la dirección,
llegaron los siete registros pendientes; una preparación sin enlace llegó con
sus 54 muestras. El punto 4 no se ha comprobado de forma explícita.

## 8. Criterios de reconsideración

Se revisa si llegan más dispositivos o consumidores del flujo en vivo, si la
cadencia no basta o excede la plataforma, o si la consulta de estado exige
continuidad garantizada.

Trazabilidad: ADR-001 § 6, ADR-002, ADR-003 § 6.
