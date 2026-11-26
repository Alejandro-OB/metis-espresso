# PCB-MANDO · liberación 2026-09-20

Tarjeta del panel: cuatro LED indicadores, ocho pulsadores y el cable soldado que
sube a `PCB-CONTROL`. No tiene red de 120 VAC.

## Archivos

| Archivo | Para qué |
|---|---|
| `pcb-mando-gerber.zip` | Lo que se sube al fabricante |
| `pcb-mando-plantilla-impresion.pdf` | Plantilla 1:1 de las dos caras (ver [`../README.md`](../README.md)) |
| `pcb-mando-esquematico.pdf` | Esquemático |
| `pcb-mando-esquematico.svg` | Esquemático en texto |

El esquemático agrupa los pulsadores por la función que rotula la serigrafía
—encendido, preparación, vapor y purga— y llama al conector por su destino,
`PCB-CONTROL`.

## Datos de la tarjeta

| | |
|---|---|
| Dimensiones | 26,0 × 96,0 mm |
| Capas | 2 |
| Taladros | 46: 42 metalizados y 4 de montaje |
| Fijación | cuatro agujeros de **3,00 mm** sin metalizar en las esquinas, separados 21,0 × 89,0 mm |
| DRC | sin violaciones |

## LED

| LED | Color | Red |
|---|---|---|
| `LED1` | verde | `LED_SAFE` |
| `LED2` | azul | `LED_READY` |
| `LED3` | amarillo | `LED_BREW` |
| `LED4` | rojo | `LED_ALARM` |

El firmware da a cada color un significado, que ya no es el del nombre de la red:
amarillo, calentando; verde, lista; azul, preparando café o purgando; rojo,
alarma. La tabla completa está en
[`indicator_panel.h`](../../../firmware/main/indicator_panel.h).

### Polaridad

La huella `LED-TH_BD5.0-P2.54_K1` lleva el **cátodo en el pin 1**, que no es la
convención habitual. En la serigrafía, el lado plano de la brida y el pad del
cátodo (a `GND_ISO`) quedan del mismo lado, y el signo `+` junto al ánodo. Antes
de pedirla, conviene comprobar sobre la plantilla impresa que el lado plano de
cada LED cae sobre el lado plano dibujado.

## Pulsadores

Los nombres de las redes coinciden con la serigrafía, no con el uso. El firmware
asigna `BTN_STEAM_N` al purgado y `BTN_PURGE_N` a la tara de la báscula; la
máquina no tiene función de vapor (ver
[`pin_map.h`](../../../firmware/main/pin_map.h) y
`panel_runtime.c`).

## Cable a PCB-CONTROL

- Se sueldan **los diez conductores**, directo a los agujeros, sin conector. La
  fila está numerada 1 a 10 en la serigrafía; el 1 es el extremo de menor Y.
- El pad 1 recibe los 3,3 V del cable y no alimenta nada: los LED se encienden
  desde las salidas de la ESP32 y los pulsadores cierran a masa contra el
  *pull-up* interno. Lleva marcador *No Connect* en el esquemático. Se suelda
  igual, porque reparte el esfuerzo mecánico del cable.
- El paso de esta fila es **2,54 mm** (22,86 mm de extremo a extremo); el de
  `PCB-CONTROL` es 2,50 mm (22,50 mm). Si alguna vez se pone una tira de pines,
  no sirve la misma pieza en las dos tarjetas.

## Lista de materiales

`LED3` usa el dispositivo genérico `5mm_LED` porque la librería no tiene uno
amarillo; el color consta en el designador. La columna de código de proveedor del
BOM no es fiable en esta tarjeta: los otros LED y las resistencias arrastran
nombres internos de librería, no referencias de compra.
