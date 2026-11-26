# ADR-019 — Temperatura del agua mediante una transformación medida

**Estado:** Aceptada (2026-09-30), reemplazada en parte. Desde el 2026-10-18
la relación entre el agua y la caldera la fija el acta de temperatura y la
aplica el firmware (ADR-009, enmienda del 2026-10-18). La corrección después de
cada café, la única que usaba la transformación con café, se retiró el
2026-10-22 (ADR-018 § 13). Sigue vigente el registro de lecturas de referencia
emparejadas con la temperatura del cuerpo (§ 4.3).

## 1. Contexto

[ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md) sostiene un
objetivo expresado como temperatura del agua, pero ningún sensor la mide. El NTC
está en el cuerpo de aluminio de la caldera, y la diferencia entre cuerpo y agua
cambia con el régimen: crece en el calentamiento, cambia de signo cuando entra
agua fría y se reduce en reposo.

## 3. Alternativas descartadas

| Alternativa | Razón |
|---|---|
| Invertir el modelo de dos nodos | Con una sola señal no es identificable: la conductancia interna varía entre 6,3 y 35 W/K, y la fracción del sensor entre 0,08 y 0,45, con el mismo error de ajuste. |
| Observador de estado o filtro de Kalman | Hereda el problema anterior. |
| Diferencia fija de la literatura o de otra máquina | No hay fuente para esta caldera; sería un número inventado. |
| Sensor en el agua | Exige intervenir la caldera y el grupo. |

## 4. Decisión

La temperatura del agua sale de una transformación ajustada sobre pares medidos
de temperatura del cuerpo y temperatura de referencia.

### 4.1 Qué se estima

La referencia es el **agua a la salida del pico del portafiltro sin
canastilla**, medida con un NTC fijado en el chorro y el portafiltro caliente.
Se mide sin intervenir la máquina, con ±2 °C de calibración entre 27 y 95 °C.
Se descartaron el agua en un recipiente, que mide el equilibrio con la loza
(66,6 °C con el cuerpo en 92,4 °C); el sensor sostenido a mano, que osciló entre
64 y 80 °C; una taza precalentada; y la ducha sin portafiltro, sin punto fijo.

**Con café, la referencia es el café a la salida del pico**, con la misma
canastilla y dosis, una magnitud distinta del agua. La transformación sin
canastilla, `campana1-v1`, no vale con café: a los 20 s predice 74,6 y 79,6 °C
donde el NTC marcó 81,9 y 82,1 °C, y al terminar el cuerpo está en unos
74,5 °C, fuera de su ámbito de «después» (90,8–91,4 °C). La de café se ajusta
con el depósito a tres temperaturas. `campana1-v1` sigue valiendo para el
reposo y el purgado sin canastilla.

### 4.2 De dónde sale

De una **transformación versionada** del cuerpo al agua de salida. Cada versión
declara un identificador inmutable, la relación y sus coeficientes, los pares
con que se ajustó, su ámbito de validez, el error del ajuste y sobre pares no
usados, y sus autores y fecha. La forma de la relación se decide con los pares
delante; sin ellos habría que adivinarla. La temperatura del cuerpo se registra
tal como se midió, y la del agua se obtiene aparte, con la versión que la
produjo.

### 4.3 Qué se registra en cada lectura de referencia

Cada lectura guarda dispositivo, instante, temperatura de referencia,
instrumento, condición (reposo, después o durante una extracción) y
preparación, obligatoria durante una extracción.

La temperatura del cuerpo no se teclea: se toma de la muestra del mismo
dispositivo más cercana al instante, si dista como mucho 1 s. Fuera de la
preparación el dispositivo publica cada 15 s
(`CAFE_TELEMETRY_IDLE_INTERVAL_MS`), así que durante la toma de pares ese
periodo se iguala a 1 s. No se ensancha el margen porque, justo después de una
extracción, el cuerpo cambia deprisa.

La lectura se empareja mientras se toma y copia la temperatura del cuerpo,
porque la telemetría se sustituye después por la serie. Sin muestra dentro del margen, la lectura se conserva sin
pareja y con la causa. Una muestra no apta se empareja y queda marcada.

### 4.4 Qué vale mientras no exista

Sin una versión aplicable, o fuera de su ámbito, la temperatura del agua es
**no disponible**. No se sustituye por la del cuerpo ni por el nodo del modelo,
porque el sostenimiento corregiría hacia un dato inexistente. Así, ADR-018 no
corrige temperatura (ADR-018 § 4.6, condición 4).

### 4.5 Dónde vive

En la plataforma: el módulo puro `espresso_simulation.water_temperature` aplica
la transformación y `POST /api/v1/water-references` registra las lecturas. El
firmware no la conoce; el PI regula la temperatura del cuerpo y el modelo queda
fuera del lazo.

## 6. Consecuencias

### 6.2 Consecuencias desfavorables

- Sin transformación, el sostenimiento no corrige temperatura.
- El umbral de 2,8 °C de ADR-018 es provisional, porque se calculó sobre el nodo
  no identificado.
- Lectura y telemetría deben estar en el mismo reloj. Por eso la plataforma
  sella el instante al recibir la lectura, y el dispositivo toma la hora de la
  plataforma (ADR-013). Con 1 s de margen, el desfase del navegador bastaría
  para emparejar mal; su hora se guarda aparte.
- La transformación con café no llegó a ajustarse: la corrección después de
  cada café, que era su único uso, se retiró antes (ADR-018 § 13).

## 9. Verificación de la decisión

Se comprueba que la temperatura del cuerpo no se acepta desde la petición y que
se cumplen los casos de § 4.3 y § 4.4.

## 10. Trazabilidad

[ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md) § 4.1, § 4.6 y
§ 4.7; [ADR-013](ADR-013-fuente-de-hora-y-fechado-de-la-evidencia.md).

## 11. Criterios de reconsideración

Se revisa si la relación depende de variables no registradas, si se instala un
sensor en el agua o si una segunda señal hace identificable el modelo.
