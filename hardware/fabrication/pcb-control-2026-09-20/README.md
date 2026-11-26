# PCB-CONTROL · liberación 2026-09-20

Tarjeta lógica del controlador. Lleva la tarjeta ESP32-S3, el conversor `ADS1115`
que lee el NTC de la caldera y el transductor de presión, el amplificador `HX711`
de la báscula y los conectores hacia `PCB-MANDO` y `PCB-POTENCIA`. No tiene red
de 120 VAC.

## Archivos

| Archivo | Para qué |
|---|---|
| `pcb-control-gerber.zip` | Lo que se sube al fabricante |
| `pcb-control-plantilla-impresion.pdf` | Plantilla 1:1 de las dos caras (ver [`../README.md`](../README.md)) |
| `pcb-control-esquematico.pdf` | Esquemático |
| `pcb-control-esquematico.svg` | Esquemático en texto |

La lista de materiales está en
[`../../easyeda/current/pcb-control-bom.csv`](../../easyeda/current/pcb-control-bom.csv).

## Datos de la tarjeta

| | |
|---|---|
| Dimensiones | 65,0 × 90,0 mm |
| Capas | 2 · cobre **1 oz** |
| Componentes | 35 |
| Taladros | 257 metalizados + 118 vías |
| Fijación | dos ranuras de **5,0 × 4,0 mm** en el borde inferior |
| DRC | sin violaciones |

## Orientación del ADS1115

Es el punto de montaje que más cuesta equivocar. Montado al revés, `+3V3` entra
por `AIN3` y el conversor se destruye sin aviso; girar el módulo no sirve, porque
su cuerpo invadiría la ESP32.

La fila de diez pads tiene el **pad 1 rectangular**, que es donde va `VDD` del
módulo:

| Pad | X (mil) | Red |
|---:|---:|---|
| 1 | 700 | `+3V3` (rectangular) |
| 2 | 800 | `GND_ISO` |
| 3 | 900 | `I2C_SCL` |
| 4 | 1000 | `I2C_SDA` |
| 5 | 1100 | `GND_ISO` |
| 6 | 1200 | — |
| 7 | 1300 | `PRESS_ADC` |
| 8 | 1400 | `TEMP_ADC` |
| 9 | 1500 | — |
| 10 | 1600 | — |

Antes de pedirla, conviene comprobar sobre la plantilla impresa que el módulo
apoya con `VDD` sobre el pad cuadrado.

## Al leer el trazado

- `I2C_SCL` e `I2C_SDA` rodean la tarjeta por el borde derecho (68 y 92 mm de
  recorrido). Son señales de bus lento y esa longitud no las afecta.
- La derivación de `+3V3` al `ADS1115` va a 10 mil, más estrecha que el resto de
  la red, porque entre dos pads contiguos no cabe una pista más ancha. El
  conversor consume microamperios.

## Al montar

- El conector de diez posiciones es de paso **2,50 mm**, no 2,54. El de
  `PCB-MANDO` sí es de 2,54: no son intercambiables.
- `C6` es de película, 2,2 µF, con huella de paso 20 mm y cuerpo de 23 mm.
  Montado en la cara inferior sobresale unos 19,5 mm más la patilla.
- `JP_EXT_5V` es un enlace removible en serie con `+5V_ISO`, la alimentación de
  5 V que llega de `PCB-POTENCIA`. **Hay que retirarlo antes de conectar un cable USB-C**
  a la tarjeta, para no unir las dos fuentes de 5 V.
