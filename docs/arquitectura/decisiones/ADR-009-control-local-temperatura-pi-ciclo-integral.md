# ADR-009 — Control local de temperatura mediante PI unilateral y modulación por ciclo integral

**Estado:** Aceptada, revisada
**Fecha de redacción:** 2026-09-14
**Fecha de aprobación:** 2026-09-15
**Última revisión:** 2026-09-21
**Ámbito:** regulación local de temperatura, actuación del calentador, protección térmica y puesta en marcha

## 1. Contexto

ADR-005 establece que la regulación térmica forma parte del comportamiento
normal de la cafetera y actúa en calentamiento, preparada, extracción, purga y
vapor, sin adquirir autoridad para cambiar referencias ni iniciar nada. ADR-007
cerró el algoritmo y la actuación del lazo de presión. **No existe una decisión
equivalente para el calentador**, y esa ausencia es hoy la única pieza del
control que falta decidir y no implementar.

El hallazgo H-13 del informe de hallazgos del firmware la identificó como tal.
Escribir el lazo sin esta decisión obligaría a tomarla implícitamente en el
código, que es donde peor se discute.

### 1.1 Lo que ya está determinado por el hardware

| Elemento | Dato | Fuente |
|---|---|---|
| Caldera | `CM8502`, aluminio, dos mitades atornilladas, pared mínima 2,8 mm | informe constructivo Xinbao |
| Volumen interno | ≈ 152 mL | geometría medida |
| Volumen útil de agua | 81 ± 3 mL | llenado y pesaje, 2026-10-06; derivados revisados el 2026-10-07 (§ 3.5) |
| Calentador | 1050 W a 120 V AC, tubular moldeado en el cuerpo | informe constructivo |
| Densidad de potencia | ≈ 6,9 W/mL; **≈ 12,7 W/mL** con lo medido (2026-10-07) | derivado |
| Conmutación | SSR-25DA **de cruce por cero**, driver `2N2222` desde `HEATER_CMD` | ADR-008 § 3.1 |
| Protección física | dos `MICROTEMP G4A00`, `Tf 192 °C`, no reajustables | inspección de la unidad |
| Sensor | NTC original sobre el **cuerpo** del boiler; `R25 = 100 kΩ` y `β = 3950 K` provisionales | caracterización |

Tres consecuencias de esa tabla condicionan la decisión antes de discutir
algoritmos:

**El control por ángulo de fase queda descartado por construcción.** Un SSR de
cruce por cero solo conmuta en el paso por cero de la onda: la única modulación
posible aplica u omite ciclos completos de red.

**El calentador no necesita `PUMP_ZC`.** El SSR lleva su propia detección, de
modo que la ruta térmica no comparte con la bomba la dependencia de una rutina
de interrupción. Las dos actuaciones no se parecen tanto como sugiere el
diagrama de bloques.

**Las protecciones térmicas son de un solo uso.** Los `G4A00` son fusibles, no
termostatos: si actúan, quedan abiertos permanentemente y recuperarlos exige
abrir la caldera. La protección por software no los duplica; existe para que
nunca lleguen a actuar.

### 1.2 El sensor no mide lo que el barista cree que mide

La sonda está montada sobre el cuerpo de aluminio y mide **la temperatura del
metal, no la del agua**. Entre ambas hay una diferencia que depende del régimen:
crece durante el calentamiento, cambia de signo cuando entra agua fría en la
extracción y se reduce en reposo.

Esto no es un detalle de instrumentación, sino una condición de significado. Una
referencia de 93 °C sobre el cuerpo no equivale a 93 °C de agua, y una tolerancia
declarada sobre una magnitud no puede evaluarse sobre la otra.

## 2. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Todo-nada con histéresis | Es lo que hace la cafetera original y lo que modela hoy la planta simulada. Simple y robusto, pero con 6,9 W/mL y el retardo entre cuerpo y agua produce sobrepasos y un rizado que no puede acotarse por diseño. No se adopta. |
| Control por ángulo de fase | Imposible con el SSR adquirido. Cambiarlo introduciría cortes parciales de onda, peor EMC y una cadena de potencia distinta a la ya aprobada. No se considera. |
| PID completo | La acción derivativa amplificaría el ruido y, sobre todo, actuaría sobre el nodo rápido —el metal— que no es la magnitud de interés. Se pospone. |
| Prealimentación desde el caudal | **Aceptada como evolución, no como punto de partida.** Es la señal correcta —la perturbación vale `ṁ·cp·ΔT`— y desde la revisión de ADR-006 del 2026-09-14 el proyecto sí medirá caudal, con celda de carga y HX711 durante la caracterización. Se recoge en § 3.4 como `ff-v2`, detrás de una aproximación disponible antes (`ff-v1`). |
| **PI unilateral con modulación por ciclo integral** | **Seleccionada.** Elimina el error estacionario, permite acotar explícitamente la actuación y respeta la única modulación que el SSR admite. |

## 3. Decisión

### 3.1 Frontera del lazo

```text
NTC-C8 → divisor y filtro → ADS1115 → validación → filtro digital
                                                    ↓
referencia protegida → limitador de cambio → PI unilateral → ciclo integral → SSR → calentador
                                                    ↑
                          estado operativo y guardián de actuación
```

La referencia procede exclusivamente de la configuración efectiva ya validada.
El controlador no elige perfiles, no cambia referencias, no inicia extracciones
y no decide terminaciones. Su única salida es la demanda térmica.

### 3.2 La magnitud regulada se declara, no se supone

Mientras no exista una correspondencia establecida entre la temperatura del
cuerpo y la del agua, **la referencia, la tolerancia y todo indicador de
estabilidad se declaran sobre la temperatura del cuerpo del boiler**, y así se
nombran en la configuración, en el registro y en la interfaz.

Establecer esa correspondencia es un trabajo experimental propio, con el
protocolo de la matriz de mediciones, y no una constante que pueda elegirse. Si
se establece, se registrará como una transformación versionada y trazable, nunca
como un ajuste silencioso del coeficiente del sensor.

#### Enmienda del 2026-10-18 — el perfil pide el agua y la placa calcula la caldera

**Se establece la correspondencia** para el agua que cae sobre el café, no
para el agua de la caldera: media del chorro entre 5 y 19 s con el NTC fijo en
el portafiltro montado, agua sola, en 88–100 °C de caldera. Resultado:
agua ≈ 0,92 · caldera + 0,3 °C, residuos ≤ 0,26 °C (evidencia
`temperatura-cierre-2026-10-17.md`, «Curva caldera → agua sobre el café»).

**Se decide (operador, 2026-10-18)** que la temperatura del perfil sea la del
agua y que la placa calcule la referencia de caldera invirtiendo la recta. El
lazo, la banda de «lista», el indicador verde y el límite duro siguen sobre el
cuerpo: solo cambia de dónde sale la referencia. Es la transformación
versionada que pide este apartado: los cuatro números (pendiente, ordenada e
intervalo de caldera medido) van en el acta de temperatura, sección
`water_reference`, y el firmware los lee del archivo generado.

**Recta contra el metal, no contra la referencia (mismo día).** La v5
(`acta-temperatura-20261018`) llevaba la recta contra la referencia y no pasó
la comprobación: con 99,67 °C el agua salió a 90,6 °C de media, porque lo que
el metal se pasa de la referencia al pulsar cambia entre sesiones (1,7–2,5 °C
en la curva, 0,4–1,3 °C en la comprobación). La v6 (`acta-temperatura-20261018b`)
usa agua = 0,8063 · metal al pulsar + 9,84 °C (rms 0,55 °C), que predijo esos
tres usos con −0,3 °C, y la aplica a la referencia suponiendo el metal en ella
al pulsar. Vive en `water_reference` (módulo puro); la aplican la
activación del perfil (`profile_runtime`), el supervisor y la planta simulada.

**Sin la sección, nada cambia:** la temperatura del perfil vuelve a ser la de
la caldera, para que un acta anterior siga significando lo que significaba al
firmarse.

**Fuera de lo medido se rechaza.** El agua que pediría un metal fuera de
88,60–102,49 °C (81,28–92,48 °C con la v6) no se activa y la placa responde
`temperature-out-of-range`. No se extrapola: la recta no se comprobó fuera, y
desde 96 °C el máximo del chorro ya pasa del hervor local.

> **Descartadas.** Hacer la cuenta en la plataforma (primera opción del
> operador, corregida el mismo día): la placa es la que regula y valida, y
> así la referencia significa lo mismo venga el perfil de donde venga. Mantener la
> recta contra la referencia: es menos dispersa en la curva, pero falló al
> comprobarla en otra sesión. Corregir el coeficiente del sensor: lo prohíbe este apartado.

**No cubre el café.** Con café la temperatura sobre la pastilla depende de la
molienda (a 44 clics el NTC leyó hasta 9 °C menos que con agua sola).

#### Enmienda del 2026-10-19 — compensación del primer uso

**El problema, medido.** La recta de la enmienda anterior supone el metal en la
referencia al pulsar, y en uso seguido acierta. Tras un reposo no: con reposos
de 20 min a 4 h, el primer uso sale 2,4–3,9 °C más frío (media del chorro
entre 5 y 19 s) que en uso continuo; tras 10 min, no sale frío. Desde el
segundo uso vuelve a lo normal (evidencia `temperatura-cierre-2026-10-17.md`,
«Ensayo de reposo» y su segunda parte). El agua sigue al metal al pulsar con
unos 0,81 °C por °C (acta v6), así que subir la referencia de caldera antes de
ese uso lo compensa.

**Se decide (usuario, 2026-10-19)** que **el gemelo de la plataforma prediga el
déficit y pida, solo para la próxima preparación, un incremento de la
referencia de caldera; la placa lo valida y lo aplica.** La placa no predice
nada.

- **Contrato.** Campo opcional `boilerOffsetC` del perfil (°C, ≥ 0). Sin él,
  cero: nada cambia. Se suma después de convertir el agua a caldera, así que
  no lo limita el intervalo de la recta sino el máximo y el límite duro.
- **Validación.** La activación del perfil (`profile_runtime`) lo rechaza con
  causa `boiler-offset-out-of-range` si es negativo, no finito, mayor que el
  máximo, o si la referencia subida no deja la banda de «lista» bajo el límite
  duro (la misma desigualdad de `process_limits_are_valid`). No se recorta: el
  perfil entero se rechaza.
- **Un solo uso.** Rige desde que se adopta hasta que termina la siguiente
  preparación que empiece después, termine como termine; luego vuelve a cero
  aunque el perfil siga pidiéndolo. Los purgados no lo consumen. La misma
  petición repetida no lo renueva: la placa cuenta como petición nueva un
  cambio de identificador, de versión o del valor pedido. No se guarda en la
  memoria no volátil: tras un reinicio no rige, y la plataforma puede volver a
  pedirlo. Vive en `first_use_offset` (módulo puro); lo aplica el supervisor.
- **«Lista» contra la referencia subida.** Calentando, el supervisor ya
  declara «lista» contra la referencia vigente. Con la máquina ya lista, la
  histéresis de 2 °C la dejaría así con la caldera todavía por debajo; por eso,
  en el ciclo en que la referencia sube, si la caldera está bajo referencia −
  banda se publica `coffee-temperature-lost` y vuelve a calentar
  (`process_supervisor_ready_withdrawn`).
- **Registro.** La línea de límites del perfil dice el desplazamiento aplicado
  y que vuelve a cero al terminar la próxima preparación, y otra línea dice
  cuándo volvió. El resumen de la preparación lleva `boilerOffsetAppliedC`
  (opcional; ausente si no consta, como en una preparación recuperada tras un
  corte). La planta simulada recibe la misma referencia subida.

**El máximo sale de lo medido.** Sección opcional `first_use_offset` del acta
de temperatura, con `max_offset_c` y `method`; sin ella el máximo es cero y la
placa rechaza cualquier desplazamiento. Es un parámetro reservado a la puesta
en servicio: su valor sale del ensayo que mide la compensación, no de una
cifra plausible. Para ese ensayo, que se hace antes de existir la sección, el
binario de identificación admite un máximo provisional
(`CAFE_FIRST_USE_OFFSET_PROVISIONAL` y `CAFE_FIRST_USE_OFFSET_MAX_DECI_C`,
apagado por omisión y con cero por omisión), que manda sobre el acta y se
avisa en el registro, igual que el arranque rápido provisional.

**Lo que no hace.** No predice: no sabe cuánto lleva la máquina en reposo ni
cuándo pulsará el barista. No compensa los usos siguientes. No extrapola: el
máximo lo da lo medido. No caduca: si el barista tarda, la caldera espera más
arriba, siempre bajo el límite duro y con el guardián decidiendo antes.

> **Descartadas.** Subirla en la placa según el tiempo de reposo: la placa no
> sabe cuándo pulsará el barista, y el modelo del reposo vive en el gemelo; un
> modelo en la placa la convertiría en predictora y duplicaría el gemelo.
> Purgar antes del primer café: gasta agua y depende de que el barista lo
> haga.

#### Enmienda del 2026-10-27 — refuerzo de la prealimentación en el primer uso

> **Estado: decidida por el usuario el 2026-10-27.** Por programar; rige en la
> placa cuando exista el acta con su máximo y un binario que la contenga.

**El problema, medido.** La subida de la enmienda anterior actúa antes de
pulsar, y el déficit del primer uso está en la segunda mitad del chorro: desde
los 10 s el agua cae hasta unos 78 °C tras reposos de 25 y 45 min, contra unos
86 °C en uso continuo. Con la prealimentación del acta (0,75) el calentador va
al 60–64 % durante el café. Con 1,25, en un binario de ensayo, va al 98–99 %.
En bloques cruzados de 25 min (sin, con, con, sin), con la caldera a
101,8–102,0 °C al pulsar en los cuatro, el déficit del primer uso pasa de 3,35 y
3,14 °C a 0,02 y 0,09 °C; con el mismo refuerzo queda −0,28 °C tras 15 min y
0,90 °C tras 45 min (`model/Documentacion/agua-portafiltro.md`,
§ 10.25–10.26).

**Se decide (usuario, 2026-10-27)** que **el gemelo de la plataforma pida, solo
para la próxima preparación, una ganancia de prealimentación mayor; la placa la
valida y la aplica durante ese café.** Es la misma forma que la subida: la
plataforma decide, la placa no predice.

- **Contrato.** Campo opcional `firstUseFeedforwardGain` del perfil, en % de
  calentador por % de bomba, como `ff-v1`. Sin él, la ganancia del acta: nada
  cambia. Es independiente de `boilerOffsetC`: la plataforma puede pedir uno,
  otro o los dos.
- **Validación.** La activación del perfil lo rechaza con causa
  `first-use-feedforward-out-of-range` si no es finito, si es menor que la
  ganancia del acta o mayor que el máximo del acta. No se recorta: el perfil
  entero se rechaza, igual que la subida.
- **Un solo uso.** El mismo ciclo que la subida: rige desde que se adopta hasta
  que termina la siguiente preparación que empiece después, y vuelve a la del
  acta aunque el perfil siga pidiéndolo. Los purgados no lo consumen. No se
  guarda en la memoria no volátil.
- **Lo demás de `ff-v1` sigue igual.** Solo cambia la ganancia: la aportación
  sigue siendo proporcional a la densidad, se corta sin agua (enmienda del
  2026-10-17) y la saturación al 100 % la limita.
- **Registro.** La línea de límites del perfil dice la ganancia adoptada, y el
  resumen de la preparación lleva `firstUseFeedforwardGainApplied` (opcional;
  ausente si no consta).

**El máximo sale de lo medido.** Campo opcional `max_feedforward_gain` de la
sección `first_use_offset` del acta, mayor que `feedforward.gain`, con lo que
dice de dónde sale en `method`. Sin él la placa solo admite la ganancia del
acta.

**Lo que se sabe que cuesta.** El calor que queda en el elemento sube la caldera
a 105–108 °C un minuto después del café, contra unos 101 °C sin refuerzo, y el
café siguiente sale cerca de 1 °C más frío porque la caldera llega más baja al
pulsarlo. Por eso es de un solo uso.

**Lo que no hace.** No decide cuándo: eso es del gemelo. No cambia la potencia
según lo que mide durante el café: el calor del elemento tarda más que la
extracción, y el realimentado ya llega tarde por construcción.

> **Descartadas.** Reforzar todos los cafés: los seguidos salen más fríos.
> Cortar el refuerzo antes del final para que la caldera se pase menos: no se
> ha medido, y entraría con su propio ensayo. Atar el refuerzo a la subida, sin
> campo propio: lo medido es el refuerzo sin subida, y la plataforma tiene que
> poder pedirlo solo.

### 3.3 Controlador PI unilateral

Se adopta **la misma estructura del lazo de presión**, porque la planta comparte
su propiedad esencial: el actuador solo puede aportar energía. La caldera no
dispone de refrigeración activa, de modo que una temperatura por encima de la
referencia se trata como saturación inferior y no como una orden negativa
inexistente.

```text
e[k] = r[k] - T_filtrada[k]
u_sin_limitar[k] = Kp · e[k] + I[k]
u[k] = limitar(u_sin_limitar[k], 0 %, 100 %)
```

La integral se actualiza solo cuando la salida no está saturada o cuando el
error tiende a sacarla de la saturación. La referencia interna se aproxima a la
autorizada mediante un limitador de cambio.

Adoptar una sola estructura de control para los dos lazos es deliberado: reduce
lo que hay que verificar, permite compartir la prueba de anti-*windup* y hace que
una sola forma de razonar sirva para ambos. Lo que difiere entre lazos es el
mecanismo de actuación, no la ley.

### 3.4 Prealimentación por entrada de agua fría

Durante la extracción entra agua fría a la caldera y desplaza al agua caliente.
La temperatura del agua cae de inmediato, pero el sensor está sobre el cuerpo de
aluminio y ve esa caída **más tarde y atenuada**.

El problema es la escala de tiempo. Una extracción dura entre 25 y 30 segundos.
Si el retardo entre el agua y el sensor es de varios segundos —lo esperable con
2,8 mm de pared—, cuando el PI reaccione ya se habrá extraído buena parte de la
bebida a la temperatura equivocada. **El realimentado no llega tarde por estar
mal sintonizado: llega tarde por construcción**, porque el proceso termina antes
de que acabe de corregir.

La perturbación, en cambio, se conoce de antemano: el firmware ordena arrancar
la bomba. Se adopta por tanto una prealimentación que aporta potencia en el
instante en que la bomba se habilita, sin esperar a que el error aparezca.

#### Se suma a la salida, no a la referencia

Gaggiuino resuelve esto elevando el umbral de temperatura durante la extracción,
en proporción a un caudal estimado. Esa forma es correcta **para un controlador
sin integral**, que se estabiliza con error permanente y necesita apuntar más
alto para acabar donde quiere.

Aquí sería un error. La integral empuja el error hasta cero: si se desplazara la
referencia, el lazo cumpliría fielmente una referencia falsa y calentaría de más.
La prealimentación se suma **a la salida del controlador**, de modo que la
referencia sigue diciendo la verdad y el PI conserva su autoridad para restar lo
que sobre.

```text
u[k] = limitar(Kp·e[k] + I[k] + u_ff[k], 0 %, 100 %)
```

`u_ff` es cero salvo mientras la bomba esté habilitada. La saturación y el
anti-*windup* operan sobre la suma, no sobre el término realimentado por
separado: de lo contrario la integral se cargaría compensando una
prealimentación que ya está saturando la salida.

#### La señal que la gobierna, y su evolución

La perturbación térmica vale `ṁ · cp · (T_caldera − T_entrada)`: es proporcional
al **caudal másico**, no a un sustituto suyo. Se define por tanto `u_ff` como
función de una estimación de la perturbación, y se versiona **la fuente de esa
estimación**:

| Versión | Señal | Disponible |
|---|---|---|
| `ff-v1` | Densidad PSM de la bomba | Desde el primer firmware |
| `ff-v2` | Caudal estimado desde densidad y presión, validado contra caudal medido | Tras la campaña de caudal |

La densidad PSM mide el *esfuerzo* de la bomba, no el caudal: a densidad
constante, el caudal cae cuando la cama ofrece más resistencia. Es una
aproximación, y se declara como tal. El caudal estimado es la señal correcta y
llega cuando exista con qué validarlo (ADR-006 y ADR-007, revisiones del
2026-09-14).

Un caudal estimado es un cálculo derivado, no una medición, y viaja al registro
con su origen y su versión de calibración.

#### Se especifica y se adopta después

La prealimentación queda **especificada y deshabilitada**, con `u_ff = 0`. Se
habilita solo si la campaña § 6.4 demuestra que el PI por sí solo no sostiene la
banda durante la extracción, y con una constante obtenida de esos registros. Es
el mismo criterio aplicado a la protección por velocidad de ascenso: no se
enciende una compensación con un número supuesto.

> **Implementación del 2026-10-11.** `ff-v1` existe en el firmware
> (`heater_feedforward`): suma `ganancia · densidad` a la salida del PI mientras
> la bomba está autorizada, y el PI satura y aplica el anti-*windup* sobre la
> suma. La ganancia se compila con `CAFE_HEATER_FEEDFORWARD_MILLI`, que vale
> **cero** por omisión, de modo que sigue deshabilitada hasta que la puesta en
> servicio 7.9 la mida. La escalera de la bomba del 2026-10-10 ya muestra la
> necesidad: al 50 % la caldera cayó de 91,6 a 83 °C con el PI solo.

#### Enmienda del 2026-10-17 — sin agua no hay prealimentación

> **Estado: aprobada por el operador el 2026-10-17.** Programada y probada en
> el anfitrión. Rige en la placa cuando se cargue un binario que la contenga.

**Por qué.** `ff-v1` sigue a la densidad, que dice que la bomba trabaja, no que
pase agua. El 2026-10-17 a las 01:12, con el depósito vacío, la bomba trabajó
27 s en seco al 75 %. La aportación tuvo el calentador al 59 % sin agua fría que
compensar, y el metal filtrado pasó de 92,6 a 112,3 °C
(E3). No se
llegó al límite duro de 120 °C.

**Se decide** cortar la aportación cuando, con la bomba autorizada, el metal
filtrado supera el valor que tenía al arrancar la bomba en más de la tolerancia
con que el supervisor declara «lista» (`ready_tolerance_c`, 0,5 °C; corregido el
2026-10-17: la primera redacción decía 0,6 °C, la banda de la verificación B). El corte dura hasta que la
bomba pare. Una medida no finita también corta. Vive en
`heater_feedforward_step`.

**De dónde sale el umbral.** No es un número nuevo, es la tolerancia de «lista». El
registro de los 190 bombeos del 11 al 17 de octubre lo respalda. Con agua, el
metal baja al arrancar la bomba: en el café de 07:53 del 17 bajó de 92,4 a
81,8 °C. Fuera del bombeo en seco, ninguno subió más de 0,50 °C sobre el valor
al pulsar, y con aportación ninguno más de 0,36 °C; el de 0,50 °C fue sin
aportación, así que el corte no lo toca. El bombeo en seco subió
6,3 °C.

**Lo que no resuelve.** Con el registro de ese bombeo, el corte llega a los
19 s de 27, cuando el metal ya subió de 92,58 a 93,28 °C. Reduce el exceso, no
lo evita. Para avisar antes se descartaron dos señales. La báscula no siempre
está bajo el portafiltro. La presión en seco (5,4–5,8 bar) queda cerca de la de
un purgado sin canastilla (6,8 bar al 75 %, paso 10.4). La bomba en seco, como
tal, es un asunto del lazo hidráulico, no de este.

### 3.5 Modulación por ciclo integral

La salida `u` se convierte en tiempo de conducción dentro de una ventana fija.
El SSR cuantiza por sí mismo la orden a semiciclos completos, de modo que el
firmware no cuenta ciclos: mantiene `HEATER_CMD` activo la fracción
correspondiente de la ventana.

| Elemento | Decisión inicial |
|---|---:|
| Ventana de modulación | **1 s** |
| Cuantización inherente del SSR a 60 Hz | 8,33 ms (medio ciclo) |
| Salida lógica | 0–100 % |
| Residuo | se acumula y se arrastra a la ventana siguiente |

El acumulador de residuo es lo que impide que la resolución efectiva quede
limitada por la ventana: el redondeo de una ventana se compensa en la siguiente,
de modo que la media a largo plazo sigue la demanda con una resolución muy
superior a la de un único periodo. Es el mismo principio que el planificador PSM
de la bomba, aplicado a otra granularidad.

**Por qué una ventana de 1 s.** El rizado térmico que introduce la modulación
vale aproximadamente `ΔT ≈ P · d · T_ventana / C`, donde `d` es la demanda y `C`
la capacidad térmica del conjunto. Con `P = 1050 W` y una capacidad combinada
estimada de unos 750 J/K —152 g de agua más unos 120 g de aluminio—, una demanda
de régimen del 20 % da:

```text
ΔT ≈ 1050 × 0,20 × 1 / 750 ≈ 0,28 K
```

> **Revisión del 2026-10-07 — con lo medido, la ventana no cambia.** Con 81 mL de
> agua, 431 g de aluminio y el calentador de 14,0 Ω (≈ 1029 W a 120 V), la
> capacidad del conjunto sale en **≈ 753 J/K** (339 de agua y 414 de aluminio) y
> el rizado en **≈ 0,27 K** con la misma demanda del 20 %. La estimación original
> acertó por compensación: suponía el doble de agua y menos de un tercio del
> aluminio. La ventana de 1 s se mantiene. El detalle está en la
> caracterización del boiler.
>
> **Corrección del 2026-10-22 — calor específico con fuente.** El aluminio pasa de
> 960 J/(kg·K), sin fuente, a 935 J/(kg·K), el aluminio puro a unos 100 °C
> (Incropera, tabla A.1): 403 J/K de metal y **≈ 742 J/K** de conjunto. El rizado
> queda en 1029 × 0,20 / 742 ≈ 0,28 K; la ventana de 1 s no cambia.

Una ventana de 5 s daría 1,4 K con la misma demanda, que es del orden de la
tolerancia completa. La advertencia de la caracterización —una caldera de alta
densidad térmica es «propensa a sobrepasos si se controla con ciclos demasiado
largos»— se traduce aquí en un número.

**Qué no limita la elección.** El desgaste por conmutación no es un criterio: un
relé de estado sólido no tiene contactos y lo que exige atención es su
disipación continua, del orden de 11 W con el disipador previsto, no el número
de conmutaciones. La ventana se elige por el rizado térmico y por nada más.

### 3.6 Periodos y filtrado

| Elemento | Decisión inicial | Estado |
|---|---:|---|
| Adquisición de temperatura | 100 ms | **Implementada el 2026-09-15.** La tarea de adquisición corre a 10 ms por la presión y decima la temperatura a uno de cada diez ciclos. |
| Ejecución del PI | 200 ms | Valor inicial; cinco ejecuciones por ventana. |
| Filtro digital | Primer orden, `τf = 1 s` | Valor inicial ajustable con registros reales. |

La temperatura no necesita la cadencia de la presión: su constante de tiempo es
de decenas de segundos y su ruido no proviene del rizado de la bomba. El
firmware conservará siempre la muestra cruda, la temperatura convertida y la
filtrada como señales distintas, y el filtro no podrá ocultar una entrada
inválida, saturada o fuera del límite duro.

### 3.7 Seguridad e inhibiciones

`HEATER_CMD` permanecerá inactivo cuando ocurra cualquiera de estas condiciones,
que ya evalúa el guardián de actuación:

- el estado operativo no autoriza regulación térmica;
- el control de temperatura no está puesto en servicio;
- la adquisición incumplió su plazo;
- la temperatura no es apta —desconectada, saturada, imposible o desactualizada—;
- se supera el límite duro de temperatura;
- la ESP32-S3 está en arranque, reinicio o actualización.

#### Límite duro y su propósito

Se adopta provisionalmente **150 °C sobre el cuerpo**. El criterio de elección no
es la protección del agua sino **la protección de la protección**: los `G4A00` no
son reajustables, de modo que el límite por software debe actuar con margen
suficiente para que nunca lleguen a abrirse. Los 42 °C que separan ambos valores
son ese margen, y es la mitad sólida de la justificación.

**La otra mitad no se sostiene, y conviene decirlo.** Sería cómodo añadir que el
límite deja «20 °C por encima de la referencia de vapor», pero esa referencia
—130 °C— procede de `simulation_default_config()`, un bloque que el propio código
declara como «valores que hacen corta y observable la simulación en banco y en la
placa. No son referencias puestas en servicio ni límites de la máquina».
Justificar un límite por su distancia a un número inventado para acelerar una
simulación no justifica nada.

**Ambos valores quedan marcados como provisionales**, y la referencia de vapor es
una magnitud que hay que **medir**: qué temperatura del cuerpo corresponde a
espumar de verdad. El riesgo de no hacerlo es concreto y va en los dos sentidos.
Si el vapor real exigiera, por ejemplo, 145 °C de cuerpo, el límite de 150 °C
saltaría durante un espumado normal y la máquina entraría en alarma haciendo
aquello para lo que está diseñada. Si se sostuviera con 115 °C, se estaría
renunciando a un margen disponible.

> **Revisión del 2026-10-06 — sin vapor, el límite solo protege la protección.**
> El prototipo deja de tener función de vapor (ADR-006, revisión de la misma
> fecha), de modo que la referencia de vapor desaparece y con ella la mitad que
> no se sostenía. Los 150 °C quedan justificados únicamente por el margen frente
> a los `G4A00`, que es la parte sólida. Ya no hay ningún uso legítimo que lleve
> el cuerpo por encima de la temperatura de extracción, así que el límite podría
> bajarse; no se hace aquí, porque ese número también es de puesta en servicio y
> se fijará con la correspondencia entre cuerpo y agua medida.

#### Protección por velocidad de ascenso

El prototipo no detecta falta de agua, y una caldera seca es el modo de fallo
térmico más rápido que admite este conjunto. La propia física ofrece una firma
utilizable sin añadir hardware:

```text
con agua:   1050 W / ≈750 J/K  ≈ 1,4 K/s
en seco:    1050 W / ≈110 J/K  ≈ 9,5 K/s
```

Casi siete veces de diferencia. Se decide vigilar la velocidad de ascenso
sostenida durante el calentamiento y tratar como condición insegura un valor que
no pueda explicarse con la caldera llena.

Dos cautelas, porque el margen no es tan amplio como sugiere el cociente. El
sensor mide el metal, que es el nodo rápido, de modo que la velocidad observada
durante los primeros segundos puede superar la del conjunto sin que falte agua.
Y el umbral debe medirse, no derivarse: se fijará con registros reales de
calentamiento en frío y con agua, y solo entonces se habilitará. Hasta que
exista esa evidencia, la protección queda especificada y **deshabilitada**, no
activada con un número supuesto.

> **Revisión del 2026-10-06 — se descarta, y no se sustituye.** Medida la
> caldera, el cociente de arriba no existe. El volumen útil es de 81 ± 3 mL, no
> 152, y la caldera vacía pesa 485 g, no los 120 g de aluminio supuestos. Con
> esos datos la capacidad con agua sigue en unos 750 J/K, pero la de la caldera
> seca sube a unos 400 J/K: en seco subiría unos 2,6 K/s frente a 1,4 K/s con
> agua, menos del doble, y el transitorio del metal que ya se advertía arriba
> basta para confundirlos. Son estimaciones con calores específicos de tabla,
> no mediciones.
>
> Se decide no implementar esta protección ni sustituirla por un sensor de
> nivel. La caldera seca queda cubierta como antes de este ADR: por el límite
> duro de 150 °C y, detrás, por los dos `G4A00`, que no se puentean. El riesgo
> que se acepta es que una caldera seca llegue al límite duro antes de cortarse,
> en un prototipo que solo opera supervisado.

#### Puesta en servicio

El firmware inicia con `temperatureControlCommissioned = false`, `Kp = 0` y
`Ki = 0`. Mientras no exista un conjunto de parámetros identificado, validado y
versionado, ninguna operación ordinaria podrá energizar el calentador. Esta
condición ya está implementada y se observa en el diagnóstico de arranque.

La identificación de la planta no es una operación ordinaria y se rige por el modo
restringido que adopta la enmienda del 2026-09-21 de la § 5.

### 3.8 Vapor y retorno térmico

> **Revisión del 2026-10-06.** El prototipo no tiene función de vapor (ADR-006,
> revisión de la misma fecha). Esta sección queda como registro; el estado de
> retorno térmico pierde su único origen.

El vapor utiliza el mismo lazo con la referencia de vapor. El retorno a café no
dispone de refrigeración activa: el controlador satura en 0 % y el descenso es
pasivo, gobernado por la pérdida hacia el ambiente. La máquina de estados ya
modela ese tránsito como un estado propio y el supervisor ya declara alcanzada la
condición cuando la temperatura vuelve a la banda de café.

No se añadirá ninguna maniobra activa de enfriamiento —purgas automáticas u
órdenes a la bomba— porque implicaría actuar sobre el circuito hidráulico sin
una acción física del barista.

### 3.9 Registro obligatorio

Cada prueba del lazo conservará, con tiempo monotónico: temperatura cruda
convertida y filtrada; referencia autorizada y referencia interna limitada; error
de control; término integral y salida antes y después de limitar; demanda
solicitada y tiempo de conducción realmente aplicado; residuo acumulado; estado
operativo, calidad de la medición y veredicto del guardián; velocidad de ascenso
observada; y causa de terminación o protección.

### 3.10 Diagnóstico de conducción sin orden

El SSR es el único medio de corte del calentador, y su modo de fallo dominante
es el cortocircuito: un triac averiado se queda conduciendo. Si eso ocurre, el
firmware pierde la salida por la que actúa, pero no la entrada por la que
observa. La decisión es aprovechar esa asimetría.

La prueba es un balance entre lo que se ordena y lo que responde el proceso.
Con la orden acumulada al calentador en cero durante una ventana de gracia, y
con la medida declarada apta, una velocidad de ascenso positiva sostenida solo
puede significar que el calentador conduce sin orden. La señal es grande: con
1050 W continuos el cuerpo sube a 1,4 K/s con agua, muy por encima del ruido del
NTC. La condición inversa —orden alta sostenida con temperatura plana— indica el
fallo abierto, que es benigno y se señaliza igual sin enclavar.

**El diagnóstico se enclava.** Declarada la conducción sin orden, el sistema no
sale de esa condición: ni al enfriarse, ni al reiniciar el lazo. Solo la levanta
un arranque posterior a la intervención. Un fallo de actuador no se recupera, y
un aviso que se apagase al bajar la temperatura le diría al barista que fue un
susto.

**Lo que este diagnóstico no hace, y conviene escribirlo.** No corta el
calentador, porque el elemento por el que se cortaba es precisamente el que ha
fallado. Inhibe la bomba, cierra la sesión y rechaza toda orden nueva, de modo
que nadie esté extrayendo mientras tanto, pero el boiler sigue recibiendo
potencia. Presentarlo como una protección sería falso: es un diagnóstico, y
cubre RF-011 —informar de una condición insegura— y la parte de RF-012 que el
hardware permite cumplir.

**Valores, decididos el 2026-10-22 (guion 5.8).** Ventana de gracia de 30 s tras
cada corte y umbral de 0,3 °C/s sostenidos 10 s. Salen de los cortes desde plena
potencia del 2026-10-13: el arrastre llega a su pico a los 25 s y pasados 20 s ya
sube menos de 0,03 °C/s, mientras que a plena potencia el cuerpo sube
1,2–1,4 °C/s. El diagnóstico sigue sin programar.

#### La cadena completa

| Escalón | Qué actúa | Naturaleza |
|---|---|---|
| 1 | Detección por NTC: enclavamiento, aviso y registro | firmware |
| 2 | Desconexión de la red por el operador | supervisión humana, ADR-008 |
| 3 | `G4A00` a 192 °C | corte físico, un solo uso |

El escalón 2 no es una carencia disimulada: ADR-008 declara el prototipo como
supervisado, y esta cadena es coherente con ese alcance. Añadir un medio de
corte independiente en serie con el SSR resolvería el escalón 1, y se descartó
porque obliga a rehacer `PCB-POTENCIA` y `PCB-CONTROL` con el trazado ya cerrado
y verificado.

#### Señalización

Se usa el LED de alarma con **parpadeo rápido**, `INDICATOR_FAST`, patrón que
hoy no tiene asignado. No se añade ningún valor al enumerado de patrones ni se
toca el cálculo del parpadeo: el LED `READY` ya lo emplea para el vapor y la
función que lo resuelve no cambia. El LED de alarma queda entonces con tres
significados escalonados:

| Patrón | Significado |
|---|---|
| Parpadeo lento | el guardián ha cortado las salidas; el sistema gobierna la situación |
| Fijo | parada declarada en `OP_STATE_ALARM` |
| **Parpadeo rápido** | **el calentador conduce sin orden; desconectar de la red** |

El criterio del escalonamiento no es la gravedad abstracta sino **quién tiene que
actuar**. Lento y fijo describen condiciones que el sistema sí gobierna. Rápido
es el único que pide una acción física a la persona que está delante, y por eso
es el que se distingue a más distancia.

#### Vigilancia permanente

La detección corre siempre que haya alimentación, no solo durante una sesión de
preparación. El caso más probable es que el SSR quede cerrado con la máquina
enchufada y en reposo, que es justo cuando nadie mira; una vigilancia activa solo
durante las sesiones no leería el NTC en ese momento.

#### Registro

El evento se archiva con tiempo monotónico y con la serie de temperatura de los
segundos anteriores y posteriores a la declaración, además de la orden
acumulada que estaba vigente. Sin ese tramo previo el episodio no es
reconstruible y no sirve como evidencia.

## 4. Parámetros reservados a la puesta en marcha

No pueden obtenerse responsablemente antes de energizar la caldera instrumentada:

- `Kp` y `Ki` definitivos;
- constante final del filtro;
- velocidad del limitador de cambio de la referencia;
- ~~**la referencia de vapor**~~, retirada el 2026-10-06; el límite duro definitivo se fija ahora solo contra los `G4A00` y la correspondencia entre cuerpo y agua;
- constante de la prealimentación, y si hace falta habilitarla;
- ~~umbral de velocidad de ascenso y su ventana de evaluación~~; la protección se descartó el 2026-10-06 (§ 3.7);
- tolerancia de estabilidad;
- correspondencia entre la temperatura del cuerpo y la del agua;
- la ventana de gracia y el umbral de velocidad de ascenso del diagnóstico de
  conducción sin orden, que se obtienen midiendo el arrastre térmico real tras
  un corte: el aluminio sigue cediendo calor al sensor durante unos segundos, y
  ese arrastre es lo que produciría un falso positivo;
- comportamiento durante la extracción, cuando entra agua fría;
- desplazamiento máximo de la compensación del primer uso (`first_use_offset`
  del acta, enmienda del 2026-10-19).

Ninguno bloquea el esquema, el cableado ni la implementación estructural del
firmware. Todos bloquean declarar validado RF-009.

## 5. Procedimiento de puesta en marcha

1. Verificar aislamiento, montaje del SSR y su disipador, continuidad de los dos
   fusibles térmicos en frío y estado seguro de `HEATER_CMD`, antes de energizar.
2. Confirmar que el SSR conduce y corta con una carga segura y una orden manual
   acotada, antes de conectar el calentador.
3. Registrar un escalón de calentamiento en lazo abierto, con la caldera llena y
   desde temperatura ambiente, con una demanda conservadora y parada disponible.
   De ese registro se estiman ganancia, retardo y constante de tiempo.
4. Comparar fuera de línea constantes de filtro candidatas —por ejemplo 0,5, 1 y
   2 s— y escoger la menor que reduzca el ruido sin ocultar el ascenso ni el
   sobrepaso.
5. Sintonizar con el método SIMC, igual que el lazo de presión:

   ```text
   Kp = τ / [K·(λ + θ)]
   Ti = min(τ, 4·(λ + θ))
   Ki = Kp / Ti
   ```

   con `λ` deliberadamente conservador.
6. Empezar con `Ki = 0`, aumentar `Kp` gradualmente y comprobar seguimiento,
   sobrepaso y apagado. Solo después incorporar integral y anti-*windup*.
7. ~~Medir la velocidad de ascenso real en varias condiciones de llenado para fijar
   el umbral de la protección, y solo entonces habilitarla.~~ Retirado el
   2026-10-06: la protección se descartó (§ 3.7).
8. Caracterizar el comportamiento durante una extracción, cuando entra agua fría.
   ~~Y durante el retorno desde vapor~~: el vapor se retiró el 2026-10-06.
9. Congelar los valores, su versión, las condiciones de prueba y los criterios de
   aceptación antes de cambiar `temperatureControlCommissioned` a verdadero.

No se utilizará sintonía por relé ni Ziegler–Nichols como método inicial, por la
misma razón que en ADR-007: obliga a provocar oscilaciones sostenidas en una
planta cuya protección todavía debe validarse.


#### Enmienda del 2026-09-21 — el modo restringido también rige aquí

Este procedimiento arrastraba el mismo círculo que ADR-007 § 5, y en peores
condiciones: allí la § 3.5 al menos **nombraba** un «modo restringido de puesta en
marcha», mientras que la § 3.7 de esta decisión se limita a decir que «ninguna
operación ordinaria podrá energizar el calentador». La palabra «ordinaria» deja
sitio a algo que no es ordinario, pero ese algo no estaba definido en ninguna
parte, de modo que los pasos 3 a 8 exigían registros que el guardián impedía
tomar.

**Se adopta el modo restringido definido en la enmienda del 2026-09-21 de
ADR-007 § 5**, con las mismas tres condiciones de autorización, la misma relajación
de una sola causa de inhibición, el mismo veredicto propio en el registro y las
mismas cuatro alternativas descartadas. No se define aquí un segundo mecanismo:
los dos lazos comparten guardián, tarea y registro, y dos autorizaciones distintas
para el mismo corte serían dos formas de equivocarse.

Lo que esta decisión añade sobre lo que ya dice ADR-007:

**El paso 2 no necesita el modo.** Confirmar que el SSR conduce y corta con una
carga segura es una verificación de actuación, no una identificación: se hace en
banco, con un microcontrolador de pruebas, igual que ADR-008 § 5.9 ordena para el
dimmer. El modo restringido hace falta a partir del paso 3, que ya registra sobre
la cadena de medida real.

**El criterio de parada de la secuencia térmica es la velocidad de ascenso
observada, no la demanda.** Es la diferencia de fondo con la presión. La bomba
tiene un límite de servicio continuo que el guardián ya vigila —`ID2/1min`— y que
detiene el ensayo por sí solo; el calentador no tiene ningún equivalente, y su
protección propia, la de velocidad de ascenso de la § 3.7, **todavía no está
habilitada: es el paso 7 el que la habilita**. Durante la identificación térmica
esa protección no existe, y lo único que queda entre una demanda excesiva y la
caldera es el límite duro de 150 °C, que es provisional y protege el cuerpo, no el
ensayo.

De ahí que la secuencia térmica declare su propio criterio de parada por pendiente
y que el operador permanezca delante con la alimentación a la vista. Es el punto
de todo el procedimiento donde la supervisión de ADR-008 deja de ser una fórmula y
pasa a ser la única protección activa.

> **Revisión del 2026-10-06.** Descartada la protección por velocidad de
> ascenso, el paso 7 ya no la habilita nunca: lo dicho para la identificación
> térmica vale ahora para toda la operación. El criterio de parada por pendiente
> de la secuencia se mantiene, porque es un límite del ensayo y no una
> protección de la máquina. Las secuencias por condición de llenado del párrafo
> siguiente solo tenían objeto para fijar ese umbral.

**Cada condición de llenado es una secuencia distinta bajo la misma campaña.** El
paso 7 pide medir la velocidad de ascenso en varias condiciones de llenado, y el
paso 3 exige caldera llena desde temperatura ambiente. Son ensayos diferentes con
el mismo estímulo, de modo que el identificador de la secuencia debe distinguirlos
o los registros resultarán indistinguibles cuando haya que compararlos.

#### Enmienda del 2026-10-08 — el paso 2 se hace con el modo restringido

No había carga segura disponible, y un programa de banco que accione el
calentador fuera del firmware queda fuera del guardián. **El paso 2 se hace con el
binario de identificación**, con `heater-step-1pct-v1` y tope del 1 %, sobre el
calentador real, con la caldera llena y supervisado con la mano en el enchufe. Se
observa que el SSR conmuta una vez por ventana, que la temperatura sube y que se
detiene al apagar.

**Contrapartida:** la prueba deja de ser independiente del firmware; un error del
modo restringido se probaría contra el calentador real. Lo acotan el tope del
1 %, que el guardián vuelve a comprobar en cada ciclo, y los dos térmicos
originales del boiler, `G4A00` de 192 °C. El termofusible de 110 °C de la placa de
potencia protege el varistor, no el calentador. Lo dicho arriba sobre el microcontrolador de pruebas sigue valiendo para
el dimmer (ADR-008 § 5.9).

#### Enmienda del 2026-10-11 — arranque rápido

> **Estado: diseño, sin acta.** Lo que sigue decide la forma; los números que la
> hacen funcionar son de puesta en servicio y no existen todavía. Mientras el
> acta de temperatura no los declare, el arranque rápido no actúa y la máquina
> calienta exactamente como con el acta v2.

**El problema.** El acta v2 (`acta-temperatura-20261010b`) limita la salida al
**10 %**. No es una preferencia: con su planta y su sintonía, el modelo del lazo
completo (`test_control_loop.c`) arranca a 22 °C y llega a 92 °C sin sobrepaso
con el 10 %, y sube a 106,5 °C con el 100 %. El precio es el tiempo: el
2026-10-11 la caldera tardó **17 min** de 25,4 °C a «lista». Con el 100 % el
calentador entrega diez veces más potencia, pero el PI no sabe qué hacer con
ella: su integral y su retardo de 44 s lo llevan por encima de la referencia.

**El margen que hay es poco.** El mismo día, con un NTC suelto en la parte alta
de la caldera, en tres calentamientos con la placa en «lista» y el metal en
92,5–92,8 °C, el agua de arriba **hervía** (hervor local 94,7 °C), entre 1,2 y
2,1 °C por encima del metal. El calentador ya lleva esa agua al hervor sin
sobrepaso; uno del metal no tiene adónde ir
(evidencia).

##### La decisión

**Una fase anterior al PI, no un PI distinto.** Al empezar a calentar, el
calentador va al **100 %** hasta una **temperatura de corte**; ahí se apaga del
todo y espera; cuando la temperatura deja de subir, el PI del acta toma el mando
con la integral en cero. La ley del PI, su sintonía y su tope no cambian: el
arranque rápido solo decide cuándo el PI empieza.

**El corte es la referencia del perfil menos un margen, `r − δ`.** No es un
número absoluto: el barista elige la referencia, y el corte la sigue. `δ` es
**un solo número del acta**: lo que la caldera sigue subiendo tras cortar desde
**frío**, que es el peor caso —el calentador pasa más tiempo al 100 % y acumula
más calor—, más un margen. Desde una caldera tibia el mismo `δ` es
conservador: se acumula menos calor, la subida se queda corta y el PI completa
los últimos grados.

> **Descartado: un `δ` calculado en el firmware según la temperatura de
> partida.** Sería más rápido desde tibio, pero mete un modelo en el camino de
> una decisión de seguridad, y el modelo se ajusta al 10 %, donde el elemento no
> alcanza la temperatura que tiene al 100 %. Un número medido en el peor caso se
> comprueba; un modelo dentro del lazo, no.

> **Descartado: pasar al PI en el mismo instante del corte.** Con la integral en
> cero y el error en `δ`, el término proporcional pide `Kp·δ` mientras el calor
> acumulado todavía sube la temperatura, y suma al arrastre lo que el corte
> quería evitar. Por eso hay una espera a 0 % entre el corte y el PI.

**Cuándo se activa.** Se arma al entrar en «calentando» y actúa si se cumplen
todas:

- la temperatura es apta y está por debajo de `r − δ − Δ_activación`: para un
  par de grados basta el PI, y el 100 % solo tiene sentido con distancia;
- la bomba no está autorizada: con agua entrando manda la prealimentación de la
  § 3.4, y las dos no se suman;
- la referencia no pasa de `r_máx`: cerca del hervor el margen desaparece y la
  máquina calienta solo con el PI;
- no se ha usado ya en este episodio de calentamiento.

No depende de arrancar en frío: vale igual al encender, desde una caldera tibia
o tras subir la referencia.

**Lo que lo hace seguro sin confiar en el cálculo.**

- **Tiempo máximo al 100 %, `t_máx`**, del acta: aunque el sensor no llegue nunca
  al corte, la energía entregada queda acotada. Vencido, se corta igual, el PI
  toma el mando y queda en el registro como anomalía.
- **Cualquier cosa que no sea «calentando» con temperatura apta** lo abandona
  sin volver: alarma, cambio de estado, sensor no apto o bomba autorizada.
- **El guardián y el límite duro no cambian** y siguen por delante, por su ruta
  propia.

**Qué se registra.** La fase —inactiva, al 100 %, espera, entregada al PI— va
en cada línea `T`, para que la evidencia diga qué gobernaba el calentador en
cada instante.

##### Parámetros reservados a la puesta en servicio

`δ`, `Δ_activación`, `t_máx` y `r_máx`. Sin ellos el módulo no se arma. No se
rellenan con una cifra plausible.

##### Cómo se pone en servicio

El arrastre a plena potencia **se mide con el propio arranque rápido**, no con
pulsos en lazo abierto. El 2026-10-11 se comprobó que el modo restringido no se
arma sobre un lazo con acta (`identification_mode_configure`), y está bien que
no lo haga: el arranque rápido es parte del control, y se ensaya como tal.

1. **Estimar `δ`** con un modelo térmico ajustado a los calentamientos y el
   enfriamiento del 2026-10-11, para tener un punto de partida, no un valor.
2. **Primer arranque desde frío con `δ` provisional grande** —un corte muy por
   debajo de la referencia, de modo que la subida estimada quede lejos de
   ella—, con el NTC suelto en el agua y el límite duro de prueba bajado como
   respaldo, igual que en la 5.10. Mide el arrastre real de metal y agua.
   El arrastre medido con un corte temprano vale para el corte final solo si
   el calentador ya había llegado a su gradiente de régimen antes de cortar;
   si no, se queda corto, que es justo el lado peligroso. El registro tiene
   que mostrarlo: la diferencia entre el metal y el agua deja de crecer antes
   del corte.
3. **Fijar `δ`** con ese arrastre y un margen, de modo que el máximo del metal
   no pase de la referencia y el del agua no pase del que tiene en régimen.
4. **Tres arranques desde frío** con el `δ` fijado, y **uno desde tibio**: ni el
   metal ni el agua pasan de esos máximos, y se registra el tiempo hasta «lista».
5. Acta v3 con `δ`, `Δ_activación`, `t_máx` y `r_máx`, y sus registros.

**Lo que el paso 2 tiene que decidir, además de `δ`.** Durante la subida al
10 % del 2026-10-11 el metal fue unos 5 °C por delante del agua, con unos 100 W.
Si esa diferencia crece en proporción a la potencia, al 100 % el sensor iría
**decenas de grados por delante del agua**: un corte sobre el metal dejaría el
agua muy por debajo, y después del corte el metal bajaría mientras el agua
sube. Es una extrapolación lineal de un régimen diez veces más suave, y no se
usa para nada salvo para saber qué mirar. Si el paso 2 la confirma, el corte
por temperatura del metal no basta y se añade un **corte por energía**:
entregar como máximo la que lleva toda la caldera, desde su temperatura de
partida, hasta `r − δ`, con la capacidad calorífica medida. Ese criterio está
acotado por construcción, como el pulso, y protege el agua, que es lo que el
metal no ve.

Los tres pulsos que el 2026-10-11 se habían planeado para medir el arrastre
quedan sustituidos por los pasos 2 a 4: miden lo mismo en la situación real de
uso, en lugar de un estímulo de 30 s que había que extrapolar a varios minutos.

#### Enmienda del 2026-10-12 — tres puertas sobre el tope del 10 %

> **Estado: diseño, sin acta.** Decidido con el operador el 2026-10-12.

El tope del 10 % del acta v2 tiene **una** razón: con el 100 % disponible, el PI
se pasa al calentar desde frío (106,5 °C en el modelo del lazo completo). Pero
el tope también frena dos cosas que no tienen ese problema:

- el **arranque rápido** (enmienda del 2026-10-11), que no es el PI y lleva su
  propio corte y su tiempo máximo;
- la **prealimentación** `ff-v1` (§ 3.4), que la 7.9 midió en 1,15 % de
  calentador por punto de densidad: a 30 % de bomba pide unos 35 puntos, y con
  el tope no suma nada útil. Al 50 % el metal cayó de 93,8 a 76,9 °C con el PI
  solo (evidencia).

**Se decide no quitar el tope, sino abrir tres puertas separadas**, cada una con
su protección y su acta:

| Puerta | Hasta | Cuándo | Qué la cierra |
|---|---|---|---|
| 1. Arranque rápido | 100 % | Solo en su fase, al empezar a calentar | Corte en `r − δ`, `t_máx`, cualquier salida de «calentando» |
| 2. Prealimentación | Tope del PI **más** `u_ff`, sin pasar de 100 % | Solo con la bomba autorizada | Bomba no autorizada: `u_ff` vuelve a cero en el mismo ciclo |
| 3. PI | El tope del acta (10 % en la v2) | Siempre | — |

**La puerta 2 cambia una regla de la § 3.4**, que decía que la saturación y el
anti-*windup* operan sobre la suma. Ahora el PI satura **solo su parte** en su
tope, y la prealimentación se suma después, hasta el 100 %. La razón de la regla
original —que la integral no se cargue compensando una prealimentación ya
saturada— se conserva: el PI nunca ve la prealimentación, así que su integral
solo responde a su propio error, y su anti-*windup* actúa contra su propio tope.

> **Descartado: subir el tope del PI durante la extracción.** Daría al PI la
> potencia que hoy le falta, pero también la capacidad de pasarse justo al
> terminar la bomba, cuando el agua fría deja de entrar y la integral está
> cargada. La prealimentación, en cambio, se apaga con la bomba en el mismo
> ciclo: lo que añade termina cuando termina su causa.

**La ganancia sale del acta, como `δ`.** Hasta ahora `CAFE_HEATER_FEEDFORWARD_MILLI`
la fijaba en cualquier binario. Pasa a ser un parámetro del acta de temperatura;
la opción de configuración solo existe en el binario de identificación, para los
ensayos que la miden. Sin acta que la traiga, la puerta 2 queda cerrada.

**Verificación antes del acta.** Extracciones con la prealimentación activa en
el binario de identificación, comprobando que la caída del metal se reduce y
que al parar la bomba no hay sobrepaso; y la ganancia corregida con café, porque
la de la 7.9 es con la canastilla vacía y es un techo.

#### Enmienda del 2026-10-13 — el arranque rápido solo en frío, y el acta v3

> **Estado: acta v3 (`acta-temperatura-20261013`) redactada, pendiente de
> firma.** Hasta que se firme y se genere su planta, rige la v2.

**Lo que las corridas cambiaron de la enmienda del 2026-10-11.** Esa enmienda
suponía que el peor caso era el arranque en frío y que un `δ` medido ahí era
conservador desde tibio. Las corridas del 2026-10-12 lo desmintieron: con el
corte en 72 °C, el arrastre fue de 12,1 °C desde 25,8 °C y de 19,4 y 22,6 °C
desde unos 55 °C, con el cuerpo en 94,6 °C en la segunda
(evidencia).
Lo que más pesa no es la duración del pulso sino la temperatura del agua al
cortar: el agua fría se lleva el calor del metal. Un solo `δ` no sirve para
toda la franja en que el arranque armaba.

**Se decide que el arranque rápido sea solo de frío**, con la banda de
activación como restricción: arma únicamente con el cuerpo por debajo de
`r − δ − Δ_activación`. En frío el arrastre medido es pequeño y estable; desde
tibio, además de pasarse, el arranque no adelantaba la «lista»: 9 min 19 s
desde 54,5 °C, más que los 7 min 56 s desde 25,8 °C.

| Parámetro | Acta v3 | De dónde sale |
|---|---|---|
| `δ` | 20 °C (corte en 72 °C con referencia 92 °C) | La corrida desde 25,8 °C: arrastre 12,1 °C, «lista» en 7 min 56 s. El mayor arrastre en frío, 16,7 °C, queda 3,3 °C bajo `δ` |
| `Δ_activación` | 42 °C (arma bajo 30 °C con referencia 92 °C) | La partida más caliente medida en frío fue 25,8 °C; desde 55 °C el arrastre pasó de `δ` |
| `t_máx` | 70 s | El de la corrida con `δ` 20. El cuerpo cruzó 72 °C a los 51–53 s desde 21,5 y 25,8 °C; cortar por tiempo es inocuo (le deja al PI los últimos grados) |
| `r_máx` | 93 °C | El de todas las corridas, que se hicieron con referencia 92 °C |
| Caída que termina la espera | 0,2 °C | El de todas las corridas; la espera duró 30–34 s en frío |

El paso 4 de la enmienda del 2026-10-11 pedía tres arranques desde frío con el
`δ` fijado y uno desde tibio. El tibio deja de aplicar, porque no arma. De los
fríos hay uno con `δ` 20 y otros dos con `δ` 15 y 30 que lo encierran, los
tres con el cuerpo bajo la referencia hasta «lista»; el acta lo declara así.
Ya en «lista», con el PI al mando, el cuerpo pasó la referencia en 0,6–1,1 °C,
del orden de lo que la v2 ya hacía en régimen.

> **Descartado: cortar en 77 °C (`δ` 15).** Se probó el 2026-10-13 para
> aprovechar que en frío el arrastre es pequeño: fue de 11,5 °C, pero la
> «lista» llegó más tarde, 9 min 41 s. Con el corte más alto el PI recibe el
> mando más cerca de la referencia y su término proporcional pide menos; la
> simulación atribuye a `δ` 68–84 s de la diferencia y a la partida más fría
> 2–9 s (simulación).

> **Descartado: precargar la integral al entregar el mando.** En el modelo
> ajustado a las corridas, precargarla con el mantenimiento medido adelanta la
> «lista» unos 4 min, pero deja el cuerpo 2 °C sobre la referencia —cota
> inferior: el modelo se queda corto en lo real— y retrasa la estabilidad de
> 8,6 a 16 min, porque el agua fría sigue quitando calor y la integral sigue
> cargándose. Con el metal en 92,5–92,8 °C el agua de arriba ya hierve. El
> traspaso sigue con la integral en cero.

**El tope del 10 % no cambia.** No limita al arranque rápido (puerta 1) y el PI
no lo alcanzó tras el traspaso (9,4 % como máximo). Tampoco hace falta subirlo
para la prealimentación: desde la puerta 2 se suma después del PI, hasta el
100 % (`heater_feedforward_apply`), sin número propio en el acta. Lo que la
evidencia de la 7.9 decía sobre el tope era anterior a esa puerta.

**La prealimentación no entra en la v3.** Su ganancia es la de la 7.9, con la
canastilla vacía, y las extracciones con ella activa que la enmienda del
2026-10-12 pide antes del acta no se han hecho. El generador ya admite la
sección `feedforward` y el firmware la lee: entrará en una versión posterior
con esa verificación.

#### Enmienda del 2026-10-16 — retener la integral durante la extracción y la recuperación

> **Estado: no superó el ensayo del 2026-10-16; no rige.** Tras el primer
> café el metal no se pasó (máximo 91,67 °C), pero se arrastró en
> 91,0–91,3 °C y volvió a «lista» a los 554 s, de 2,5 a 7 veces más tarde que
> sin retención. La caldera necesita más sostenimiento después del café que
> antes, y la regla lo impide
> (evidencia).
> El código queda en el firmware, apagado salvo opción de ensayo o acta.

**El problema, medido.** El 15 de octubre, el metal pasó a 93,3–93,9 °C
hacia los 9 min de cada café, con prealimentación y sin ella. Es el
pendiente 7.6. En el registro, la integral sube 0,8–1,5 puntos sobre la
potencia de sostenimiento mientras la caldera se recupera. Al cruzar la
referencia se descarga a 0,07 puntos cada 30 s, y el proporcional (Kp
0,716) no alcanza a compensarla
(evidencia)).

**Se decide retener la integral** desde que la bomba queda autorizada hasta
que la medida filtrada vuelve a la referencia autorizada. La caída del café
no cambia la potencia que sostiene la caldera, porque es agua fría que entra
y se va. Retenida, la integral conserva el sostenimiento aprendido antes del
café, y al llegar no hay sobrante. Durante la retención actúan el
proporcional y la prealimentación. La regla vale igual para un purgado,
porque también es bomba con agua fría.

No añade números. Suelta al cruzar la referencia, sin margen ni tiempo. Vive
en un módulo puro, `heater_recovery_hold`, y el PI la recibe como un
argumento (`pi_controller_update_holding`). La retención también usa la
integral retenida en la salida, no la candidata. En el binario de
identificación la enciende `CAFE_HEATER_RECOVERY_HOLD`, y en uno de operación
el acta.

**Simulación antes de programarla**
(evidencia)).
- En el modelo de la 7.9 el máximo bajó de 93,59 a 92,29 °C, y en la planta
  del acta de 94,59 a 92,41 °C.
- «Lista» llega 33–50 s más tarde.
- En la prueba del lazo completo (`test_control_loop.c`), con la planta del
  acta y un café simulado, el máximo bajó de 94,23 a 92,73 °C.

> **Descartado: subir Kp.** En el modelo de la 7.9 quitaba el sobrepaso, pero
> en la planta del acta, la única con el retardo de 44 s, no lo quitaba y
> empezaba a oscilar. La sintonía actual es SIMC con λ = θ, y Kp ×2 equivale
> a λ ≈ 0. Cambiarla, además, obliga a repetir la puesta en servicio de todo
> el lazo, no solo de lo que pasa después del café.
>
> **Descartado: subir Ki.** La integral se cargaría más rápido durante la
> recuperación. En el modelo de la 7.9 el máximo subió a 94,36 °C.
>
> **Descartado: vaciar la integral al cruzar la referencia.** Dejaría la
> caldera sin su potencia de sostenimiento. Por la misma razón se descartó
> precargarla o vaciarla en el traspaso del arranque rápido (enmienda del
> 2026-10-13).

**Verificación antes del acta.**
- Dos cafés con el binario de identificación y la retención encendida, con
  el procedimiento A–G del plan de la temperatura del café.
- Criterio, fijado antes de medir: el metal no pasa de la banda de B
  (+0,6 °C) en los 15 min que siguen, y la vuelta a «lista» se registra.

#### Segunda enmienda del 2026-10-16 — devolver la integral al sostenimiento previo

> **Estado: no adoptada (2026-10-18).** Probada en la máquina con τ_eq = 533 s:
> recupera antes y se pasa 0,4 °C menos que sin ella, pero no cumple el
> criterio de 92,6 °C en ningún uso (92,85, 92,69 y 92,84 °C) y deja la caldera
> corta después. El modelo no encuentra un τ_eq que cumpla; el metal ya oscila
> cerca de 1 °C en reposo sin ella. El operador fijó como criterio el agua y no
> el metal, y el control sin ley ya lo cumple. Queda en el firmware apagada
> (τ_eq = 0). Evidencia: `temperatura-cierre-2026-10-17.md`, «Resultados del
> 2026-10-18».

**Por qué otra.** La retención de la primera enmienda quitó el sobrepaso, pero
dejó el metal bajo la referencia: tras el café hace falta más sostenimiento
mientras el agua nueva se calienta. La observación del director de la tesis
lo explica: el agua y el metal se igualan pasado un tiempo, y después hace
falta menos calor (evidencias/cafe-temperatura-2026-10-16.md).

**Se decide, a prueba,** dejar crecer la integral durante el café y la
recuperación, y devolverla desde que para la bomba al valor que tenía al
arrancarla:

    dI/dt = Ki·e − (I − I0)/τ_eq      durante 3·τ_eq desde la parada

Si otro café llega con la devolución en curso, I0 no se renueva. Con
τ_eq = 0 o sin acta, no actúa. Vive en `heater_recovery_return` (módulo puro);
el PI ofrece `pi_controller_relax_integral`. En el binario de identificación la
enciende `CAFE_HEATER_RECOVERY_RETURN_TAU_S`, y no se combina con la retención.

**τ_eq = 533 s, provisional.** Sale del modelo de tres nodos (calentador, metal
y agua en dos capas) ajustado a los cafés del 15 y 16
(informe).
El sumidero medido sin modelo da 568 ± 217 s. En ese modelo, el máximo tras
el café baja de 92,59–92,99 a 92,16–92,49 °C, y «lista» llega 23 s más tarde.
El modelo subestima el sobrepaso real en 0,2–0,6 °C. En la planta de primer
orden del acta, que no tiene el agua aparte, la prueba del lazo completo solo
muestra que el máximo baja (94,23 a 94,03 °C) y que no oscila.

> **Descartadas (en el mismo informe):** la integración condicional, porque se
> atasca bajo la referencia si la demanda cambia de verdad; el back-calculation
> con Tt = Ti, porque carga la integral hasta el techo (95,6 °C en la planta);
> retener solo con la bomba, porque casi no cambia nada.

**Verificación antes del acta.** Como el sobrepaso aparece también tras un
purgado de 10 s, el ensayo puede hacerse sin café. Dos purgados, cada uno
tras ≥10 min en «lista» y con el metal en la banda. Criterio, fijado antes de
medir: el metal no pasa de 92,6 °C en los 15 min siguientes y la vuelta a
«lista» se registra. Después, un café para confirmar.

## 6. Consecuencias

- Queda decidido lo único que faltaba decidir del control local.
- Los dos lazos comparten ley, estructura y criterio de puesta en servicio, y se
  diferencian solo en el mecanismo de actuación que su hardware admite.
- La magnitud regulada queda declarada como lo que es —temperatura del cuerpo—,
  de modo que ninguna tolerancia se evalúa sobre una magnitud distinta de la
  medida.
- La protección por software se justifica por conservar intactos unos fusibles
  que no son reajustables, no por duplicarlos.
- La falta de agua deja de ser un modo de fallo sin ninguna defensa, aunque su
  umbral quede pendiente de medición.
- El proyecto asume un trabajo experimental que antes no estaba enunciado: la
  correspondencia entre cuerpo y agua.

## 7. Verificación de la decisión

1. inhibición completa fuera de los estados autorizados y ante cada fallo crítico;
2. correspondencia entre demanda solicitada y tiempo de conducción aplicado,
   incluido el arrastre del residuo entre ventanas;
3. ausencia de conducción durante arranque, reinicio o pérdida de la medida;
4. seguimiento de referencia, sobrepaso, estabilidad y recuperación dentro de
   límites aprobados, sobre la magnitud declarada;
5. actuación segura cuando la temperatura supera la referencia;
6. corte al alcanzar el límite duro, con registro de la causa;
7. ~~disparo de la protección por velocidad de ascenso una vez fijado su umbral~~; descartada el 2026-10-06 (§ 3.7);
8. trazabilidad de muestras, salida, saturaciones y causa de terminación.

## 8. Trazabilidad

- Requisitos: RF-001, RF-007–RF-012 y RF-014; RNF-002, RNF-004, RNF-005 y RNF-007.
- Decisiones relacionadas: ADR-005, ADR-006, ADR-007 y ADR-008.
- Hallazgo que la origina: H-13 del informe de hallazgos del firmware.
- Arquitecturas afectadas: lógica, física y esquema funcional de tres placas.

## 9. Referencias técnicas

- Xinbao — informe constructivo local de la familia CM8502.
- Caracterización del boiler CM8502.
- Caracterización de la cafetera, § 2.3 y § 2.5.
- [Skogestad y Grimholt — *The SIMC Method for Smooth PID Controller Tuning*](https://www.mic-journal.no/PDF/2010/MIC-2010-4-3.pdf).
