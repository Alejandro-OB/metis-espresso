# ADR-020 — Cómo entran al firmware la calibración y la planta identificada

**Estado:** Aceptada
**Fecha de redacción:** 2026-10-01
**Fecha de aprobación:** 2026-10-01
**Ámbito:** vía por la que llegan al dispositivo los coeficientes de calibración de las cadenas de medida y los parámetros de la planta identificada en la puesta en servicio

## 1. Contexto

La puesta en servicio § 3 enumera tres
piezas de firmware sin las cuales su procedimiento no es ejecutable. La primera,
el modo restringido, está decidida en ADR-007 § 5, enmienda del 2026-09-21. Las
otras dos no lo estaban:

- **F2**, una vía para introducir la calibración de las cadenas de medida;
- **F3**, una vía para introducir la planta identificada de cada lazo.

### 1.1 Situación comprobada

| Elemento | Situación |
|---|---|
| Persistencia de la calibración | Existe. `sensor_calibration_store` valida y guarda en NVS; `acquisition_runtime` la carga al arrancar y cada medida declara el identificador de la calibración que usó |
| Quién llama a `sensor_calibration_store` | **Nadie.** No hay consola de comandos ni llega por el canal de configuración |
| Precedente de orden local | `config_command`: módulo puro que interpreta órdenes `C,…` recibidas por el puerto de servicio y las guarda en NVS, sin pasar por la red |
| Ganancias de los lazos | `control_tuning` las calcula por SIMC sobre el modelo de banco de `control_plant`, con `commissioned` escrito como literal falso |
| Actualización remota del firmware | No existe; ADR-002 dejó abierta la decisión sobre particiones OTA |

## 2. Problema de decisión

Se debe definir por qué vía llegan al firmware los números que produce la puesta
en servicio, distinguiendo los que solo cambian la interpretación de una medida
de los que habilitan la actuación.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Por el canal de configuración de la plataforma (ADR-017) | Esa vía es para perfiles que el barista selecciona, edita o acepta. La calibración y la sintonía no se seleccionan, y RD-002 mantiene fuera de la red toda autoridad sobre el dispositivo. Descartada para ambas. |
| Actualización remota del firmware (OTA) | Equivale a fijar los valores en compilación y entregar el binario por la red: cada calibración cambiaría el firmware que gobierna la máquina, exige construir una actualización remota que no existe y choca con RD-002. Descartada. |
| Por el puerto de servicio (USB) | Reutiliza el patrón de `config_command` y exige estar frente a la máquina. **Seleccionada para la calibración.** Descartada para la planta: pondría la llave de la actuación a una orden de distancia. |
| Fijados en compilación, desde un archivo versionado en el repositorio | Obliga a que cada cambio pase por un commit y un acta. Para la calibración es un costo sin beneficio. **Seleccionada para la planta.** |

## 4. Decisión

### 4.1 F2 — La calibración entra por el puerto de servicio

Los coeficientes de calibración del NTC (`R25`, `β`), de la cadena de presión
(ganancia y cero, con las resistencias y el raíl medidos) y de la báscula llegan
como una orden por el puerto USB de servicio, interpretada por un módulo puro con
el mismo patrón que `config_command`, y se guardan con `sensor_calibration_store`.

- **Identificador y versión obligatorios.** Una orden sin ellos se rechaza: cada
  medida debe poder decir qué calibración usó (RNF-004).
- **Rige desde el siguiente arranque**, no en caliente, para que una serie de
  medidas nunca mezcle dos calibraciones.
- **Se rechaza sin escribir** si los coeficientes no son físicamente posibles,
  como ya hace `sensor_calibration_is_valid`.
- **El cálculo no lo hace el operador a mano.** Un programa en el computador toma
  los registros de la calibración, calcula los coeficientes, los muestra y, si
  quien calibra los aprueba, los envía por el mismo USB por el que se registró la
  medida.

### 4.2 F3 — La planta identificada se fija en compilación

Los parámetros identificados de cada lazo (`K`, `θ`, `τ`, `λ`, la constante del
filtro y los límites de § 4 de ADR-007 y ADR-009) se escriben en un archivo
versionado del repositorio, generado a partir del acta de puesta en servicio, y
se compilan en el firmware. `control_tuning` calcula las ganancias sobre esa
planta en lugar de sobre el modelo de banco.

- **`commissioned` solo es verdadero en un binario compilado con ese archivo**, y
  lo es por lazo: el de presión y el de temperatura se ponen en servicio por
  separado.
- **Ninguna orden externa cambia la planta ni la marca.** Ni por USB ni por la
  red.
- **El identificador de la sintonía** sustituye a `bench-v1` y viaja en el
  registro, de modo que cada serie dice con qué planta se reguló.

> **Revisión del 2026-10-06 — la sintonía se ajusta por USB en el binario de
> identificación.** ADR-009 § 5 pide empezar con `Ki = 0` y subir `Kp` poco a
> poco. Con la planta fijada en compilación eso exigía un acta, una compilación y
> una grabación por cada valor probado, y los autores lo descartaron por lento.
>
> Se decide una excepción acotada. **Solo en el binario de identificación**
> (`CAFE_IDENTIFICATION`), y solo después de la confirmación física en el mando,
> una orden por el puerto de servicio cambia en caliente `λ` y si la integral
> está activa, y el PI regula con esos valores. El guardián conserva su tope y
> todas sus causas de corte. Cada cambio se registra con hora, valor y el
> veredicto vigente, y la columna `tuning` del registro lo declara. Cuando se
> encuentra el valor, **un solo acta** lo congela, y de ella sale el binario de
> operación como hasta ahora.
>
> **El binario de operación no cambia:** ninguna orden externa modifica su planta
> ni su marca. **La red queda fuera**, también en el binario de identificación:
> se consideró permitirlo desde la interfaz web y se descartó, porque exigía
> abrir una excepción a RD-002, un mensaje nuevo de la plataforma al dispositivo
> y su pantalla, y hacía depender la sintonía del enlace inalámbrico, cuando el
> operador de todos modos está al lado de la máquina.
>
> ~~**Pendiente de implementar:** hoy, en identificación, el PI se mantiene
> reiniciado y solo se aplican las secuencias de estímulo.~~ **Implementado el
> 2026-10-10** (`identification_tuning`, secuencia `heater-tuning-10pct-v1`):
> órdenes `A,show`, `A,lambda,<s>` y `A,integral,on|off`, aceptadas solo con el
> modo restringido corriendo; la integral empieza apagada; la salida del PI no
> pasa del techo de la secuencia, que el guardián juzga contra su tope; la
> planta provisional (`K`, `θ`, `τ`) llega por Kconfig y, sin ella, el
> calentador queda en cero en vez de aplicar el techo en lazo abierto. Cada
> cambio responde con instante, sintonía, ganancias y veredicto, y la columna
> `tuning` lo repite en cada línea.

## 5. Justificación

La línea que separa ambas decisiones es qué habilita cada número. La calibración
cambia cómo se interpreta una lectura y la protegen sus propias comprobaciones de
validez: un valor equivocado da una medida equivocada, que el acta y la
comparación delatan. La planta identificada va unida a `commissioned`, que es la
llave con la que el guardián deja energizar la bomba y el calentador. Esa llave
es el límite de seguridad del sistema, y no debe abrirse con una orden de una
línea.

La calibración por USB coincide además con la forma en que se calibra: durante
la medida el computador ya está conectado por ese puerto registrando la serie.
La planta en compilación coincide con lo que ADR-007 § 5.9 y ADR-009 § 5.9 piden
—congelar los valores con su versión, sus condiciones de prueba y sus criterios
de aceptación— y con la decisión del modo restringido, también fijado en
compilación para que un equipo compilado para operar no cambie de régimen por
ninguna vía externa. La puesta en servicio ocurre una vez y solo se repite si
cambian la bomba, la caldera, el filtro o un sensor; que entonces haya que
recompilar es el control buscado, no un costo.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- La etapa 4 de la puesta en servicio deja de exigir recompilar.
- El equipo desplegado puede recalibrarse sin herramientas de desarrollo.
- Cada sintonía puesta en servicio queda en el historial del repositorio con su
  acta.

### 6.2 Consecuencias desfavorables

- Repetir la puesta en servicio exige recompilar y regrabar el firmware.
- Una calibración cargada por USB no deja rastro en el repositorio; su único
  registro es el identificador que viaja con las medidas y el acta de la
  calibración, que hay que conservar.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Cargar una calibración equivocada | Validación de coeficientes en el dispositivo, identificador obligatorio y contraste posterior con el termómetro de referencia (etapa 4.1) |
| Compilar un binario de operación con una planta no aprobada | El archivo de planta solo se genera desde un acta; el diagnóstico de arranque declara el identificador de la sintonía |

## 8. Aspectos pendientes

- ~~Formato exacto de la orden de calibración y del archivo de planta.~~
  Resuelto el 2026-10-01: `calibration_command.h` y `commissioned_plant.h`.
- ~~El programa del computador que calcula y envía la calibración.~~ Resuelto el
  2026-10-01: `software/commissioning/calibration.py`. El emparejamiento con el
  registro del DS18B20 quedó resuelto el 2026-10-07 en
  `software/commissioning/reference_log.py`.
- Conservación del acta de cada calibración junto a sus registros.

**Implementado el 2026-10-01.** Cómo se usa, en
puesta en servicio § 3. Dos
concreciones que este ADR no fijaba: las tres cadenas comparten un registro con
un solo identificador, y la planta compilada trae también el **límite duro** de
su lazo, que se congela con ella. Un firmware con planta no compila con la
simulación ni con el lazo de banco.

## 9. Verificación de la decisión

La decisión quedará implementada cuando una calibración cargada por USB rija tras
el reinicio y aparezca en las medidas con su identificador, una orden sin
identificador o con coeficientes imposibles se rechace sin escribir, y ningún
binario sin archivo de planta pueda reportar `commissioned` verdadero.

## 10. Trazabilidad

- Requisitos: RNF-004 y RD-002.
- Decisiones: ADR-002, ADR-007 § 5, ADR-009 § 5, ADR-012 § 4.4 y ADR-017.
- Procedimiento: puesta en servicio § 3.
