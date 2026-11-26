# Identificación térmica de la caldera · 2026-10-10

Pasos 5.1, 5.2 y 5.8 de la puesta en servicio.

## Para qué es la prueba

La prueba obtiene la planta del lazo térmico: la respuesta de la caldera real
al calentador, descrita como un sistema de primer orden con retardo (FOPDT). De
estas constantes salen la sintonía del lazo de temperatura y el acta térmica.
También mide cuánto sigue subiendo la caldera después de cortar el calentador.

## En qué consiste

Se aplica un escalón largo y pequeño de demanda al calentador desde la caldera
fría y se registra la temperatura del metal. Después se ajusta un modelo de
primer orden con retardo a ese registro y se mide el arrastre tras el corte.

## Cómo se hizo

| Condición | Valor |
|---|---|
| Binario | `ident-2pct-v2`, modo restringido con la secuencia `heater-step-2pct-v2`: 30 s de reposo y 2 % durante 3 h; tope del guardián en 2 % |
| Confirmación física | café + tara durante 5 s y encendido, en el mando provisional; secuencia en marcha desde las 06:12:45 |
| Fin | 09:13:15, por la propia secuencia (`finished`, 10 830 000 ms recorridos), sin intervención manual |
| Caldera al empezar | 22,97 °C (NTC, calibración `ntc-2026-10-09` v4): caldera fría |
| Ambiente | DS18B20 al aire junto a la máquina: media 23,68 °C, entre 22,6 y 24,5 °C |
| Tensión de red | 123,3 V CA, medida con multímetro en la toma antes de confirmar |
| Estado de llenado | no se confirmó el día de la prueba |
| Registro | líneas `T` del lazo cada 50 ms y ambiente cada 1 s |

La tensión fue un 3,6 % mayor que los 119 V medidos el 2026-10-07. Como la
potencia del calentador escala con el cuadrado de la tensión, la ganancia
obtenida vale para unos 123 V.

El ajuste usa la herramienta de identificación (`identify.py step`). Para cada
par (θ, τ), `K` se obtiene por mínimos cuadrados; θ y τ se buscan por rejilla y
luego se afinan. La demanda se toma de la columna `u`, es decir, de lo que el
guardián dejó aplicar y no de lo que la secuencia planeaba.

## Resultados

| Constante | Valor |
|---|---|
| Ganancia `K` | 32,58 °C por punto de demanda |
| Constante de tiempo `τ` | 2076 s (34,6 min) |
| Retardo `θ` | 44 s |
| Residuo (RMSE) | 0,57 °C sobre 9315 muestras |
| Constantes de tiempo cubiertas | 5,2: `K` y `τ` quedan separados (`separable: true`) |

Con estos valores, el régimen al 2 % queda en unos 88 °C desde 23 °C, y
mantener 92 °C pide cerca del 2,1 %.

La ganancia es mayor que la estimación preliminar sacada de registros
anteriores (≈ 27 °C/%, con un régimen esperado de 78–86 °C). La tensión explica
una parte: 123 V frente a 119 V dan un 7 % más de potencia. El resto no está
explicado; aquellos registros no eran un escalón limpio.

El retardo también es mayor que el preliminar (2–4 s). Un ajuste con 1 h 28 min
de registro daba θ = 0 y `K` = 33,6 °C/%; con el registro completo, el ajuste
prefiere θ = 44 s. La interpretación más probable, no medida, es que θ absorbe
una dinámica que un primer orden no tiene: el paso del calor del elemento al
cuerpo y del cuerpo al NTC (constante de la vaina ≈ 36 s).

**Arrastre tras el corte.** El último instante con demanda fue a las 09:13:15,
con la caldera en 86,56 °C. La lectura máxima posterior fue 86,63 °C, un segundo
después: +0,07 °C, del orden del temblor del NTC.

## Conclusión

La caldera se describe con `K` = 32,58 °C por punto de demanda, `τ` = 2076 s y
`θ` = 44 s, válidos para una red de unos 123 V. Queda sin determinar si θ es un
retardo físico o el precio de forzar un primer orden. Para la sintonía, un θ
mayor es la opción conservadora, porque SIMC da entonces ganancias menores. Al
2 % no hay arrastre medible tras el corte; con el calentador entero el arrastre
será mayor y debe medirse con perturbación real. El estado de llenado del día
no consta.
