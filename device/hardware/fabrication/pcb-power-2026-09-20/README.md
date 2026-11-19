# PCB-POTENCIA · liberación 2026-09-20

> **Es la única tarjeta con red de 120 VAC.** No se energiza sin haber revisado la
> barrera de aislamiento ya soldada (sin restos de estaño, fundente ni puentes) y
> el cableado de los portafusibles descrito abajo.

Recibe la red, la protege y la reparte: fuente aislada `HLK-10M05` de 5 V para la
lógica, varistor con termofusible y condensador X2 a la entrada, mando del SSR del
calentador por un `2N2222` y conexión al dimmer de la bomba, que además entrega a
`PCB-CONTROL` el cruce por cero (`PUMP_ZC`).

## Archivos

| Archivo | Para qué |
|---|---|
| `pcb-power-gerber.zip` | Lo que se sube al fabricante |
| `pcb-power-plantilla-impresion.pdf` | Plantilla 1:1 de las dos caras (ver [`../README.md`](../README.md)) |
| `pcb-power-esquematico.pdf` | Esquemático |
| `pcb-power-esquematico.svg` | Esquemático en texto |

El esquemático sigue el recorrido de la corriente en seis zonas rotuladas. La
sexta, la única con recuadro **verde**, reúne lo que no toca los 120 V: la
conexión a `PCB-CONTROL`, el driver del SSR y los conectores de mando `SSR-DC` y
`DIM-LOG`. Así se ve de un vistazo qué se puede tocar con la máquina enchufada.

## Datos de la tarjeta

| | |
|---|---|
| Dimensiones | 75,0 × 83,0 mm |
| Capas | 2 · cobre **1 oz** |
| Taladros | 59: 55 metalizados y 4 de montaje |
| Fijación | cuatro agujeros de **3,20 mm** sin metalizar, separados 68 × 76 mm |
| DRC | sin violaciones |

A 1 oz, las pistas de potencia van anchas: 4,37 mm en el retorno `AC_NEUTRAL` y
3,93 mm en la rama del calentador.

## Barrera de aislamiento

Medida sobre el Gerber: entre el cobre del dominio de red y el del dominio
aislado quedan **8,90 mm en la cara inferior** y 10,04 mm en la superior. La
regla de diseño fija 8,0 mm de región prohibida en ambas capas.

Esa franja es lo único que impide que la red llegue a la ESP32 y al panel que se
toca con la mano. Un cambio que la cruce invalida la tarjeta, y hay que volver a
medirla cada vez que se reconstruya un relleno de cobre.

## Componentes fuera de la tarjeta

El esquemático incluye, sin huella en el PCB, lo que se instala en el chasis,
para que se vea a dónde llega cada bornera:

| Designador | Qué es |
|---|---|
| `FUSIBLE-12A`, `FUSIBLE-2A` | Portafusibles de panel |
| `SSR` | Relé de estado sólido del calentador |
| `DIMMER` | Módulo dimmer de la bomba, con detector de cruce por cero |
| `RESISTENCIA-BOILER` | Calentador |
| `BOMBA-VIBRATORIA` | Bomba `JYPC-503A` |
| `FUSIBLE-TERMICO-BOILER-IN`, `-OUT` | Fusibles térmicos de la caldera: se funden y hay que sustituirlos. **No se eliminan ni se puentean.** |
| `PROTECTOR-TERMICO-BOMBA` | Protector térmico de la bomba, que rearma |

## Cableado

**Los fusibles no van en la tarjeta.** Las borneras `DESDE-FUS-12A` y
`DESDE-FUS-2A` son por donde **vuelve** el cable desde cada portafusible, no donde
se monta. Sus dos pines son el mismo nodo: conectar ahí el portafusible lo dejaría
en paralelo con la pista y anulado.

**Cruzar esas dos borneras es el error más grave posible.** El calentador, con
8,75 A, quedaría detrás del fusible de 2 A, y la fuente y la bomba detrás del de
12 A, sin protección efectiva.

**El reparto del conductor de línea se hace fuera de la tarjeta.** Ese conductor se
divide hacia `RED-IN`, el portafusible de 12 A y el de 2 A, y por ese nudo pasan
los 9,46 A de toda la máquina. Hace falta una regleta en el chasis.

**El dimmer necesita tres hilos de red.** Su módulo tiene cuatro bornes —`AC-IN`,
`N`, `N`, `AC-OUT`— y detecta el cruce por cero entre `AC-IN` y `N`. Además de los
dos hilos de `DIM-AC`, se lleva un neutro desde uno de los tornillos de neutro
libres de la tarjeta.

### Rótulos de serigrafía

Solo llevan marca los bornes donde equivocarse tiene consecuencias:

| Rótulo | Dónde | Significado |
|---|---|---|
| `A` / `B` | `RED-IN` | `A` es la rama que va a los portafusibles y alimenta el varistor y el X2; `B` es el retorno común. No es polaridad, pero son redes distintas. |
| `SSR+` / `SSR-` | `SSR-DC` | Sí hay polaridad: es el LED del optoacoplador del SSR. Invertido, el calentador no enciende nunca. |

En `NEUTRO-1` y `NEUTRO-2` los dos pines son el mismo nodo, y en `SSR-AC`,
`CALENTADOR` y `BOMBA` los dos conductores son intercambiables.

## Montaje

### 2N2222

La huella `TO-92-3_2N2222_EBC` queda **emisor · base · colector** de izquierda a
derecha, mirando la cara plana, que es el patillaje del `2N2222A`. El pad
rectangular marca el **emisor**, no el pin 1.

| Pad | X (mm) | Forma | Red | Terminal |
|---|---:|:---:|---|---|
| 3 | 57,46 | rectangular | `GND_ISO` | emisor |
| 1 | 60,00 | circular | `SSR_DRIVE_BASE` | base |
| 2 | 62,54 | circular | `SSR_LED_CATHODE` | colector |

Un transistor mal orientado puede dejar el SSR, y con él el calentador,
**encendido de forma permanente** sin que el firmware lo gobierne. Antes de
pedirla, conviene comprobar sobre la plantilla impresa que el emisor apoya sobre
el pad cuadrado.

### Fuente HLK-10M05

Los pines juntos, separados 7,8 mm, van del lado de entrada de red; los separados
22,8 mm, del lado de salida de 5 V.

### Conector de la bomba

`BOMBA` usa el conector original de la cafetera, con huella a medida: paso
7,92 mm y taladro de 1,80 mm. No lleva bornera.

### Varistor y termofusible, juntos

`VARISTOR-14D241K` y `TERMOFUSIBLE-98-GRADOS` tienen huellas separadas
(10,86 mm entre ejes), pero **se montan apoyados uno contra otro y sujetos entre
sí**.

El termofusible protege de la fuga térmica del varistor: un varistor degradado
conduce corrientes de fuga con la tensión normal, se calienta y conduce más.
Esas corrientes son de decenas o cientos de miliamperios y **ningún fusible de
red las ve**. El termofusible solo mide su propia temperatura; separado por aire,
reaccionaría tarde y leería sobre todo la temperatura interior de la cafetera.

Cómo se monta:

1. El disco del varistor va vertical. Se desplaza el termofusible unos 6,5 mm
   hacia él y se apoya su cuerpo contra una de las caras del disco.
2. Se sujetan los dos con tubo termorretráctil que abrace el conjunto, o con cinta
   de kapton o de fibra de vidrio. Es sujeción térmica, no aislamiento: tienen
   que quedar apretados.
3. Las patas se doblan cerca de los pads, nunca junto al cuerpo.

Se comprueba a simple vista: los dos cuerpos tienen que estar unidos.

Un varistor termoprotegido (TMOV) resolvería esto en una sola pieza, pero no se
consiguió; el conjunto atado es la solución adoptada.

La serigrafía dice 98 °C; en la unidad construida se montó un termofusible de
110 °C.

### Al soldar el termofusible

El calor del soldador lo dispara y queda abierto para siempre, y entonces parece
un defecto de la tarjeta. Se suelda rápido, con la punta a temperatura baja y
**sujetando la pata con alicates entre el cuerpo y el punto de soldadura**, para
que hagan de disipador. Los mismos alicates sirven al doblar las patas, para que
el esfuerzo no llegue al sello del encapsulado.
