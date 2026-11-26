# ADR-007 — Control local de presión mediante PI unilateral y PSM

**Estado:** Aceptada el 2026-08-30.

## 1. Contexto

La presión se regula durante una extracción que el barista inicia físicamente.
El hardware ya está elegido: transductor HK1100C, convertidor ADS1115 con
adquisición cada 10 ms, bomba vibratoria JYPC-503A (marcada `ID2/1min`, con
protector térmico) y módulo de potencia con TRIAC y detección aislada de cruce
por cero. La bomba puede subir la presión, pero la máquina no tiene una
electroválvula para bajarla, y el control debe ser seguro cuando la presión
supera la referencia.

Ni la FMU, ni el gemelo digital, ni el servidor, ni la ontología entran en el
lazo. El lazo tampoco usa caudal: un caudal estimado es un cálculo derivado y
se etiqueta con su origen.

## 2. Alternativas consideradas

| Alternativa | Resultado |
|---|---|
| Algoritmo de Gaggiuino | Descartado: depende de un caudal estimado y de parámetros de otra planta hidráulica. |
| Proporcional | Descartado: puede dejar error estacionario. |
| PID completo | Descartado: la derivativa amplifica el ruido. |
| PI unilateral | **Seleccionado**: usa solo referencia y presión filtrada, y elimina el error estacionario. |
| Ángulo de fase | Descartado: corta ondas parciales, empeora la compatibilidad electromagnética y es difícil de reproducir. |
| Modulación por densidad de pulsos (PSM) sincronizada con el cruce por cero | **Seleccionada**: aplica u omite actuaciones completas con una salida media graduable. |

## 3. Decisión

### 3.1 Frontera del lazo

El lazo se ejecuta por completo en la ESP32-S3:

```text
HK1100C → divisor y filtro → ADS1115 → validación → filtro digital
                                                    ↓
referencia protegida → limitador de cambio → PI unilateral → PSM → módulo dimmer → JYPC-503A
                                                    ↑
                              estado y supervisor de seguridad
```

La referencia sale solo de la configuración efectiva validada. El controlador no
elige perfiles, no cambia referencias ni inicia extracciones.

El transductor está en una derivación en T entre la bomba y la caldera, así que
el lazo regula la presión a la salida de la bomba y no la presión sobre el café
del requisito de regulación de presión. Se conserva ese punto porque es el montaje existente, es donde se
caracteriza la bomba y es donde miden las máquinas comerciales. La diferencia
entre ambas presiones no es constante: crece con el cuadrado del caudal
(`Δp = (ρ/2)·(Q/(Cd·A))²`). Se mide barriendo la orden de bomba con el
portafiltro vacío (ensayo E1), con caudal y presión a la vez, y entra al modelo
como pérdida dependiente del caudal.

### 3.2 Controlador PI unilateral

```text
e[k] = r[k] - p_filtrada[k]
u_sin_limitar[k] = Kp · e[k] + I[k]
u[k] = limitar(u_sin_limitar[k], 0 %, 100 %)
```

La integral se actualiza solo si la salida no está saturada o si el error tiende
a sacarla de la saturación (anti-*windup*). Con la presión por encima de la
referencia la salida baja hasta 0 %; el sistema no afirma poder descargar
presión. Un limitador de cambio acerca la referencia interna a la autorizada,
para no aplicar el 100 % de golpe.

### 3.3 Filtrado y periodos iniciales

| Elemento | Valor | Carácter |
|---|---:|---|
| Adquisición de presión | 10 ms | Cerrado |
| Ejecución del PI | 20 ms | Inicial; el *jitter* se verifica en la puesta en marcha |
| Filtro digital | Primer orden, `τf = 50 ms` | Candidato inicial |
| Coeficiente a 10 ms | `α = Ts / (τf + Ts) ≈ 0,167` | Derivado |
| Salida lógica | 0–100 %, resolución 1 % | Cerrado |
| Densidad mínima forzada | Ninguna | Cerrado: no se supone una zona muerta sin medirla |

El firmware conserva la muestra cruda, la presión convertida y la filtrada como
señales distintas. El filtro no puede ocultar una entrada inválida, saturada o
por encima del límite duro.

### 3.4 Modulación de la bomba

Un acumulador PSM convierte `u` en densidad de actuaciones sincronizadas con
`PUMP_ZC`; cada oportunidad se aplica u omite completa. Una orden de 0 % no
dispara nunca y una de 100 % usa todas las oportunidades. No se impone una
salida mínima: una zona sin respuesta se documenta como característica de la
planta.

**Enmienda del 2026-10-11: la actuación útil es un ciclo completo.** La bomba
lleva un diodo en serie y solo trabaja con una polaridad. Decidiendo por
semiciclo, toda densidad de periodo par (1, 2, 5, 10, 50 %) cae entera en una
polaridad que depende del arranque, y la mitad de las veces la bomba no recibe
nada aunque el registro cuente los disparos. El planificador decide ahora en el
primer semiciclo de cada pareja y repite la decisión en el segundo
(`psm_scheduler_half_cycle`); el acumulador cuenta ciclos. Se descartó detectar
la polaridad, porque el cruce por cero del módulo no la distingue. Los escalones
registrados antes de esta enmienda no se mezclan con los posteriores.

### 3.5 Seguridad e inhibiciones

`PUMP_CMD` queda inactivo si:

- no hay una extracción o una purga autorizadas;
- la presión es inválida, imposible o está saturada;
- se pierde `PUMP_ZC` o su frecuencia sale del intervalo permitido;
- se supera el límite duro de presión;
- el supervisor inhibe la actuación o falta la alimentación lógica;
- la adquisición o el control incumplen su plazo de forma peligrosa;
- la ESP32-S3 está arrancando, reiniciando o actualizándose;
- la bomba lleva dos minutos continuos habilitada o no ha cumplido el minuto de
  reposo posterior, medidos con reloj monotónico sea cual sea la densidad. Es
  una lectura conservadora de `ID2/1min` que no depende del protector térmico.

El firmware arranca con `pressureControlCommissioned = false`, `Kp = 0` y
`Ki = 0`. Sin parámetros identificados, validados, versionados y protegidos por
integridad, una extracción no energiza la bomba; solo el modo restringido de
§ 5 aplica estímulos acotados. La purga usa una demanda limitada propia, no el PI.

### 3.6 Registro obligatorio

Cada prueba del lazo conserva, con tiempo monotónico: presión cruda y filtrada;
referencia autorizada y limitada; error; término integral y salida PI antes y
después de limitar; densidad pedida y actuaciones emitidas; estado operativo,
calidad de la medida e inhibiciones; tiempo continuo de habilitación y reposo de
la bomba; y causa de terminación o protección. Con él se reproduce el
comportamiento sin caudal estimado ni plataforma externa.

## 4. Parámetros reservados para la puesta en marcha

Solo se obtienen con la bomba en el circuito real: `Kp` y `Ki`; constante del
filtro y velocidad del limitador; zona sin respuesta; límites de operación,
tolerancia de estabilidad y límite duro; sobreimpulso y error aceptables; y
comportamiento de bomba, módulo y cruce por cero con carga hidráulica. No
bloquean el esquema ni la estructura del firmware; sí bloquean habilitar el
lazo presurizado y dar por validada la regulación de presión.

## 5. Procedimiento de puesta en marcha

1. Verificar mecánica, aislamiento, protecciones, fugas, cero del sensor,
   respuesta monótona, saturación del ADC, parada segura y el protector de la
   bomba, sin puentearlo.
2. Probar `PUMP_ZC` y los disparos con una carga segura antes de presurizar.
3. Registrar escalones conservadores con agua circulando; no contra una salida
   bloqueada sin protección mecánica de sobrepresión.
4. Comparar fuera de línea filtros de 20, 50 y 100 ms y elegir el menor que
   reduzca el ruido sin ocultar el ascenso ni el sobreimpulso.
5. Estimar ganancia, retardo y constante de tiempo. Para
   `G(s) = K·e^(-θs)/(τs+1)`, partir de SIMC:

   ```text
   Kp = τ / [K·(λ + θ)]
   Ti = min(τ, 4·(λ + θ))
   Ki = Kp / Ti
   ```

   Para una planta integradora (`dp/dt = k·Δu`):

   ```text
   Kp = 1 / [k·(λ + θ)]
   Ti = 4·(λ + θ)
   Ki = Kp / Ti
   ```

   `λ` se elige conservador: mayor `λ`, respuesta más lenta.
6. Empezar con `Ki = 0`, subir `Kp` gradualmente y solo después añadir la
   integral.
7. Buscar una zona sin respuesta con órdenes de 1, 2, 3, 5 y 10 %.
8. Validar con varias resistencias de la cama de café y repetir tras cambiar
   filtro, bomba, tubería o sensor.
9. Congelar valores, versión, condiciones y criterios de aceptación antes de
   poner `pressureControlCommissioned` en verdadero.

Se descartan la sintonía por relé y Ziegler–Nichols porque exigen oscilaciones
sostenidas en una planta cuya protección aún se valida.

#### Enmienda del 2026-09-21: el modo restringido

Los pasos 3 a 7 exigen actuar, pero el guardián corta todo mientras
`pressureControlCommissioned` sea falso, y esa marca solo cambia con los
registros de esos pasos. Un microcontrolador de banco no rompe el círculo: no
atraviesa la cadena de medida ni produce el registro de § 3.6.

Se decide un modo restringido de lazo abierto, que sustituye al controlador:

- relaja solo la inhibición por control sin poner en servicio; las demás cortan
  igual que en una extracción;
- no ejecuta el PI: `Kp` y `Ki` siguen en cero y la salida la fija una secuencia
  de estímulo versionada, elegida en compilación, que termina sola y deja su
  identificador en el registro;
- exige un binario distinto del de operación y una confirmación física en el
  mando en cada arranque;
- el tope lo impone el guardián, no la secuencia, porque quien genera el
  estímulo no puede juzgar si es admisible;
- usa su propio veredicto (`allowed-identification`), que el firmware consulta
  con `actuation_verdict_allows`.

Se descartaron poner la marca en verdadero para probar (anula la barrera contra
ganancias sin identificar), autorizar desde la plataforma (pondría en la red una
orden capaz de energizar la máquina, ADR-012) y relajar otra inhibición.
Implementado en `identification_mode` y `stimulus_sequence`.

#### Enmienda del 2026-10-10: la escalera de la bomba en un solo binario

Recompilar por escalón costaba unos 8 minutos para 12 s de bomba. El binario
`pump-ladder-v1` arranca en el 1 % y admite `B,step,<n>` por USB. El módulo puro
`pump_ladder` exige la secuencia anterior terminada, solo permite repetir o subir
un escalón, mantiene una confirmación física por arranque y un tope compilado
que cubre el 75 %, y pide una pulsación nueva de café por escalón
(`pump_ladder_may_arm`).

#### Enmienda del 2026-10-12: sintonía de la presión en caliente

Como ADR-020 § 4.2 para el lazo térmico, `λ` y la integral se ajustan por USB,
solo en el binario de identificación y tras la confirmación física; la planta no
se toca por el puerto. La planta de partida sale de la
[escalera con café](../../engineering/evidencias/escalera-con-cafe-2026-10-11.md):
primer orden con retardo, `K = 0,06 bar/%` (cota superior), `τ = 0,1 s`,
`θ = 0,1 s` y `λ` inicial de 1 s. Se descartó la forma integradora porque del 10
al 30 % la presión llega a su meseta en 0,3 s y se queda quieta; la pendiente
lenta del 75 % daría `Kp ≈ 57 %/bar` y haría oscilar el lazo.

El techo es el 75 %, el escalón más alto medido con café (10,3 bar), en la
secuencia `pump-tuning-75pct-v1` de 25 s (1 s en cero, 23 s con techo, 1 s en
cero), rearmada con `B,step,75`. El PI no calcula mientras el techo es cero. La
referencia es la del perfil con un limitador de 3 bar/s, y el filtro es el de
20 ms elegido en el paso 4. Se descartaron subir el techo al 100 %, sin registro
con café por encima del 75 %, y ajustar `K` o `τ` por el puerto, que es cambiar
la planta.

#### Enmienda del 2026-10-12, tarde: preinfusión a presión de extracción

Con la molienda más fina la presión llegó a 11,6 bar con `λ = 1 s` y a 12,3 bar
con `λ = 2 s`, donde cortó el límite duro. Mientras la pastilla se moja, la
canastilla presurizada fija la presión en 6,6–7 bar y la integral sube hasta el
techo; al cerrar la pastilla, la presión sobrepasa.

Se decide una preinfusión que termina por presión (`pressure_preinfusion`): la
bomba va al techo sin PI hasta que la presión, con un filtro propio de 300 ms,
llega a la referencia menos 0,5 bar. Entonces arranca el PI con la integral en
el 15 %, salvo que la presión ya esté sobre la referencia, en cuyo caso arranca
en cero. Una simulación en 60 situaciones dio picos de 9,2–9,8 bar sin cortes.
Se descartaron la preinfusión de tiempo fijo (una pastilla lenta cierra con la
integral en el techo), la de baja potencia (la referencia llega hacia los
18–20 s) y ajustar `λ`, bajar el techo o añadir derivativo. El margen y la
integral inicial se fijan en el
[acta de presión](../../engineering/actas/acta-presion-20261012.json).

#### Enmienda del 2026-10-16: ensayo hidráulico E2 con las actas aprobadas

E2 mide la curva presión-caudal de la bomba con órdenes fijas, sin PI. El modo
restringido responde `already-commissioned` con el acta compilada, y el firmware
de operación no arranca la bomba por debajo de la presión atrapada (4–6 bar). Se
decide el binario `CONFIG_CAFE_HYDRAULIC_TEST`: no compila sin las dos actas,
conserva los límites del acta (corte a 12 bar), elige con `H,order,<n>` entre
tres órdenes que no superan el 75 %, fija la orden al abrir la extracción y
marca cada línea `P` con `hydraulic-test-v1`. Se descartaron quitar el acta para
usar el modo restringido y relajar `already-commissioned`. Lógica en
`hydraulic_test`, probada en `test_hydraulic_test.c`; un equipo desplegado lleva
siempre el binario de operación.

#### Enmienda del 2026-10-20: referencia de operación de 8,5 bar

Los perfiles usan 8,5 bar y no 9 bar, porque los golpes de la bomba
(±0,4–0,9 bar) llevan la presión por encima en los picos:

| Referencia | Molienda | Pico | Bomba en régimen |
|---|---|---|---|
| 8,5 bar | 44 / 38 / 32 clics | 8,9 / 8,8 / 9,0 bar | 40–59 % |
| 9,0 bar | 32 clics | 9,4 bar | 64–66 % |

Es el valor de esta máquina con canastilla presurizada de dos tazas, no un
límite: el perfil admite de 1 a 11 bar.

## 6. Consecuencias

- El control no depende de caudal, servidor, FMU ni ontología.
- La presión por encima de la referencia es saturación inferior de una planta
  unilateral.
- Las ganancias salen de la respuesta real y cambiarlas no exige tocar el
  hardware.
- La diferencia entre la presión regulada y la del café se mide antes de
  comparar series medidas y simuladas.

## 7. Verificación de la decisión

1. Inhibición completa fuera de extracción y ante cada fallo crítico.
2. Correspondencia entre densidad pedida y actuaciones emitidas.
3. Ausencia de disparos al arrancar, al reiniciar o sin cruce por cero.
4. Seguimiento, sobreimpulso, estabilidad y recuperación dentro de límites
   aprobados.
5. Actuación segura con la presión por encima de la referencia.
6. Trazabilidad de muestras, salida, saturaciones y causa de terminación.
7. Comportamiento con varias resistencias hidráulicas.
8. Corte a los dos minutos continuos, inhibición durante el reposo y registro de
   ambas causas.

## 8. Referencias técnicas

- [Gaggiuino `pump.cpp`, revisión `bddc57a`](https://github.com/Zer0-bit/gaggiuino/blob/bddc57a63552b921a9f54024e41bc04965fa8dd8/src/peripherals/pump.cpp).
- [PSM Library, rama `feature/timer`](https://github.com/banoz/PSM.Library/tree/feature/timer).
- [RobotDyn AC Light Dimmer](https://robotdyn.com/ac-light-dimmer-module-1-channel-3-3v-5v-logic-ac-50-60hz-220v-110v).
- [Skogestad y Grimholt, *The SIMC Method for Smooth PID Controller Tuning*](https://www.mic-journal.no/PDF/2010/MIC-2010-4-3.pdf).
