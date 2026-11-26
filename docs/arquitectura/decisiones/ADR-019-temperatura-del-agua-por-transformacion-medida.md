# ADR-019 — Temperatura del agua mediante una transformación medida

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-30
**Fecha de aprobación:** 2026-09-30
**Ámbito:** cómo se obtiene la temperatura del agua a partir de la del cuerpo de la caldera, qué se registra para establecerla y qué vale mientras no exista

## 1. Contexto

[ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md) sostiene un
objetivo expresado como temperatura del agua. Ningún sensor la mide: el NTC está
en el cuerpo de aluminio de la caldera, y entre ambas magnitudes hay una
diferencia que cambia con el régimen. Crece durante el calentamiento, cambia de
signo cuando entra agua fría en la extracción y se reduce en reposo
(plan experimental § 5).

### 1.1 Situación comprobada

| Elemento | Situación |
|---|---|
| Sensor de temperatura | NTC en el cuerpo de la caldera, leído por la ESP32-S3 |
| Telemetría | Con instante, dispositivo, calidad y origen. Una muestra por segundo durante la preparación y una cada 15 s fuera de ella (`CAFE_TELEMETRY_ACTIVE_INTERVAL_MS` y `CAFE_TELEMETRY_IDLE_INTERVAL_MS`) |
| Modelo de dos nodos | Tiene un nodo «caliente» que representa el agua, pero **nunca se midió** |
| Identificabilidad | Con una sola señal, la conductancia interna varía entre 6,3 y 35 W/K, y la fracción del sensor entre 0,08 y 0,45, entre ajustes que los datos no distinguen (procedencia de las ecuaciones, P-04 bis) |
| Pares cuerpo–agua medidos | **Ninguno.** La campaña 1 del plan experimental los prevé, y exige las placas montadas |
| Lugar donde registrar una lectura de referencia | No existía; se añade con esta decisión |

## 2. Problema de decisión

Se debe definir de dónde sale la temperatura del agua que usa el sostenimiento,
qué evidencia hace falta para establecerla, cómo se registra esa evidencia y
qué vale la temperatura del agua mientras no exista.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Invertir el modelo de dos nodos a partir de la temperatura medida | No requiere ningún ensayo, pero la diferencia entre cuerpo y agua depende de la conductancia interna, que puede ser cinco veces mayor o menor con el mismo error de ajuste. Presentaría como temperatura del agua un número que ningún dato respalda. |
| Observador de estado o filtro de Kalman sobre el mismo modelo | Hereda el problema anterior y añade covarianzas de ruido que nadie ha medido. |
| Diferencia fija tomada de la literatura o de otra máquina | No hay fuente para esta caldera. Sería un número inventado. |
| Sensor adicional en el agua | Mediría directamente, pero exige intervenir la caldera y el grupo, fuera del alcance del prototipo. |
| Transformación ajustada sobre pares medidos de cuerpo y agua de salida | Usa la única evidencia que puede establecer la relación, y es la que el plan experimental ya prevé. **Seleccionada.** |

## 4. Decisión

### 4.1 Qué se estima

La **temperatura del agua de salida**: la del agua que sale del grupo, medida
con un termómetro de referencia en el recipiente. Es la magnitud que puede
medirse sin intervenir la máquina
(matriz de mediciones),
y la más cercana a la temperatura sobre el café que admite el prototipo. La
monografía debe nombrarla así.

**Enmienda del 2026-10-13 — la referencia, bajo el pico.** La medida en el
recipiente no sirvió. Con la sonda de la placa auxiliar en una taza, la
lectura tardó un minuto en estabilizarse y lo hizo en el equilibrio del agua
con la loza (66,6 °C con el cuerpo en 92,4); en el chorro no pasó de 39 °C en
los 10 s del purgado. El NTC de la placa auxiliar responde en 1–2 s, pero
sostenido a mano bajo la ducha la lectura osciló entre 64 y 80 °C y salpicó
agua casi hirviendo sobre el operador
(evidencia).

La referencia pasa a ser el **agua a la salida del pico del portafiltro sin
canastilla**, medida con el NTC **fijado** en el chorro, sin ninguna mano en
él. El portafiltro va puesto durante el reposo, para que el metal llegue
caliente y enfríe lo menos posible el agua. Sigue midiéndose sin intervenir la
máquina, que es la razón de esta sección. Se descartaron la taza precalentada,
porque el resultado dependería de cuánto se precalentara, y la ducha sin
portafiltro, porque reparte el agua en chorros finos sin un punto fijo donde
poner el sensor. Lo que se estima es, por tanto, la temperatura del agua a la
salida del pico, y la monografía debe nombrarla así. El NTC tiene la
calibración del 2026-10-11, con ±2 °C entre 27 y 95 °C; esa incertidumbre
acompaña a cada par.

**Enmienda del 2026-10-14 — con café, la referencia es el café a la salida
del pico.** Decidida con el operador el 2026-10-14, antes de medir.

Las cuatro extracciones con café de la 7.9
(evidencia)
mostraron que `campana1-v1` no vale en la condición de uso:
- **El caudal es otro.** Con 14 g en la canastilla presurizada, la bomba al
  47–51 % y 8,5 bar, salen unos 2,1–2,2 g/s, no el flujo libre de la campaña 1.
- **La fórmula se queda corta.** A los 20 s, la recta «durante» predice 74,6 y
  79,6 °C, y el NTC bajo el pico marcó 81,9 y 82,1.
- **El cuerpo queda fuera del ámbito.** Al terminar una extracción con café,
  el cuerpo está en unos 74,5 °C, fuera del de «después» (90,8–91,4). El
  sostenimiento evalúa con la última muestra del cuerpo de la extracción
  (ADR-018; `sustainment.py`, `evaluate`), así que con café real recibiría
  siempre «agua no disponible» y no corregiría nunca.

Por eso, **para las preparaciones con café la referencia es la temperatura
del café a la salida del pico**:
- se mide con el mismo NTC fijado y la misma canastilla y dosis que la
  preparación;
- es lo más cercano a «el agua sobre el café» (ADR-018 § 4.1) que puede
  medirse sin intervenir la máquina;
- es una magnitud distinta del agua, porque ya pasó por la pastilla, y la
  monografía debe nombrarla así.

La transformación de esa condición se ajusta con la campaña 2 (guion 8.5):
café con el depósito a tres temperaturas, registrado por el DS18B20
sumergido.

`campana1-v1` no se retira: sigue valiendo para el reposo y el purgado sin
canastilla, en su ámbito.

Queda pendiente, y es trabajo del hilo de la plataforma, que el sostenimiento
pida a la transformación la condición de café y no la de «después» sin
canastilla.

### 4.2 De dónde sale

De una **transformación versionada** que convierte la temperatura del cuerpo en
temperatura del agua de salida, ajustada sobre pares medidos. Cada versión
declara:

| Campo | Contenido |
|---|---|
| Versión | Identificador inmutable |
| Forma | La relación ajustada y sus coeficientes |
| Evidencia | Los pares con los que se ajustó, por identificador |
| Ámbito de validez | Condiciones de la máquina y rango de temperatura del cuerpo que cubren esos pares |
| Error | Residuo del ajuste sobre los pares y, cuando existan, sobre pares no usados en él |
| Autores y fecha | Quién la estableció |

**La forma de la relación se decide con los datos, no antes.** Hoy no se sabe si
la diferencia es casi constante, si depende del tiempo de extracción o si cambia
con el reposo; fijar una forma sin pares obligaría a adivinarla.

La transformación nunca se aplica como un ajuste del coeficiente del sensor. La
temperatura del cuerpo sigue registrándose tal como se midió, y la del agua se
obtiene aparte, con la versión que la produjo (plan experimental § 5).

### 4.3 Qué se registra en cada lectura de referencia

| Campo | Origen |
|---|---|
| Dispositivo | Declarado por quien mide |
| Instante de la lectura | **Lo sella la plataforma al recibirla**; la hora del navegador se guarda aparte, con su desfase |
| Temperatura del agua de salida | Leída en el termómetro |
| Instrumento | Declarado por quien mide |
| Condición | Reposo, justo después de una extracción o durante una extracción |
| Preparación | Opcional; obligatoria si la condición es «durante una extracción» |
| Temperatura del cuerpo en ese instante | **Se toma de la serie del dispositivo, no se teclea** |

La temperatura del cuerpo se deriva de la muestra de telemetría del mismo
dispositivo más cercana al instante sellado, siempre que diste como mucho un
periodo de telemetría (1 s). Escribirla a mano la separaría de la evidencia que
la respalda y abriría la puerta a transcribirla mal.

Fuera de la preparación el dispositivo publica una muestra cada 15 s, y con ese
periodo las lecturas en reposo y justo después de una extracción quedarían casi
siempre sin pareja. **Durante la campaña 1, el periodo fuera de la preparación
se iguala al de la preparación (1 s).** Se prefirió a ensanchar el margen porque
justo después de una extracción la temperatura del cuerpo cambia deprisa, y
emparejar con una muestra de varios segundos antes registraría una diferencia
que no existió.

**La lectura se registra mientras se toma, y la pareja copia la temperatura del
cuerpo.** La telemetría de una preparación se sustituye al cerrarla por su serie,
cuyas muestras no tienen instante propio. Una lectura emparejada después ya no
encontraría con qué, y una que solo guardara el identificador de la muestra
perdería la evidencia al sustituirse.

**El instante lo sella la plataforma, no quien mide** (cambio del 2026-10-12).
La telemetría la fecha la Raspberry con su reloj, y un instante tomado del
navegador traería el desfase de ese otro reloj: con un margen de 1 s, unos
segundos de diferencia bastan para emparejar con la muestra equivocada o con
ninguna. Sellar al recibir pone las dos series en el mismo reloj; la hora del
navegador se conserva aparte con su desfase, como dato de la lectura y no como
su instante.

Si no hay muestra dentro de ese margen, la lectura se conserva y queda **sin
pareja**, con la causa. Si la muestra existe pero su calidad no es apta, se
empareja igual y queda marcada: no se sustituye por otra ni se descarta. Quien
ajuste la transformación decide qué pares usa.

### 4.4 Qué vale mientras no exista

Mientras no exista una versión aplicable, la temperatura del agua es **no
disponible**. Tampoco lo es fuera del ámbito de validez de la versión vigente.

No se sustituye por la temperatura del cuerpo ni por el nodo caliente del
modelo. Un valor por omisión que afirmara una temperatura del agua convertiría
un dato inexistente en evidencia, y el sostenimiento corregiría hacia él.

Con la temperatura del agua no disponible, el sostenimiento de ADR-018 no
corrige temperatura (ADR-018 § 4.6, condición 4).

### 4.5 Dónde vive

En la plataforma. La aplicación de la transformación es un módulo puro de
`platform/packages/espresso_simulation`, sin aplicación web ni almacenamiento, probado
por separado; el registro de las lecturas de referencia, en la aplicación. El
firmware no la conoce: el PI sigue regulando la temperatura del cuerpo, que es
lo que mide, y el modelo sigue fuera del lazo (RD-004).

## 5. Justificación

1. **Es la única evidencia que puede establecer la relación.** El modelo no la
   determina con una sola señal, y cualquier cifra sin pares medidos sería
   inventada.
2. **Separa lo medido de lo inferido.** La temperatura del cuerpo queda intacta
   y la del agua declara siempre de qué versión salió.
3. **El estado no disponible es el conservador.** Sin transformación, el
   sistema no corrige hacia un número que nadie ha comprobado.
4. **Todo lo que no depende de los datos puede construirse ya.** El registro de
   lecturas y el estado no disponible están listos el día de la campaña 1, y la
   forma de la relación se decide una sola vez, con los pares delante.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- El objetivo de temperatura de ADR-018 pasa a tener una magnitud medible y
  trazable.
- La campaña 1 produce evidencia utilizable sin tratamiento posterior, porque
  cada lectura queda emparejada con su muestra al registrarse.
- La transformación mejora con cada campaña sin reescribir lo anterior: cada
  versión cita sus pares.

### 6.2 Consecuencias desfavorables

- Hasta la campaña 1 el sostenimiento no corrige temperatura, y el sistema no
  alcanza en la práctica la clasificación de gemelo digital en ese ámbito.
- El umbral de 2,8 °C de ADR-018 queda provisional, porque se calculó sobre el
  nodo no identificado.
- La lectura de referencia depende de que el reloj del dispositivo y el de quien
  mide coincidan; el margen de un periodo lo absorbe solo si la hora se toma de
  la plataforma (ADR-013).

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Emparejar la lectura con una muestra de otro momento | Margen de un periodo de telemetría; fuera de él, la lectura queda sin pareja y con su causa |
| Aplicar la transformación fuera de lo que sus pares cubren | Ámbito de validez declarado; fuera de él, no disponible |
| Ajustar una forma equivocada | La forma se elige con los datos, y el error sobre pares no usados en el ajuste queda declarado |
| Sustituir en silencio la temperatura del agua por la del cuerpo | Prohibido por § 4.4; la ausencia se declara como no disponible |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| ~~Registro de lecturas de referencia en la plataforma~~ | **Implementado el 2026-09-30**: `POST /api/v1/water-references` |
| ~~Estado no disponible en el módulo puro~~ | **Implementado el 2026-09-30**: `espresso_simulation.water_temperature` |
| Pantalla para registrar lecturas durante la campaña | Implementación en la interfaz |
| Campaña 1 | Placas montadas y puesta en servicio (plan experimental § 5), con `CAFE_TELEMETRY_IDLE_INTERVAL_MS` en 1000 durante la campaña. El cambio se hace editando `sdkconfig` a mano, nunca regenerándolo, porque guarda las credenciales |
| Forma de la relación y primera versión | Ajuste sobre los pares de la campaña 1 |
| Recalcular el umbral de ADR-018 | Primera versión de la transformación |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. una lectura de referencia se empareja con la muestra del mismo dispositivo
   más cercana dentro de un periodo, y la temperatura del cuerpo no se acepta
   desde la petición;
2. una lectura sin muestra dentro del margen se conserva sin pareja y con su
   causa;
3. una muestra de calidad no apta se empareja y queda marcada, sin
   sustituirse;
4. sin transformación, la temperatura del agua es no disponible y nunca igual
   a la del cuerpo;
5. fuera del ámbito de validez de la versión vigente, la temperatura del agua es
   no disponible.

## 10. Trazabilidad

- [ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md) § 4.1, § 4.6 y
  § 4.7: objetivo expresado como temperatura del agua y condición que impide
  corregir sin ella.
- [ADR-013](ADR-013-fuente-de-hora-y-fechado-de-la-evidencia.md): fuente de hora
  del dispositivo.
- Plan experimental § 5: campaña 1.
- Procedencia de las ecuaciones,
  P-04 bis: identificabilidad del modelo de dos nodos.
- Requisitos: RF-030 y RD-004.

## 11. Criterios de reconsideración

La decisión deberá revisarse si los pares de la campaña 1 muestran que la
relación depende de variables que el sistema no registra, si se instala un
sensor en el agua, o si una segunda señal de temperatura hace identificable el
modelo de dos nodos.
