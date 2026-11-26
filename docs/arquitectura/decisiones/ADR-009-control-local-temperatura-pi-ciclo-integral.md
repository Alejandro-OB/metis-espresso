# ADR-009 — Control local de temperatura mediante PI unilateral y modulación por ciclo integral

**Estado:** Aceptada, 2026-09-15. Las subsecciones tituladas «enmienda»
conservan su fecha porque el código las cita por ella.

## 1. Contexto

ADR-005 hace de la regulación térmica parte del funcionamiento normal, sin
autoridad para cambiar referencias ni iniciar nada. ADR-007 decide el lazo de
presión; este ADR decide el del calentador.

| Elemento | Dato |
|---|---|
| Caldera | `CM8502`, aluminio, pared mínima 2,8 mm; volumen interno ≈ 152 mL, útil 81 ± 3 mL |
| Calentador | 1050 W a 120 V AC (14,0 Ω medidos, ≈ 1029 W), ≈ 12,7 W/mL |
| Conmutación | SSR-25DA de cruce por cero, driver `2N2222` desde `HEATER_CMD` (ADR-008 § 3.1) |
| Protección física | dos `MICROTEMP G4A00`, `Tf 192 °C`, no reajustables |
| Sensor | NTC sobre el cuerpo de la caldera |

El SSR solo aplica u omite ciclos completos de red y lleva su propia detección
de cruce por cero, así que no hay control por ángulo de fase ni dependencia de
`PUMP_ZC`. Los `G4A00` son fusibles de un solo uso; la protección por software
existe para que nunca actúen. El sensor mide el metal, no el agua, y la
diferencia cambia con el régimen: crece al calentar y cambia de signo cuando
entra agua fría.

## 2. Alternativas consideradas

| Alternativa | Razón |
|---|---|
| Todo o nada con histéresis | Con la densidad de potencia y el retardo entre cuerpo y agua produce sobrepasos y un rizado no acotable. Descartada. |
| Ángulo de fase | Imposible con el SSR de cruce por cero. Descartada. |
| PID completo | La derivada amplificaría el ruido y actuaría sobre el metal, que no es la magnitud de interés. Descartada. |
| Prealimentación desde el caudal | Señal correcta, pero exige validar el caudal estimado. Queda como evolución (`ff-v2`, § 3.4). |
| PI unilateral con modulación por ciclo integral | Elimina el error estacionario, acota la actuación y respeta la modulación que admite el SSR. **Seleccionada.** |

## 3. Decisión

### 3.1 Frontera del lazo

```text
NTC-C8 → divisor y filtro → ADS1115 → validación → filtro digital
                                                    ↓
referencia protegida → limitador de cambio → PI unilateral → ciclo integral → SSR → calentador
                                                    ↑
                          estado operativo y guardián de actuación
```

La referencia sale solo de la configuración efectiva validada. La única salida
del controlador es la demanda térmica.

### 3.2 La magnitud regulada se declara, no se supone

La referencia, la tolerancia y los indicadores de estabilidad se declaran
sobre la temperatura del cuerpo de la caldera y así se nombran en la
configuración, el registro y la interfaz. La relación entre cuerpo y agua se
registra como una transformación versionada y trazable, nunca como un ajuste
del coeficiente del sensor.

#### Enmienda del 2026-10-18 — el perfil pide el agua y la placa calcula la caldera

La temperatura del perfil es la del agua que cae sobre el café. La placa
calcula la referencia de caldera invirtiendo una recta medida con un NTC fijo
en el portafiltro, con agua sola y la media del chorro entre 5 y 19 s. El lazo,
la banda de «lista» y el límite duro siguen sobre el cuerpo. Los coeficientes,
el intervalo de caldera medido y el método van en la sección `water_reference`
del acta de temperatura. La aplican el módulo puro `water_reference`,
`profile_runtime`, el supervisor y la planta simulada
([evidencia](../../engineering/evidencias/temperatura-cierre-2026-10-17.md)).

Sin esa sección, la temperatura del perfil es la de la caldera. Si el agua
pedida exige una caldera fuera del intervalo medido, la placa responde
`temperature-out-of-range`: la recta no se comprobó fuera, y desde 96 °C de
caldera el máximo del chorro pasa del hervor local. La recta no cubre el café,
cuya temperatura sobre la pastilla depende de la molienda (a 44 clics, hasta
9 °C menos que con agua sola).

> **Descartado.** Hacer la cuenta en la plataforma: la placa regula y valida,
> y así la referencia significa lo mismo venga de donde venga el perfil.

#### Enmienda del 2026-10-19 — compensación del primer uso

Tras reposos de 20 min a 4 h, el primer uso sale 2,4–3,9 °C más frío que en uso
continuo; desde el segundo uso vuelve a lo normal. El agua sigue al metal al
pulsar con unos 0,81 °C por °C, así que subir la referencia de caldera lo
compensa. El gemelo de la plataforma predice el déficit y pide el incremento
para la próxima preparación; la placa lo valida y lo aplica, sin predecir.

- **Contrato.** Campo opcional `boilerOffsetC` del perfil, en °C y ≥ 0; sin él
  vale cero. Se suma tras convertir el agua a caldera.
- **Validación.** `profile_runtime` rechaza el perfil entero con
  `boiler-offset-out-of-range` si el valor es negativo, no finito o mayor que el
  máximo, o si la banda de «lista» no queda bajo el límite duro
  (`process_limits_are_valid`).
- **Un solo uso.** Rige hasta que termina la siguiente preparación; los
  purgados no lo consumen, y solo un cambio de identificador, versión o valor
  cuenta como petición nueva. No se guarda en memoria no volátil. Vive en el
  módulo puro `first_use_offset`.
- **«Lista».** Si la caldera queda bajo la nueva referencia menos la banda, se
  publica `coffee-temperature-lost` (`process_supervisor_ready_withdrawn`).
- **Registro.** El resumen de la preparación lleva `boilerOffsetAppliedC`,
  ausente si no consta.

El máximo es `max_offset_c` en la sección opcional `first_use_offset` del acta;
sin ella vale cero. Para medirlo, el binario de identificación admite un máximo
provisional (`CAFE_FIRST_USE_OFFSET_PROVISIONAL`,
`CAFE_FIRST_USE_OFFSET_MAX_DECI_C`, apagado por omisión).

> **Descartadas.** Subir la referencia en la placa según el reposo: la placa no
> sabe cuándo pulsará el barista, y un modelo del reposo duplicaría el gemelo.
> Purgar antes del primer café: gasta agua y depende del barista.

#### Enmienda del 2026-10-27 — refuerzo de la prealimentación en el primer uso

El déficit del primer uso está en la segunda mitad del chorro: desde los 10 s
el agua cae a unos 78 °C tras reposos de 25 y 45 min, frente a unos 86 °C en uso
continuo. Con la ganancia del acta (0,75) el calentador va al 60–64 % durante
el café; con 1,25, al 98–99 %. En bloques cruzados de 25 min el déficit pasa de
3,35 y 3,14 °C a 0,02 y 0,09 °C.

El gemelo pide para la próxima preparación una ganancia mayor en el campo
opcional `firstUseFeedforwardGain`, en % de calentador por % de bomba. El perfil
se rechaza con `first-use-feedforward-out-of-range` si el valor no es finito,
es menor que la ganancia del acta o mayor que `max_feedforward_gain` (sección
`first_use_offset`). Tiene el mismo ciclo de un solo uso que `boilerOffsetC`,
del que es independiente, y el resumen lleva `firstUseFeedforwardGainApplied`.
Solo cambia la ganancia: el corte sin agua y la saturación siguen igual.

Su costo es que la caldera sube a 105–108 °C un minuto después del café, frente
a unos 101 °C, y el café siguiente sale cerca de 1 °C más frío.

> **Descartadas.** Reforzar todos los cafés: los seguidos salen más fríos.
> Atarlo a la subida de la referencia: lo medido es el refuerzo solo.

### 3.3 Controlador PI unilateral

Se adopta la estructura del lazo de presión, porque el actuador también solo
aporta energía. Sin refrigeración activa, una temperatura sobre la referencia
es saturación inferior y no una orden negativa.

```text
e[k] = r[k] - T_filtrada[k]
u_sin_limitar[k] = Kp · e[k] + I[k]
u[k] = limitar(u_sin_limitar[k], 0 %, 100 %)
```

La integral solo se actualiza sin saturación o cuando el error tiende a sacar la
salida de ella. Un limitador de cambio acerca la referencia interna a la
autorizada. Una sola ley para los dos lazos reduce lo que hay que verificar y
comparte la prueba de anti-*windup*.

### 3.4 Prealimentación por entrada de agua fría

Durante la extracción entra agua fría, y el sensor del cuerpo ve la caída tarde
y atenuada. Con una extracción de 25 a 30 s y un retardo de varios segundos, el
realimentado llega tarde por construcción. La perturbación se conoce de
antemano, porque el firmware arranca la bomba, así que se aporta potencia
mientras la bomba está autorizada.

La prealimentación se suma a la salida, no a la referencia. Gaggiuino eleva la
referencia, lo cual sirve sin integral; aquí la integral llevaría el error a
cero contra una referencia falsa y calentaría de más.

```text
u[k] = limitar(Kp·e[k] + I[k] + u_ff[k], 0 %, 100 %)
```

`u_ff` vale cero sin bomba autorizada. La saturación y el anti-*windup* operan
sobre la suma, para que la integral no se cargue compensando una
prealimentación que ya satura. Con el tope del PI (§ 3.12), el PI satura su
parte y la prealimentación se suma después, hasta el 100 %
(`heater_feedforward_apply`); la integral sigue sin verla.

La perturbación vale `ṁ · cp · (T_caldera − T_entrada)`. Se versiona la fuente
de su estimación:

| Versión | Señal | Disponible |
|---|---|---|
| `ff-v1` | Densidad PSM de la bomba | Desde el primer firmware |
| `ff-v2` | Caudal estimado desde densidad y presión, validado contra caudal medido | Tras la campaña de caudal |

La densidad mide el esfuerzo de la bomba, no el caudal, y se declara como
aproximación. `ff-v1` vive en `heater_feedforward`. Su ganancia sale del acta
(sección `feedforward`); sin ella la aportación es cero, y
`CAFE_HEATER_FEEDFORWARD_MILLI` solo existe en el binario de identificación.
Sin prealimentación, la caldera cayó de 91,6 a 83 °C con la bomba al 50 %.

#### Enmienda del 2026-10-17 — sin agua no hay prealimentación

La densidad indica que la bomba trabaja, no que pase agua. Con el depósito
vacío, 27 s de bombeo al 75 % llevaron el metal filtrado de 92,6 a 112,3 °C.
Por eso la aportación se corta, hasta que la bomba para, cuando el metal supera
en más de `ready_tolerance_c` (0,5 °C) el valor que tenía al arrancarla, o
cuando la medida no es finita (`heater_feedforward_step`). En 190 bombeos con
agua ninguno subió más de 0,50 °C; en seco subió 6,3 °C. El corte reduce el
exceso, no lo evita: llega a los 19 s de 27. La báscula y la presión se
descartaron como señales, porque la báscula no siempre está bajo el portafiltro
y la presión en seco (5,4–5,8 bar) se parece a la de un purgado sin canastilla.

### 3.5 Modulación por ciclo integral

La salida se convierte en tiempo de conducción dentro de una ventana de 1 s.
El SSR cuantiza a medios ciclos (8,33 ms a 60 Hz), así que el firmware no
cuenta ciclos: mantiene `HEATER_CMD` activo la fracción de la ventana. El
residuo de redondeo pasa a la ventana siguiente, de modo que la media a largo
plazo sigue la demanda con más resolución que una ventana, como el planificador
PSM de la bomba.

La ventana se elige por el rizado térmico, `ΔT ≈ P · d · T_ventana / C`. Con
81 mL de agua (339 J/K) y 431 g de aluminio a 935 J/(kg·K) (403 J/K; Incropera,
tabla A.1), `C ≈ 742 J/K`, y con una demanda del 20 %:

```text
ΔT ≈ 1029 × 0,20 × 1 / 742 ≈ 0,28 K
```

Una ventana de 5 s daría 1,4 K, del orden de la tolerancia. El desgaste no es
criterio: el SSR no tiene contactos.

### 3.6 Periodos y filtrado

La temperatura se adquiere cada 100 ms (uno de cada diez ciclos de la tarea de
10 ms), el PI corre cada 200 ms y el filtro es de primer orden con `τf = 1 s`,
ajustable con registros reales. La temperatura no necesita la cadencia de la
presión: su constante de tiempo es de decenas de segundos. El firmware conserva
la muestra cruda, la convertida y la filtrada como señales distintas, y el
filtro no oculta una entrada inválida, saturada o fuera del límite duro.

### 3.7 Seguridad e inhibiciones

El guardián mantiene `HEATER_CMD` inactivo si el estado no autoriza regulación
térmica, si el control no está puesto en servicio, si la adquisición incumple
su plazo, si la temperatura no es apta, si se supera el límite duro o durante
arranque, reinicio o actualización.

**Límite duro.** Protege a los `G4A00`: el software corta con margen para que
nunca se abran. El firmware trae un límite de banco de 150 °C sobre el cuerpo,
42 °C bajo los fusibles, y el acta fija el de operación (`hard_limit`). Sin
función de vapor (ADR-006), ningún uso lleva el cuerpo sobre la temperatura de
extracción.

**Velocidad de ascenso: descartada.** Con agua, la capacidad es de unos
750 J/K y a plena potencia el cuerpo sube 1050 W / ≈750 J/K ≈ 1,4 K/s. La
caldera vacía, de 485 g, tiene unos 400 J/K y subiría unos 2,6 K/s: menos del
doble, y el transitorio del metal confunde ambos casos. La caldera seca queda
cubierta por el límite duro y los `G4A00`, en un prototipo que opera
supervisado.

**Puesta en servicio.** El firmware arranca con
`temperatureControlCommissioned = false`, `Kp = 0` y `Ki = 0`. Sin parámetros
identificados, validados y versionados, ninguna operación ordinaria energiza el
calentador. La identificación usa el modo restringido de la § 5.

### 3.9 Registro obligatorio

Cada prueba del lazo conserva, con tiempo monotónico: temperatura cruda,
convertida y filtrada; referencia autorizada e interna; error; integral y
salida antes y después de limitar; demanda y tiempo de conducción aplicado;
residuo; estado, calidad de la medición y veredicto del guardián; velocidad de
ascenso; y causa de terminación o protección.

### 3.10 Diagnóstico de conducción sin orden

El fallo dominante del SSR es el cortocircuito, y es el único medio de corte del
calentador. El firmware pierde la salida pero no la medida. Con la orden en
cero durante una ventana de gracia de 30 s y la medida apta, un ascenso
sostenido de 0,3 °C/s durante 10 s indica conducción sin orden. Los valores
salen de cortes desde plena potencia: el arrastre llega a su pico a los 25 s y
pasados 20 s sube menos de 0,03 °C/s, frente a 1,2–1,4 °C/s a plena potencia.
El diagnóstico no está programado.

El diagnóstico se enclava hasta un arranque posterior a la intervención. No
corta el calentador, porque el elemento de corte es el que falló: inhibe la
bomba, cierra la sesión y rechaza órdenes nuevas. Así informa al barista de la condición insegura e inhibe las
salidas que el hardware permite. Detrás quedan la desconexión por el operador
(ADR-008) y los `G4A00`. Un segundo medio de corte en serie se descartó porque
obliga a rehacer `PCB-POTENCIA` y `PCB-CONTROL`.

El LED de alarma usa el parpadeo rápido, `INDICATOR_FAST`, reservado a esta
condición; el lento indica que el guardián cortó las salidas y el fijo,
`OP_STATE_ALARM`. El rápido es el único que pide una acción física:
desconectar de la red. La vigilancia corre siempre que haya alimentación, y el
evento se archiva con la serie de temperatura anterior y posterior.

### 3.11 Arranque rápido (enmiendas del 2026-10-11 y del 2026-10-13)

El tope del 10 % del PI evita el sobrepaso desde frío: en el modelo del lazo
completo (`test_control_loop.c`) se llega a 92 °C sin sobrepaso con el 10 % y a
106,5 °C con el 100 %. El precio son 17 min de 25,4 °C a «lista».

Antes del PI se añade una fase al 100 % hasta `r − δ`, con `r` la referencia.
Luego el calentador se apaga y, cuando la temperatura deja de subir, el PI
toma el mando con la integral en cero. La fase actúa solo en frío, bajo
`r − δ − Δ_activación`, sin bomba autorizada, con la referencia bajo `r_máx` y
una vez por calentamiento. Desde tibio el arrastre crecía (12,1 °C desde
25,8 °C, frente a 19,4 y 22,6 °C desde unos 55 °C) y la fase no adelantaba la
«lista». Un tiempo máximo `t_máx` acota la energía; salir de «calentando»,
perder la medida apta o autorizar la bomba abandona la fase. Cada línea `T`
registra la fase.

Los parámetros están en la sección `fast_start` del acta y sin ellos la fase no
se arma. Se midieron con la propia fase, porque el modo restringido no se arma
sobre un lazo con acta (`identification_mode_configure`).

> **Descartadas.** Calcular `δ` en el firmware según la partida: mete un modelo
> en una decisión de seguridad. Pasar al PI en el corte: el proporcional
> sumaría `Kp·δ` al arrastre. Cortar en 77 °C: la «lista» llegó más tarde.
> Precargar la integral: deja el cuerpo 2 °C sobre la referencia y retrasa la
> estabilidad de 8,6 a 16 min.

### 3.12 Tope del PI y puertas (enmienda del 2026-10-12)

El tope del PI solo existe porque el PI se pasa desde frío. Se mantiene y se
abren tres puertas, cada una con su protección:

| Puerta | Hasta | Cuándo | Qué la cierra |
|---|---|---|---|
| 1. Arranque rápido | 100 % | Solo en su fase | Corte en `r − δ`, `t_máx`, salida de «calentando» |
| 2. Prealimentación | Tope del PI más `u_ff`, sin pasar de 100 % | Con la bomba autorizada | `u_ff` vuelve a cero en el ciclo en que se retira la bomba |
| 3. PI | El tope del acta | Siempre | — |

> **Descartado.** Subir el tope del PI durante la extracción: podría pasarse al
> parar la bomba, con la integral cargada.

### 3.13 La integral tras la extracción (enmiendas del 2026-10-16)

Tras cada café el metal pasaba a 93,3–93,9 °C hacia los 9 min, porque la
integral se carga durante la recuperación y se descarga lentamente. Se probaron
dos leyes y ninguna rige; su código queda apagado.

- **Retener la integral** hasta volver a la referencia (`heater_recovery_hold`,
  `CAFE_HEATER_RECOVERY_HOLD`): quitó el sobrepaso, pero la vuelta a «lista»
  tardó de 2,5 a 7 veces más.
- **Devolverla** al valor previo al café según
  `dI/dt = Ki·e − (I − I0)/τ_eq` (`heater_recovery_return`,
  `CAFE_HEATER_RECOVERY_RETURN_TAU_S`): con `τ_eq` = 533 s no cumplió el
  criterio de 92,6 °C en ningún uso. Como el criterio pasó a ser el agua
  (§ 3.2) y el control sin estas leyes lo cumple, no se adoptó.

> **Descartadas también.** Subir `Kp`: en la planta del acta, con 44 s de
> retardo, oscila. Subir `Ki`: el máximo subió a 94,36 °C. Vaciar la integral
> al cruzar la referencia: quita la potencia de sostenimiento.

## 4. Parámetros reservados a la puesta en marcha

Se obtienen con la caldera instrumentada: `Kp` y `Ki`, la constante del filtro,
la velocidad del limitador, el límite duro de operación, la ganancia de la
prealimentación, la tolerancia de estabilidad y el sobrepaso aceptable, la
correspondencia entre cuerpo y agua, los valores del diagnóstico de conducción
sin orden, los del arranque rápido y los máximos del primer uso. Ninguno
bloquea el esquema ni la estructura del firmware; todos bloquean dar
por validada la regulación de temperatura.

## 5. Procedimiento de puesta en marcha

- **Paso 1.** Antes de energizar, verificar aislamiento, montaje del SSR,
  continuidad de los fusibles térmicos y estado seguro de `HEATER_CMD`.
- **Paso 2.** Confirmar que el SSR conduce y corta, con el binario de
  identificación (`heater-step-1pct-v1`, tope del 1 %) sobre el calentador real
  y la mano en el enchufe. Un programa de banco fuera del firmware quedaría
  fuera del guardián.
- **Paso 3.** Registrar un escalón en lazo abierto desde temperatura ambiente
  y estimar ganancia, retardo y constante de tiempo.
- **Paso 4.** Comparar constantes de filtro candidatas (0,5, 1 y 2 s) y escoger
  la menor que reduzca el ruido sin ocultar el ascenso.
- **Paso 5.** Sintonizar con SIMC, como el lazo de presión, con `λ`
  conservador:

  ```text
  Kp = τ / [K·(λ + θ)]
  Ti = min(τ, 4·(λ + θ))
  Ki = Kp / Ti
  ```

- **Paso 6.** Empezar con `Ki = 0`, subir `Kp` poco a poco y solo después
  incorporar la integral.
- **Paso 8.** Caracterizar la extracción, cuando entra agua fría.
- **Paso 9.** Congelar valores, versión, condiciones y criterios en el acta
  antes de poner `temperatureControlCommissioned` en verdadero.

El paso 7 desapareció con la protección por velocidad de ascenso. No se usa
sintonía por relé ni Ziegler–Nichols, porque provocan oscilaciones sostenidas.

**Modo restringido (enmienda del 2026-09-21).** Los pasos 3 a 8 exigen registros
que el guardián impediría. Se adopta el modo restringido de ADR-007 § 5; los
dos lazos comparten guardián, tarea y registro. A diferencia de la bomba, el
calentador no tiene un límite de servicio que el guardián vigile, así que la
secuencia térmica declara su propio criterio de parada por pendiente y el
operador permanece delante con la alimentación a la vista.

## 6. Consecuencias

Los dos lazos comparten ley y puesta en servicio. La temperatura del agua se
obtiene por una transformación medida y versionada. La caldera seca no tiene
defensa propia más allá del límite duro y los `G4A00`, y el prototipo depende
de la supervisión.

## 7. Verificación de la decisión

1. inhibición completa fuera de los estados autorizados y ante cada fallo
   crítico;
2. correspondencia entre demanda y tiempo de conducción, incluido el residuo
   entre ventanas;
3. ausencia de conducción durante arranque, reinicio o pérdida de la medida;
4. seguimiento, sobrepaso, estabilidad y recuperación dentro de límites
   aprobados, sobre la magnitud declarada;
5. actuación segura cuando la temperatura supera la referencia;
6. corte en el límite duro, con registro de la causa;
7. trazabilidad de muestras, salida, saturaciones y causa de terminación.

## 8. Trazabilidad

- Decisiones relacionadas: ADR-005, ADR-006, ADR-007 y ADR-008.
- Última acta de temperatura publicada:
  [acta-temperatura-20261028b.json](../../engineering/actas/acta-temperatura-20261028b.json).

## 9. Referencias técnicas

- [Skogestad y Grimholt — *The SIMC Method for Smooth PID Controller Tuning*](https://www.mic-journal.no/PDF/2010/MIC-2010-4-3.pdf).
- Incropera et al., *Fundamentals of Heat and Mass Transfer*, tabla A.1.
