# Fabricación

Salidas de fabricación de las tres tarjetas del controlador. Cada liberación vive
en una subcarpeta con su fecha.

| Carpeta | Tarjeta |
|---|---|
| [`pcb-control-2026-09-20/`](pcb-control-2026-09-20/README.md) | `PCB-CONTROL`: ESP32-S3, conversor ADS1115, báscula HX711 y conectores de sensores, mando y potencia |
| [`pcb-mando-2026-09-20/`](pcb-mando-2026-09-20/README.md) | `PCB-MANDO`: panel con cuatro LED y los pulsadores |
| [`pcb-power-2026-09-20/`](pcb-power-2026-09-20/README.md) | `PCB-POTENCIA`: la única con red de 120 VAC; fuente, protecciones, mando del SSR del calentador y del dimmer de la bomba |

Cada carpeta trae:

| Archivo | Para qué |
|---|---|
| `*-gerber.zip` | Lo que se sube al fabricante: capas de cobre, máscara, serigrafía, contorno y taladros, tal como los exporta EasyEDA |
| `*-plantilla-impresion.pdf` | Las dos caras de cobre a escala 1:1, para comprobar sobre papel que las piezas reales encajan |
| `*-esquematico.pdf` | El circuito, para leer e imprimir |
| `*-esquematico.svg` | El mismo esquemático en texto, para ver los cambios en un diff |

Los proyectos de origen, en EasyEDA Pro (`.eprj2`), están en
[`../easyeda/current/`](../easyeda/current/).

Las tres tarjetas se fabrican en un mismo panel, así que comparten el espesor de
cobre: 1 oz.

## Plantilla de impresión

`plantilla_impresion.py` genera la plantilla a partir del Gerber descomprimido.
Solo usa la biblioteca estándar de Python.

```bash
python3 plantilla_impresion.py <directorio-gerber> <salida.pdf> [nombre-tarjeta]
```

- La cara inferior sale **espejada**, vista desde abajo, que es como se montan
  sus componentes.
- Los pads rectangulares se dibujan rectangulares: el pad cuadrado marca el pin 1
  y es lo que permite orientar la pieza.
- El relleno de cobre no se dibuja, para que se vean los taladros y la
  serigrafía.

Se imprime **al 100 %, sin «ajustar a página»**, y se mide con calibrador la
barra de la cabecera: tiene que dar **50,0 mm**. Si no, la impresora escaló y la
plantilla no sirve.
