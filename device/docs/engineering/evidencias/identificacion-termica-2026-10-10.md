# Identificación térmica · 2026-10-10 (puesta en servicio 5.1, 5.2 y 5.8)

La caldera real, medida con un escalón largo del calentador, descrita como un
sistema de primer orden con retardo (FOPDT). Estas constantes son la planta de
la que sale la sintonía del lazo térmico
(sintonía térmica) y la que congelará el acta
térmica.

## Ensayo (5.1)

| | |
|---|---|
| Binario | `ident-2pct-v2`, modo restringido con secuencia `heater-step-2pct-v2`: 30 s de reposo y 2 % durante 3 h; tope del guardián 2 % |
| Confirmación física | café + tara 5 s y encendido, en el mando provisional; secuencia en marcha desde las 06:12:45 |
| Fin | 09:13:15, por la propia secuencia (`finished`, 10 830 000 ms recorridos); nadie cortó a mano |
| Caldera al empezar | 22,97 °C (NTC, calibración `ntc-2026-10-09` v4): caldera fría |
| Ambiente | DS18B20 al aire junto a la máquina: media 23,68 °C, entre 22,6 y 24,5 °C |
| Tensión de red | 123,3 V CA, multímetro en la toma, antes de confirmar. Es un 3,6 % más que los 119 V del 2026-10-07, y la potencia del calentador escala con el cuadrado: la ganancia de abajo vale para ~123 V |
| Estado de llenado | **sin confirmar** el día del ensayo |
| Registros | `5.1-escalon-20261010/`: `maquina.csv.xz` (líneas `T` del lazo, 50 ms) y `ambiente.csv.xz` (1 s) |

## Ajuste (5.2)

```
python -m software.commissioning.identify step --machine maquina.csv
```

Primer orden con retardo; `K` por mínimos cuadrados para cada par (θ, τ), y θ y
τ por rejilla con afinado. La demanda se toma de la columna `u`, es decir, de lo
que el guardián dejó aplicar, no de lo que la secuencia planeaba.

| Constante | Valor |
|---|---|
| Ganancia `K` | **32,58 °C por punto de demanda** |
| Constante de tiempo `τ` | **2076 s** (34,6 min) |
| Retardo `θ` | **44 s** |
| Residuo (RMSE) | 0,57 °C sobre 9315 muestras |
| Constantes de tiempo cubiertas | 5,2: `K` y `τ` quedan separados (`separable: true`) |

Con estos valores, el régimen al 2 % queda en unos 88 °C desde 23 °C, y mantener
92 °C pide cerca del 2,1 %.

### Contra lo que se esperaba

- **La ganancia es mayor que la preliminar** de los registros de la 4.1 (≈ 27 °C/%,
  con régimen esperado de 78–86 °C). Parte de la diferencia es la tensión, 123 V
  frente a 119 V, que da un 7 % más de potencia. Lo demás no está explicado; los
  registros de la 4.1 no eran un escalón limpio.
- **El retardo es mayor que el preliminar** (2–4 s). Un ajuste a 1h28 min de
  ensayo daba θ = 0 y `K` = 33,6 °C/%. Con el registro completo, el ajuste
  prefiere θ = 44 s. Lo más probable es que absorba la dinámica que un primer
  orden no tiene: el paso del calor del elemento al cuerpo y del cuerpo al NTC
  (constante de la vaina ≈ 36 s, medida en la 4.1). **Es una interpretación, no
  una medida.** Para la sintonía es lo conservador: SIMC con θ mayor da ganancias
  menores.

## Arrastre tras el corte (5.8)

El último instante con demanda fue a las 09:13:15, con la caldera en 86,56 °C.
La lectura máxima posterior fue 86,63 °C, un segundo después: **+0,07 °C**, del
orden del temblor del NTC. Al 2 % no hay arrastre medible. Con el calentador
entero el arrastre será mayor; se mide en la sintonía con perturbación real
(etapa 7).

## Lo que queda abierto

- El estado de llenado del día.
- Si `θ = 44 s` describe un retardo físico o es el precio de forzar un primer
  orden. Lo responde el modelo de dos nodos de la etapa 10.2.
