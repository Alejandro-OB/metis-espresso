# Actas de puesta en servicio

Un acta registra cómo se puso en servicio un lazo de control de Metis. Dice qué
planta se identificó, con qué sintonía queda el controlador, en qué condiciones
se midió y qué criterios de aceptación cumplió. También declara lo que no se
probó.

Los valores de cada acta llegan al firmware sin copiarse a mano. Una herramienta
genera desde el acta los encabezados de `firmware/main/commissioned/`, y cada
encabezado guarda la huella SHA-256 del acta de la que salió. Sin esos
encabezados, el guardián de actuación mantiene apagados la bomba y el calentador
(ADR-020 § 4.2). La herramienta que los genera no forma parte de este
repositorio.

Las actas están en JSON porque las leen el generador, las pruebas y la réplica
del controlador que usa la plataforma (`model/controllers/`). **No se editan**:
cambiar un carácter cambia su huella, y las pruebas lo detectan. Este documento
las resume para leerlas sin abrir el JSON.

| Acta | Lazo | Versión | Fecha | Uso |
|---|---|---|---|---|
| `acta-presion-20261012.json` | Presión | 1 | 2026-10-12 | Firmware vigente y réplicas 1.0.0 y 2.0.0 |
| `acta-temperatura-20261028b.json` | Temperatura | 12 | 2026-10-28 | Firmware vigente y réplica 2.0.0 |
| `acta-temperatura-20261019.json` | Temperatura | 7 | 2026-10-19 | Réplica 1.0.0 |

Las versiones de temperatura que faltan (2 a 6 y 8 a 11) cambiaron parámetros
que la versión 12 recoge, y no se publican.

## Presión (versión 1)

La bomba vibratoria se modeló como una planta de primer orden con retardo,
ajustada a una escalera de potencias de la bomba con café en la canastilla.

| Parámetro | Valor | Significado |
|---|---|---|
| Ganancia de la planta | 0,06 bar por punto porcentual | Presión que añade cada punto de potencia de la bomba |
| Constante de tiempo | 0,1 s | Rapidez con que la presión sigue a la bomba |
| Retardo | 0,1 s | Tiempo hasta que la presión empieza a responder |
| λ | 1,0 s | Constante de tiempo pedida al lazo cerrado |
| Filtro de la medida | 0,02 s | Suaviza los golpes de la bomba |
| Rampa de la referencia | 3,0 bar/s | Velocidad máxima de cambio de la referencia |
| Techo de la bomba | 75 % | Potencia máxima que el controlador puede pedir |
| Preinfusión | margen 0,5 bar; integral desde el 15 % | La integral arranca cuando el lecho ya está mojado |
| Límite duro | 12,0 bar | Por encima, la máquina corta y pasa a alarma |

Condiciones: canastilla presurizada de dos tazas con 14 g de café, caldera a
92 °C, cuarto a 22,3 °C y red a 120,5 V.

| Criterio | Resultado |
|---|---|
| Solo con acción proporcional, el lazo no oscila | Con λ = 1 s y molienda de 38 clics, 4,5 bar de media sin oscilación del lazo |
| Error en régimen ≤ 0,3 bar | Media entre 14 y 23 s de 8,4–8,5 bar con referencia de 8,5 bar, y 9,0 bar con referencia de 9,0 bar |
| Sobrepaso ≤ 1 bar | Máximo 0,4 bar sobre la referencia, con preinfusión |
| Régimen antes de 14 s | Entre 10 y 12 s en las cuatro extracciones con preinfusión |
| Funciona con distintas resistencias del lecho | Moliendas de 32, 38 y 44 clics; la bomba quedó entre el 40 y el 66 % |
| El límite duro corta y lo registra | A 12,3 bar la máquina pasó a alarma y apagó la bomba en el mismo ciclo |
| Plazos de ejecución | 0 ciclos fuera de plazo en 198 360 |

No se probaron referencias por debajo de unos 7 bar, porque la canastilla
presurizada no deja pasar el agua bajo su propia presión de apertura, ni por
encima de 9 bar. Tampoco se probaron dosis distintas de 14 g.

## Temperatura (versión 12, vigente)

La caldera se modeló como una planta de primer orden con retardo, identificada
con un escalón del 2 % de potencia durante 3 horas desde la caldera fría. El
ajuste tiene un error cuadrático medio de 0,57 °C.

| Parámetro | Valor | Significado |
|---|---|---|
| Ganancia de la planta | 32,58 °C por punto porcentual | Temperatura que gana la caldera por cada punto de potencia sostenido |
| Constante de tiempo | 2076 s | La caldera tarda unos 35 minutos en responder |
| Retardo | 44 s | Tiempo hasta que el sensor nota el calentamiento |
| λ | 45 s | Constante de tiempo pedida al lazo cerrado |
| Filtro de la medida | 0,5 s | Suaviza el ruido del termistor |
| Rampa de la referencia | 0,5 °C/s | Velocidad máxima de cambio de la referencia |
| Techo del PI | 10 % | Aporte máximo de la acción proporcional-integral |
| Prealimentación | 0,75 % por % | Potencia del calentador que se añade por cada punto de potencia de la bomba, mientras corre el agua |
| Arranque rápido | margen 25 °C; hasta 70 s; referencia máxima 106 °C | Refuerzo del calentamiento cuando la caldera está lejos de la referencia |
| Límite duro | 120,0 °C | Por encima, la máquina corta y pasa a alarma |

**Del agua a la caldera.** El barista pide la temperatura del agua sobre el
café, pero el lazo regula la caldera. El acta fija la relación entre las dos,
medida con un termistor en la cavidad del portafiltro en 17 usos del 28 de
octubre:

agua = 0,9093 · referencia de caldera − 2,39 °C (error cuadrático medio 0,51 °C)

Con esa recta, 92 °C de agua piden 103,81 °C de caldera. El firmware solo
acepta temperaturas del agua entre 85,72 y 92,97 °C, que corresponden al rango
de caldera medido, de 96,9 a 104,88 °C.

**Primer uso tras un reposo.** Después de 30 minutos sin uso, el grupo y el
portafiltro se enfrían y el primer café sale más frío. El acta autoriza subir la
referencia de caldera hasta 4,0 °C solo para esa preparación, y una
prealimentación de hasta 1,25. Con +4,0 °C, el primer uso quedó a +0,20 °C de la
media de los cuatro siguientes, dentro del criterio de 0,5 °C. Con +2,4 y
+2,9 °C quedó 0,80 y 0,70 °C por debajo.

| Criterio | Resultado |
|---|---|
| La recta predice el agua | Cambia respecto de la versión 6, que quedaba entre 1,9 y 3,7 °C sobre lo medido |
| El primer uso queda a ≤ 0,5 °C de los siguientes | Cumple con +4,0 °C tras 30 minutos de reposo |
| Lo demás no cambia | Planta, sintonía, filtro, límites y prealimentación son los de las versiones anteriores |

Lo no probado:

- La recta se midió en un solo día, pulsando 2 minutos después de que la
  máquina indicara «lista». Pulsar antes o mucho después da el agua más fría.
- Con café, la temperatura sobre el lecho depende de la molienda, y la recta se
  midió con agua sola.
- No se midieron reposos de más de 30 minutos con la subida del primer uso, ni
  subidas mayores de 4,0 °C, que el firmware rechaza.

## Temperatura (versión 7)

Tiene la misma planta, la misma sintonía y la misma subida máxima del primer
uso que la versión 12. Difiere en tres puntos:

- La recta del agua se tomaba contra el metal de la caldera:
  agua = 0,8063 · metal + 9,84 °C. Dejaba el agua entre 1,9 y 3,7 °C más
  caliente de lo medido el 28 de octubre, y por eso la versión 12 la reemplazó.
- El arranque rápido tenía un margen de 20 °C y una referencia máxima de 93 °C.
- No tenía el refuerzo de la prealimentación en el primer uso.

Se conserva porque la réplica 1.0.0 del controlador, en `model/controllers/`,
la usa.
