# ADR-007 — Control local de presión mediante PI unilateral y PSM

**Estado:** Aceptada, revisada
**Fecha de decisión:** 2026-08-30  
**Última revisión:** 2026-10-10
**Ámbito:** regulación local de presión, actuación de la bomba, seguridad y puesta en marcha

## 1. Contexto

La arquitectura vigente exige regular la presión durante una extracción iniciada físicamente por el barista. Ya se seleccionaron el transductor HK1100C, el ADS1115, el periodo de adquisición de 10 ms, la bomba vibratoria JYPC-503A y un módulo de potencia con TRIAC y detección aislada de cruce por cero. La bomba inspeccionada está marcada `ID2/1min` y conserva un protector térmico externo sobre la bobina. Faltaba definir el algoritmo de control y la forma de traducir su salida en actuaciones sobre la bomba.

El proyecto no utilizará la FMU, el gemelo digital, el servidor o la ontología dentro del lazo de control. Por ello no resulta aplicable copiar el controlador heurístico de Gaggiuino, cuya prealimentación depende de un flujo suavizado estimado a partir de pulsos de bomba y presión.

> **Revisión del 2026-09-14.** La formulación original añadía que «el proyecto no
> medirá ni estimará flujo». Esa frase se retira. El modelado de la presión
> demostró que sin caudal no puede separarse la resistencia de la cama en
> `P = R · q`, y ADR-006 admitió una báscula externa de ensayo para medirlo
> durante la caracterización. Con esos datos podrá además ajustarse y **validarse**
> un estimador de caudal a partir de la densidad PSM y la presión.
>
> Lo que **no** cambia es esta decisión: el lazo de presión no usa caudal. Su
> entrada sigue siendo la presión filtrada y nada más. El caudal es una variable
> de análisis y, eventualmente, una prealimentación del lazo **térmico**
> (ADR-009). Cambiar dos lazos a la vez no sería defendible, y el argumento
> original contra copiar a Gaggiuino se mantiene intacto: sus parámetros
> pertenecen a otra planta hidráulica.
>
> Un caudal estimado es un **cálculo derivado, no una medición**, y así se
> etiqueta con su origen y su versión de calibración, como exige RNF-008.

La bomba puede aumentar la presión al entregar energía hidráulica, pero la Kalley K-EX no dispone de una electroválvula mediante la cual el controlador pueda reducirla activamente. El control debe reflejar esta asimetría y permanecer seguro cuando la presión medida sea mayor que la referencia.

## 2. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Copiar el algoritmo de Gaggiuino | No seleccionada. Depende de una estimación de flujo que está fuera del alcance y de parámetros propios de otra planta hidráulica. |
| Control proporcional | Es simple y será útil durante la identificación inicial, pero puede conservar error estacionario. No se adopta como solución final. |
| PID completo | La acción derivativa amplifica ruido y no se justifica antes de caracterizar la planta. Se pospone. |
| PI unilateral de presión | Seleccionada. Utiliza únicamente referencia y presión filtrada, elimina error estacionario y permite limitar explícitamente la actuación. |
| Control por ángulo de fase | No seleccionado para el prototipo inicial. Introduce cortes parciales de onda, mayor complejidad EMC y una relación de actuación más difícil de reproducir. |
| Modulación por densidad de pulsos sincronizada con cruce por cero | Seleccionada. Aplica u omite oportunidades completas de actuación y conserva una salida promedio graduable. |

## 3. Decisión

### 3.1 Frontera del lazo

El lazo se ejecutará por completo en la ESP32-S3 y tendrá esta cadena:

```text
HK1100C → divisor y filtro → ADS1115 → validación → filtro digital
                                                    ↓
referencia protegida → limitador de cambio → PI unilateral → PSM → módulo dimmer → JYPC-503A
                                                    ↑
                              estado y supervisor de seguridad
```

La referencia procede exclusivamente de la configuración efectiva ya validada. El controlador no elige perfiles, no cambia referencias, no inicia extracciones y no ejecuta fases autónomas de una receta.

**Punto de medida, anotado el 2026-09-15.** Esta decisión describe la cadena desde el HK1100C, pero no declaraba **dónde** está el transductor en el circuito hidráulico. Está en una derivación en T **entre la bomba y la caldera**, que es el montaje existente.

De ahí se sigue qué regula el lazo realmente: **la presión a la salida de la bomba**, que es la suma de las caídas de todo lo que hay aguas abajo —los pasos internos de la caldera, el grupo, la ducha y la pastilla de café—. No es la presión sobre el café, que es la magnitud que nombra RF-010 y la que publica el modelo de simulación.

Se conserva ese punto y no se traslada al grupo, por tres razones. Es el montaje que existe. Es donde se caracteriza la bomba, que es el ensayo pendiente (E1 y E2 del modelado de la presión). Y es lo que hacen las máquinas comerciales con manómetro, de modo que la magnitud regulada es la habitual en el oficio y no una particularidad del prototipo.

Lo que la elección obliga es a **declarar la diferencia en lugar de ignorarla**.

**Corrección del 2026-09-21: esa diferencia no es constante.** La formulación
anterior decía que la caída «se resta» y que produce un desplazamiento constante.
Es falsa, y el documento de ingeniería ya la había corregido el mismo día en que
este apartado se escribió, sin que la corrección llegara hasta aquí. La caída a
través de una restricción escala con el **cuadrado del caudal** —ecuación de
orificio, `Δp = (ρ/2)·(Q/(Cd·A))²`—, de modo que entre el punto de medida y la
pastilla hay **una curva y no un número que se reste**. La regla del oficio,
«alrededor de un bar», es un solo punto de esa curva: el que corresponde al caudal
de una extracción normal, y se dispara a caudales altos. Ver modelado de la
presión § 3.1.1 y el apartado B de
revisión de medición sin trazabilidad,
que la documenta como error metodológico frecuente en la literatura gris.

De ahí que la diferencia se **mida barriendo el mando** con portafiltro vacío en
E1 —que es lo que entrega la curva en vez de un punto— y se **incorpore al modelo**
como término de pérdida dependiente del caudal, que es la clase de ecuación que
hoy le falta a la cadena hidráulica.

La corrección tiene una consecuencia sobre el orden de los ensayos que la
formulación anterior escondía: corregir la diferencia **exige medir caudal**, no
solo presión. Restar un número habría bastado con el transductor; medir una curva
contra el caudal no se puede sin la celda de carga registrando en simultáneo. Es
una razón más para que la celda sea el bloqueante real de la campaña hidráulica y
no un instrumento accesorio.

Mientras esa curva no exista, una comparación físico-virtual de presión enfrenta
dos magnitudes distintas, y la discrepancia **no es un error del modelo**.

### 3.2 Controlador PI unilateral

Para cada ejecución del control:

```text
e[k] = r[k] - p_filtrada[k]
u_sin_limitar[k] = Kp · e[k] + I[k]
u[k] = limitar(u_sin_limitar[k], 0 %, 100 %)
```

La integral se actualizará solo cuando la salida no esté saturada o cuando el error tienda a sacar la salida de la saturación. Esta condición constituye el anti-*windup*. Cuando la presión sea superior a la referencia, la salida puede bajar hasta 0 %, pero el sistema no afirmará que puede descargar activamente la presión.

La referencia interna se aproximará a la referencia autorizada mediante un limitador de cambio para evitar una aplicación instantánea del 100 %. La velocidad definitiva de esa transición se determinará con la planta montada.

### 3.3 Filtrado y periodos iniciales

| Elemento | Decisión para implementar | Estado |
|---|---:|---|
| Adquisición de presión | 10 ms | Cerrada para construcción. |
| Ejecución del PI | 20 ms | Valor inicial cerrado para firmware; se verificará el *jitter* durante la puesta en marcha. |
| Filtro digital | Primer orden, `τf = 50 ms` | Valor inicial implementable; puede ajustarse con registros reales sin cambiar hardware. |
| Coeficiente a 10 ms | `α = Ts / (τf + Ts) ≈ 0,167` | Derivado del valor inicial. |
| Salida lógica | 0–100 % | Cerrada. |
| Resolución solicitada | 1 % | Cerrada para el planificador PSM. |
| Densidad mínima forzada | Ninguna | Cerrada. No se inventa una zona muerta antes de medirla. |

El firmware conservará siempre la muestra cruda, la presión convertida y la presión filtrada como señales distintas. El filtro no puede ocultar una entrada inválida, saturada o fuera del límite duro de seguridad.

### 3.4 Modulación de la bomba

La salida `u` se convertirá en densidad de actuaciones mediante un acumulador PSM. En cada oportunidad sincronizada con `PUMP_ZC`, el planificador decidirá aplicar u omitir una actuación útil completa; no recortará una onda mediante ángulo de fase. La polaridad y la secuencia útil que admita el módulo y la bomba reales se verificarán en banco antes de presurizar el conjunto.

Una solicitud de 0 % no genera disparos. Una solicitud de 100 % utiliza todas las oportunidades autorizadas. Los valores intermedios distribuyen actuaciones en el tiempo. No se impondrá una salida mínima distinta de cero: si durante la puesta en marcha se demuestra una zona sin respuesta, esta se documentará como característica de la planta y se tratará sin alterar el significado de 0 %.

### 3.5 Seguridad e inhibiciones

`PUMP_CMD` deberá permanecer inactivo cuando ocurra cualquiera de estas condiciones:

- no existe una extracción válidamente autorizada o, si la extensión de purga
  se habilita, una purga válidamente autorizada;
- la presión es inválida, imposible o se encuentra saturada;
- se pierde `PUMP_ZC` o su frecuencia queda fuera del intervalo permitido;
- se supera el límite duro de presión;
- el supervisor inhibe la actuación o falta la alimentación lógica;
- la tarea de adquisición o control incumple su plazo de forma peligrosa;
- la ESP32-S3 está en arranque, reinicio o actualización.
- se alcanzan dos minutos continuos de habilitación de la bomba o todavía no ha transcurrido un minuto de reposo después de ese límite.

El supervisor medirá ese intervalo con tiempo monotónico de pared mientras la
bomba esté habilitada, independientemente de la densidad PSM solicitada. Esta
interpretación conservadora de `ID2/1min` protege la primera implementación y
no depende de la actuación del protector térmico. Solo podrá cambiarse con una
fuente del fabricante o evidencia de ensayo aprobada.

El firmware iniciará con `pressureControlCommissioned = false`, `Kp = 0` y `Ki = 0`. Mientras no exista un conjunto de parámetros identificado, validado, versionado y protegido por integridad, una extracción ordinaria no podrá energizar la bomba. Solo un modo restringido de puesta en marcha podrá aplicar estímulos acotados, con supervisión y parada física disponibles; ese modo queda definido en la enmienda del 2026-09-21 de la § 5, que es lo que lo hace ejecutable. Así, aplazar la sintonía no introduce ganancias ficticias ni un arranque inseguro.

Si posteriormente se habilita la purga como extensión física, utilizará una
demanda limitada independiente y no reutilizará el PI de una extracción. La
primera implementación mantiene la bomba apagada durante espumado.

> **Revisión del 2026-10-06.** El espumado se retira del prototipo (ADR-006,
> revisión de la misma fecha), así que la frase anterior ya no tiene objeto.

### 3.6 Registro obligatorio

Cada prueba del lazo conservará, con tiempo monotónico:

- presión cruda convertida y presión filtrada;
- referencia autorizada y referencia interna limitada;
- error de control;
- término integral y salida PI antes y después de limitar;
- densidad solicitada y actuaciones realmente emitidas;
- estado operativo, calidad de la medición e inhibiciones;
- tiempo continuo de habilitación y reposo acumulado de la bomba;
- causa de terminación o protección.

Este registro permite reproducir el comportamiento sin introducir flujo estimado ni depender de la plataforma externa durante la extracción.

## 4. Parámetros reservados para la puesta en marcha

Los siguientes parámetros no pueden obtenerse responsablemente antes de instalar el sensor y la bomba en el circuito hidráulico real:

- `Kp` y `Ki` definitivos;
- constante final del filtro;
- velocidad del limitador de cambio de la referencia;
- posible zona de actuación sin respuesta y su tratamiento;
- límites ordinarios de operación, tolerancia de estabilidad y límite duro de presión;
- sobreimpulso y error de seguimiento aceptables;
- comportamiento de la bomba, el módulo y el cruce por cero bajo carga hidráulica.

Estos puntos no bloquean el esquema, el cableado de baja tensión ni la implementación estructural del firmware. Sí bloquean habilitar el lazo sobre una máquina presurizada y declarar que RF-010 está validado.

## 5. Procedimiento de puesta en marcha

1. Verificar mecánica, aislamiento, protecciones, fugas, cero del sensor, respuesta monótona, saturación del ADC y parada segura. Confirmar además continuidad a temperatura ambiente, sujeción y contacto térmico del protector original de la bomba, sin puentearlo ni provocar deliberadamente su disparo.
2. Probar `PUMP_ZC` y disparos con una carga y un procedimiento seguros antes de conectar la bomba al circuito presurizado.
3. Registrar respuestas a escalones conservadores de actuación con agua circulando por una carga hidráulica representativa. No se realizará identificación contra una salida bloqueada salvo que exista una protección mecánica de sobrepresión verificada.
4. Registrar la presión cruda en reposo y bajo actuaciones constantes. Comparar fuera de línea constantes de filtro candidatas —por ejemplo 20, 50 y 100 ms— y escoger la menor que reduzca el ruido sin ocultar el ascenso, el sobreimpulso ni una condición insegura. Confirmar después la elección con el lazo cerrado. El valor de 50 ms es solo el candidato inicial para disponer de firmware ejecutable.
5. Estimar ganancia, retardo y constante de tiempo de la planta. Si la respuesta se aproxima mediante `G(s) = K·e^(-θs)/(τs+1)`, utilizar como punto de partida conservador:

   ```text
   Kp = τ / [K·(λ + θ)]
   Ti = min(τ, 4·(λ + θ))
   Ki = Kp / Ti
   ```

   Si la respuesta se comporta como integradora y `dp/dt = k·Δu`, utilizar:

   ```text
   Kp = 1 / [k·(λ + θ)]
   Ti = 4·(λ + θ)
   Ki = Kp / Ti
   ```

   `λ` se elegirá deliberadamente conservador: un valor mayor produce una respuesta más lenta y robusta.
6. Empezar con `Ki = 0`, aumentar `Kp` de forma gradual y comprobar seguimiento, sobreimpulso y apagado. Solo después incorporar integral y anti-*windup*.
7. Para comprobar si existe una zona sin respuesta, aplicar solicitudes pequeñas y crecientes —1 %, 2 %, 3 %, 5 % y 10 %— bajo una condición hidráulica repetible, registrar actuaciones emitidas y comparar la pendiente de presión con el ruido de reposo. Si aparece un umbral reproducible, documentarlo; 0 % seguirá significando bomba apagada y cualquier compensación se aplicará únicamente a órdenes positivas.
8. Validar con diferentes resistencias reales de la cama de café y repetir ante cambios de filtro, bomba, tubería o sensor.
9. Congelar los valores, su versión, las condiciones de prueba y los criterios de aceptación antes de cambiar `pressureControlCommissioned` a verdadero y habilitar el control ordinario.

No se utilizará una sintonía por relé ni Ziegler–Nichols como método inicial porque obliga a provocar oscilaciones sostenidas en una planta hidráulica cuya protección todavía debe validarse.


#### Enmienda del 2026-09-21 — el modo restringido, definido

La § 3.5 dice desde la redacción original que «solo un modo restringido de puesta
en marcha podrá aplicar estímulos acotados, con supervisión y parada física
disponibles». Nunca se definió qué es ese modo y el firmware no lo implementó.

La consecuencia es que **este procedimiento no es ejecutable**. Sus pasos 3 a 7
exigen registrar la respuesta a escalones sobre el sistema real; el guardián corta
toda actuación mientras `pressureControlCommissioned` sea falso; y esa marca solo
puede cambiar cuando existan los registros que no se pueden tomar. La puesta en
marcha se pedía a sí misma como requisito.

Con el dimmer el círculo se rompió de otra manera: ADR-008 § 5.9 ordena ensayarlo
en banco, fuera de la cafetera y con un microcontrolador de pruebas. Esa vía
sirve para saber si el TRIAC corta y **no sirve para identificar**, porque un
programa de banco no atraviesa la cadena de medida que se está caracterizando
—divisor, filtro antialias, ADS1115, conversión— ni produce el registro de la
§ 3.6, que es la evidencia de la que salen `K`, `θ` y `τ`.

**Qué es.** Una autorización de actuación en **lazo abierto**, acotada, temporal
y declarada, que sustituye al controlador en lugar de habilitarlo.

**Qué relaja.** Exactamente una causa de inhibición: la de control sin poner en
servicio. Ninguna otra. Siguen cortando igual la señal no apta, el límite duro de
presión, el plazo de adquisición incumplido, la pérdida de sincronismo de red, el
estado operativo que no autoriza, el arranque o la actualización en curso, y el
servicio continuo de la bomba con su reposo obligatorio. **Un ensayo de
identificación se detiene por las mismas razones por las que se detendría una
extracción**, y esa es la propiedad que lo hace admisible.

**Qué no habilita.** El PI no se ejecuta. `Kp` y `Ki` siguen valiendo cero y la
referencia no gobierna nada: la salida la fija la secuencia de estímulo y no el
controlador. Identificar y regular son operaciones distintas, y este modo autoriza
solo la primera. `pressureControlCommissioned` permanece en falso durante toda la
campaña y cambia únicamente al final, por el paso 9.

**Cómo se autoriza.** Tres condiciones simultáneas, las tres necesarias:

1. **Un binario distinto del de operación**, compilado con la opción de
   identificación activa. No es una marca en tiempo de ejecución ni un mensaje
   recibido: un equipo compilado para operar no puede entrar en este modo por
   ninguna vía, y el diagnóstico de arranque declara cuál de los dos binarios
   corre.
2. **Una confirmación física deliberada en cada arranque**, sobre el mando, que
   fija el procedimiento. Sin ella el binario de identificación se comporta como
   el de operación. Responde a la supervisión permanente que exige ADR-008.
3. **Una secuencia de estímulo declarada y versionada**, elegida en compilación.
   El operador no mueve la salida: la ejecuta una secuencia que termina sola.

**Por qué una secuencia y no un mando manual.** Un humano moviendo un botón
produce escalones que no se pueden repetir ni fechar con precisión, y la
identificación vive justamente de la forma del flanco. Una secuencia versionada
hace el ensayo reproducible, deja su identificador en el registro y permite
repetir la campaña después de cambiar filtro, bomba, tubería o sensor, que es lo
que pide el paso 8.

**Los topes no se fijan aquí.** La demanda máxima de cada secuencia exige medición
y queda reservada, como el resto de la § 4. El criterio sí se declara: se empieza
por la escalera del paso 7 —1, 2, 3, 5 y 10 %— y no se sube de escalón mientras el
anterior no haya producido una respuesta legible sobre el ruido de reposo. El tope
vigente lo impone **el guardián y no la secuencia**, por la misma razón por la que
decide antes que cualquier controlador: quien genera el estímulo no puede ser
quien juzgue si es admisible.

**El registro no puede confundirse con uno de operación.** El veredicto que
autoriza en este modo es distinto del que autoriza una extracción, de modo que la
columna de veredicto que la § 3.6 ya conserva dice por sí sola en qué condiciones
se tomó cada línea. Una serie de identificación archivada como si fuera de
operación contaminaría la evidencia de RF-010, y después no hay forma de
distinguirlas.

**Lo que se decidió no hacer.**

- **Poner la marca en verdadero para ensayar y devolverla al terminar.** Es lo más
  corto y anula la única barrera que impide energizar con ganancias sin
  identificar; además habilitaría el lazo cerrado, que es precisamente lo que no
  se quiere durante la identificación.
- **Autorizar desde la plataforma externa.** Pondría al alcance de la red una
  orden capaz de energizar la máquina, contra la frontera de autoridad que
  sostiene ADR-012.
- **Identificar con el microcontrolador de banco**, por lo dicho arriba: no
  atraviesa la cadena de medida ni produce el registro.
- **Relajar cualquier otra causa de inhibición**, aunque estorbe al ensayo. Si una
  protección impide completar una secuencia, lo que se revisa es la secuencia.

**Consecuencia para el firmware.** Hoy no existe nada de esto: el guardián no
distingue más autorización que la ordinaria y no hay ninguna secuencia de estímulo
escrita. Queda como trabajo previo a la campaña 4 del plan experimental. La
implementación debe consultar la autorización por `actuation_verdict_allows` y no
comparando contra el valor ordinario, porque a partir de esta enmienda **hay dos
veredictos que permiten actuar** y uno de ellos es nuevo.

**Implementado el 2026-10-01** (`identification_mode`, `stimulus_sequence` y el
veredicto `allowed-identification` del guardián). Cómo se usa, y las dos
consecuencias que tiene para el orden de la puesta en servicio, en
puesta en servicio § 3.

#### Enmienda del 2026-10-10 — la escalera de la bomba en un solo binario

La enmienda anterior hizo de cada escalón una secuencia propia, y subir exigía
compilar otra. En la máquina real eso costó unos 8 minutos por escalón —
desenchufar para grabar, reiniciar, confirmar de nuevo y recalentar la caldera
con la integral del lazo térmico a cero— para 12 s de bomba, y en la etapa 6 la
espera llevó a confirmaciones fallidas y a pulsaciones de más.

**Se decide un binario de escalera** (`pump-ladder-v1`). Arranca en el 1 % y,
con la secuencia terminada, `B,step,<n>` por el puerto de servicio arma el
mismo escalón o el siguiente. Lo que la regla protegía se mantiene en
`pump_ladder`, que es puro y se prueba en el anfitrión:

- **no se elige sin la secuencia anterior terminada entera**: la respuesta que
  se mira tiene que existir, y un aborto sigue cerrando la sesión;
- **no se salta un escalón ni se baja**: solo el mismo o el siguiente. Repetir
  sí, porque la partición de calibración y validación (puesta en servicio 6.9)
  lo necesita;
- **la confirmación física sigue siendo una por arranque**, y el guardián juzga
  cada escalón contra el tope compilado, que tiene que cubrir el 75 %;
- **la red queda fuera**: solo el puerto USB, con el operador delante.

Lo que cambia es dónde se mira la respuesta antes de subir: en el registro, no
en una recompilación. Los binarios de un escalón siguen existiendo y siguen
valiendo.

**Corrección del 2026-10-11 a esta enmienda.** Un escalón pedido con la
secuencia anterior terminada pero con su extracción todavía abierta se armaba
en el acto, y la extracción dura más que los 12 s de bomba: el 3 % arrancó sin
que nadie pulsara café. Ahora el escalón elegido espera, sin perderse, a que la
máquina esté apagada, calentando o lista, de modo que **cada escalón exige una
pulsación de café nueva** (`pump_ladder_may_arm`).

#### Enmienda del 2026-10-11 — la actuación útil es un ciclo completo

§ 3.4 dejó pendiente verificar en banco «la polaridad y la secuencia útil que
admita el módulo y la bomba reales». No se verificó, y el planificador decidía
por semiciclo: una oportunidad en cada cruce por cero, a 120 Hz.

La bomba lleva un diodo en serie y solo trabaja con los semiciclos de una
polaridad. Con decisión por semiciclo, toda densidad de periodo par —1, 2, 5,
10, 50 %— pone todas sus actuaciones en la misma polaridad, y cuál depende del
instante de arranque. La mitad de las veces la bomba no recibe nada durante el
escalón entero, aunque el registro cuente cada disparo. Así se vio en las
escaleras de la etapa 6 del 2026-10-10 y 11: el 10 % y el 50 % respondieron en
una sesión (hasta 8,2 bar) y callaron en otra con la presión quieta tras
cientos de disparos, mientras el 3, 20, 30 y 75 %, cuyos huecos alternan la
polaridad, respondieron siempre. Explica también las fallas del diagnóstico al
50 % de esa tarde, que se habían buscado en el cableado.

**Se decide que la actuación útil es un ciclo completo.** El planificador
decide en el primer semiciclo de cada pareja y repite la decisión en el
segundo (`psm_scheduler_half_cycle`), de modo que cada actuación lleva las dos
polaridades y la que conduce siempre está. El acumulador cuenta ciclos, así que
la exactitud sin deriva y el reparto uniforme de § 3.4 valen por ciclo. Una
densidad nula corta también a mitad de ciclo.

Lo que se descartó: **detectar la polaridad** y disparar solo en la que
conduce. Conservaría la resolución por semiciclo, pero el cruce por cero del
módulo no la distingue, y adivinarla en el arranque reintroduce la misma
dependencia del azar que se quiere eliminar.

Consecuencia: a igual densidad, la bomba recibe ahora la mitad de semiciclos
conductores que en las corridas de periodo par que sí respondieron. Los
escalones registrados antes de esta enmienda no se mezclan con los de después
para identificar la planta.

#### Enmienda del 2026-10-12 — la sintonía en caliente de la presión

Los pasos 5 y 6 piden estimar la planta, empezar con `Ki = 0` y subir `Kp` poco
a poco. Con la planta compilada eso es una grabación por valor probado, y aquí
cada prueba es además una pastilla de café. ADR-020 § 4.2 ya resolvió lo mismo
para el lazo térmico; **se extiende a la presión, con las mismas tres
condiciones**: solo el binario de identificación, solo tras la confirmación
física en el mando, y solo `λ` y la integral por el puerto USB. La planta no se
toca por el puerto y `pressureControlCommissioned` sigue en falso.

**La planta de partida sale de la 6.5 y la 6.6 con café** (registro del
2026-10-11). Tras el arranque de la bomba la presión salta en unos 0,1 s
(retardo de 60 a 100 ms y constante rápida de 40 a 100 ms entre el 50 y el
75 %) a una meseta que depende poco de la densidad, y después sube despacio
mientras la canastilla abre: al 75 % llega a 9,9 bar y se estabiliza; al 50 %
va por 8,0–8,4 bar sin estabilizar a los 6 s. Se toma un primer orden con
retardo con `K = 0,06 bar/%` (entre las dos mesetas altas; es cota superior,
porque el 50 % no había terminado de subir), `τ = 0,1 s` y `θ = 0,1 s`. La
subida lenta de la pastilla no se modela: es una perturbación que corrige la
integral. `λ` empieza en 1 s, unas diez veces el retardo, como en la térmica.

**Por qué primer orden y no integradora.** La § 5 ofrece las dos fórmulas. La
pendiente de la subida lenta al 75 % (unos 0,016 bar/s por punto) daría
`Kp ≈ 57 %/bar`, casi cuarenta veces más que el primer orden; pero del 10 al
30 % la presión llega a su meseta en 0,3 s y se queda quieta, que es una planta
que se regula sola, no una que integra. Con la planta mal elegida, SIMC
integradora pondría al lazo a oscilar contra la válvula de la canastilla.

**El techo es el 75 %**, el escalón más alto medido con café (10,3 bar): el PI
nunca lleva la bomba a una densidad que no se haya visto. La secuencia
`pump-tuning-75pct-v1` dura 25 s (1 s en cero, 23 s con techo, 1 s en cero) y
cabe en los 27 s de extracción del perfil. El límite duro de 12 bar y el corte
de 2 min continuos rigen igual.

**Una extracción por prueba.** La secuencia se re-arma con `B,step,75`, con las
reglas de la escalera: solo con la anterior terminada entera y con una pulsación
de café nueva. Las órdenes `A,` se aceptan con la sesión confirmada —armada,
corriendo o terminada—, no solo mientras corre: el ajuste siguiente se decide
mirando el registro de la extracción anterior, con la bomba quieta. Un aborto
cierra la sesión y las rechaza.

**El PI no calcula mientras el techo es cero.** En el segundo que abre la
secuencia el error es de 9 bar y la bomba no puede responder; si el PI
calculara, la integral llegaría cargada al techo. Fuera del techo el lazo queda
abierto y la compuerta lo reinicia desde la medida.

**La referencia es la del perfil**, que el estado de extracción ya trae, con
el limitador de 3 bar/s del lazo de banco. **El filtro es el de 20 ms elegido
en la 6.4**, no el candidato de 50 ms de la § 3.3; la 7.8 lo confirma con el
lazo cerrado. La planta se ajustó sobre la columna filtrada de la escalera,
que llevaba 50 ms: el retardo medido incluye unos 35 ms que el filtro de 20 ms
ya no pone, de modo que la planta de partida es algo más lenta que la real, que
es el lado conservador para SIMC.

**Lo que se decidió no hacer.**

- **Compartir los símbolos de configuración de la térmica.** La presión se
  sintoniza en décimas de segundo y en bar; con los mismos nombres cada número
  se leería según qué secuencia esté elegida.
- **Subir el techo al 100 %** para tener margen. No hay registro con café por
  encima del 75 %; si el 75 % no basta para sostener 9 bar con otra molienda
  (7.3), se publica otro techo con ese registro delante.
- **Ajustar también `K` o `τ` por el puerto.** Es cambiar la planta, que
  ADR-020 ata a `commissioned`.

#### Enmienda del 2026-10-12, tarde — preinfusión a presión de extracción

La sintonía con café de esa mañana reguló bien con 32 y 38 clics: el pico fue
de 9,5–9,8 bar y la presión quedó en 8,5 bar desde el segundo 14, con la bomba
en 50–56 %. Con 44 clics, en cambio, **la presión llegó a 11,6 bar con λ = 1 s
y a 12,3 bar con λ = 2 s, y en esta el límite duro cortó y la máquina quedó en
alarma**. El guardián actuó como debía.

**Lo que se midió.** Con la canastilla presurizada, mientras la pastilla se
moja la presión se queda en la meseta de la válvula (6,6–7 bar) haga lo que
haga la bomba. El lazo no tiene ganancia ahí, y la integral sube hasta el
techo del 75 %. Cuando la pastilla cierra, la presión sube a 2,5–3 bar/s. Con
la cama gruesa el 75 % estaba cerca de lo necesario (54 %); con la fina, que
pide un 10 %, sobra tanto que la presión pasa de 11 bar antes de que el PI baje
la bomba. Subir λ empeoró el pico, porque la integral también descarga más
despacio.

**Se decide una preinfusión a presión de extracción**, como la de la máquina
del barista. Mientras la pastilla se moja la bomba va al techo que el guardián
admitió, sin PI. Cuando la presión filtrada llega a la referencia menos
0,5 bar, la pastilla ya cerró, y el PI arranca con su integral en el 15 %, entre
lo que pidieron la cama fina y la gruesa (`pressure_preinfusion`). El paso
depende de la presión, no del tiempo.

**Cómo se eligió.** Con una simulación de la extracción ajustada a las cinco
pruebas de ese día (bomba de vibración, válvula de la canastilla, pastilla que
cierra al absorber su volumen; reproduce los picos medidos con unos 0,3 bar),
en 60 situaciones: cinco moliendas (del 5 al 70 % en régimen), mojado de la
mitad al doble del medido, presión de partida de 0 y 4,5 bar, λ de 0,5 a 2 s, y
el rizado de la bomba (±0,4 bar a 3,6 Hz). Con la preinfusión, el pico máximo
queda en 9,2–9,8 bar sin ningún disparo, y con las condiciones medidas la
presión se asienta a los 10–12 s. La simulación es una herramienta para
comparar estrategias, no el modelo del gemelo: sus unidades son relativas y se
ajustó y comparó con las mismas extracciones. La prueba con café decide.

**Lo que se decidió no hacer.**

- **Preinfusión de tiempo fijo.** Con 40 % durante 4 s, una pastilla que tarda
  más en mojarse cierra con el techo ya en el 75 % y el pico es el de antes.
- **Preinfusión a baja potencia hasta el cierre.** No dispara, pero el mojado
  se alarga tanto que la presión llega a la referencia hacia los 18–20 s, y con
  mojados largos no llega dentro de la extracción.
- **Ajustar λ, bajar el techo o añadir derivativo.** λ no lo resuelve, como se
  midió; un techo del 60 % deja la cama fina cerca de 11 bar; el derivativo
  amplificaría el rizado de los golpes de la bomba.

**Corrección del mismo día.** En la primera prueba con preinfusión (44 clics)
el pico bajó de 11,6 a 10,8 bar, no a menos de 10: el rizado de esa pastilla
fue de ±0,6–0,9 bar, un golpe llevó la lectura de 20 ms a 8,2 bar con la meseta
en 7,3, y el PI entró antes del cierre. La decisión del paso se toma desde
entonces sobre un filtro propio de 300 ms, que deja ese rizado en ±0,1 bar; el
PI conserva el de 20 ms.

**Corrección del 2026-10-12, noche.** En la 7.6 con el programa de operación,
una extracción con la referencia en 3 bar empezó con 3,3 bar atrapados de la
anterior: el paso al PI fue inmediato y la integral de arranque del 15 % dio un
golpe de 0,35 s que llevó la presión a 5,9 bar sin que hiciera falta bombear.
La integral solo se precarga si la presión, al pasar, está por debajo de la
referencia; si ya llegó, el PI arranca en cero. Se descartó medir el golpe y
compensarlo restando el término proporcional: depende de la planta, y la regla
de no bombear cuando sobra presión no depende de nada.

**Riesgos que quedan.** El paso a 8,0 bar deja unos 0,7 bar sobre la meseta
media (7,0–7,3) una vez filtrado el rizado. Un paso prematuro devolvería el comportamiento
anterior, con el límite duro como protección. Una cama tan gruesa que nunca
cierre deja la bomba al techo en la meseta, sin llegar a la referencia, sin
riesgo. El margen y la integral de arranque son provisionales y se fijan en el
acta de presión.

#### Enmienda del 2026-10-16 — el ensayo hidráulico E2 con las actas aprobadas

E2 (puesta en servicio 9.3, Anexo D) mide la curva presión-caudal de la bomba:
cada restricción fija da un punto, y cada orden de bomba una curva. La bomba
tiene que trabajar a una orden fija, sin el PI, porque lo que se mide es la
presión que resulta, no la que el PI busca. El modo restringido de § 5 no lo
permite: con el acta de presión compilada responde `already-commissioned` y no
actúa, porque existe para llegar al acta, no para ensayar después de ella. Con
el firmware de operación tampoco: la referencia del perfil no baja de la
presión atrapada en reposo (4–6 bar; puesta en servicio 7.6 y E1), y por
debajo de ella la bomba no arranca.

**Se decide un binario de ensayo hidráulico** (`CONFIG_CAFE_HYDRAULIC_TEST`),
por decisión del operador del 2026-10-16:

- **exige las dos actas compiladas**, o no compila. El guardián autoriza la
  bomba como en operación, con la máquina de estados y los límites del acta
  (corte a 12 bar, servicio continuo), y el calentador lo regula su acta;
- **sin confirmación en el mando.** La confirmación de § 5 existía para actuar
  sobre una máquina sin poner en servicio, y esta ya lo está. La presencia del
  operador la da la pulsación de café: cada corrida exige una nueva;
- **la orden se elige por el puerto** con `H,order,<n>` entre las tres que el
  binario declara, **en cualquier orden y repetida**. La regla de la escalera
  (repetir o subir) protegía la primera exposición de la bomba; las órdenes de
  E2 ya se vieron en la máquina, y E2 recorre cada restricción con las tres;
- **ninguna orden por encima del techo del acta** (75 %): el ensayo no lleva la
  bomba a una zona que el acta no vio. Si una no cabe, el ensayo queda sin
  configurar y la bomba en cero;
- **la orden se fija al abrir la extracción** y no cambia hasta cerrarla; sin
  orden elegida, la bomba queda en cero. El purgado sigue siendo el de
  operación;
- cada línea `P` declara `hydraulic-test-v1` en la columna de sintonía, y la
  referencia en cero: ninguna referencia gobierna la bomba.

Lo que se descartó: **quitar el acta de presión del binario** para usar el
modo restringido tal como está. Dejaba el límite duro en el provisional de
banco y obligaba a la confirmación, y el ensayo se haría con una máquina
distinta de la que se puso en servicio. **Relajar `already-commissioned` en el
modo restringido**: habría abierto el lazo abierto a cualquier secuencia sobre
un lazo con acta, no solo a la de E2.

Lógica pura en `hydraulic_test`, probada en el anfitrión
(`test_hydraulic_test.c`). Un equipo desplegado lleva siempre el binario de
operación.

#### Enmienda del 2026-10-20 — por qué la referencia de operación es 8,5 bar

**Qué se registra.** Todos los perfiles de la placa y todos los ensayos con café
desde la sintonía del 2026-10-12 (E3 incluido) usan 8,5 bar, no los 9 bar de la
referencia general del espresso. La decisión se tomó en la sintonía y no se
escribió entonces; el operador la recuerda así (2026-10-20): **con 9 bar la
presión se pasaba por los picos.** Se escribe ahora, con lo que la sostiene en
los registros.

**Lo medido** (sintonía de presión,
pruebas 9–12, con preinfusión y filtro del paso de 300 ms):

| Referencia | Molienda | Pico | Bomba en régimen |
|---|---|---|---|
| 8,5 bar | 44 / 38 / 32 clics | 8,9 / 8,8 / 9,0 bar | 40–59 % |
| 9,0 bar | 32 clics | 9,4 bar | 64–66 % |

Los golpes de la bomba suman ±0,4–0,9 bar a 3,6 Hz sobre la media. Con 9 bar el
pico llega a 9,4 bar y la bomba trabaja a 64–66 % frente al techo de 75 %; con
8,5 bar queda a 56–59 % en la misma molienda. La lectura de que 8,5 bar deja más
margen al pico y al techo para moliendas más finas es inferida de estas cinco
pruebas; no hay un ensayo que compare las dos referencias en varias moliendas.

**Alcance.** 8,5 bar es el valor de operación de esta máquina con canastilla
presurizada de dos tazas, no un límite: el perfil admite de 1 a 11 bar y el
lazo se probó con café hasta 9 bar.

## 6. Consecuencias

- Quedan definidos el algoritmo, la frecuencia inicial, la interfaz con potencia, la modulación y los estados seguros necesarios para construir el prototipo.
- La implementación no depende de flujo, servidor, FMU, ontología ni reglas de conocimiento.
- La presión por encima de la referencia se trata como saturación inferior de una planta unilateral, no como una orden negativa inexistente.
- Las ganancias no se copian de Gaggiuino ni se seleccionan por conveniencia; se obtienen de la respuesta real y quedan versionadas.
- Cambiar `Kp`, `Ki`, filtro o limitador después de la puesta en marcha no exige modificar el hardware ni esta arquitectura, siempre que se mantengan las fronteras y protecciones decididas.
- El lazo regula presión **a la salida de la bomba** y no sobre el café, porque el transductor está entre la bomba y la caldera (§ 3.1). La diferencia entre ambas magnitudes debe medirse y declararse antes de comparar la serie medida con la simulada.

## 7. Verificación de la decisión

Antes de considerar validado el control deberán existir pruebas reproducibles de:

1. inhibición completa fuera de extracción y ante cada fallo crítico;
2. correspondencia entre densidad solicitada y actuaciones emitidas;
3. ausencia de disparos durante arranque, reinicio o pérdida de cruce por cero;
4. seguimiento de referencia, sobreimpulso, estabilidad y recuperación dentro de límites aprobados;
5. actuación segura cuando la presión supera la referencia;
6. trazabilidad de muestras, salida, saturaciones y causa de terminación;
7. robustez frente a varias resistencias hidráulicas representativas.
8. corte de la bomba al alcanzar dos minutos continuos, inhibición durante el minuto de reposo y registro de ambas causas.

## 8. Trazabilidad

- Requisitos: RF-007–RF-012, RF-014 y RF-019; RNF-001–RNF-005 y RNF-007–RNF-008.
- Decisiones relacionadas: ADR-001, ADR-005 y ADR-006.
- Arquitecturas afectadas: lógica, física y esquema funcional de tres placas.

## 9. Referencias técnicas

- [Gaggiuino `pump.cpp`, revisión `bddc57a` — control de bomba dependiente de estimación de flujo](https://github.com/Zer0-bit/gaggiuino/blob/bddc57a63552b921a9f54024e41bc04965fa8dd8/src/peripherals/pump.cpp).
- [PSM Library, rama `feature/timer` — planificador por densidad de pulsos utilizado como referencia por Gaggiuino](https://github.com/banoz/PSM.Library/tree/feature/timer).
- [RobotDyn AC Light Dimmer — entrada lógica, detección de cruce por cero y advertencias para cargas inductivas](https://robotdyn.com/ac-light-dimmer-module-1-channel-3-3v-5v-logic-ac-50-60hz-220v-110v).
- [Skogestad y Grimholt — *The SIMC Method for Smooth PID Controller Tuning*](https://www.mic-journal.no/PDF/2010/MIC-2010-4-3.pdf).
- Xinbao — informe constructivo local de la familia CM8502, págs. 12, 60 y 61.
- Fotografía local de la bomba JYPC-503A inspeccionada.
