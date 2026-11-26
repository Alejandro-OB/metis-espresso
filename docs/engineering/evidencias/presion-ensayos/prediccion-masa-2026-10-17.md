# Predicción de la masa en la taza según el tiempo de corte · 2026-10-17

## Para qué es la prueba

El sistema debe decirle al barista qué relación de bebida saldrá con el tiempo
de corte que elija, o qué tiempo poner para una relación dada. Esta prueba
construye y comprueba la parte de la masa en la taza. El método se fijó antes
de ajustar.

## En qué consiste

Con la canastilla presurizada, el caudal a la taza es casi constante una vez
llena la canastilla, y casi no depende de la molienda. Por eso se usa:

`m_taza(T) = q · max(T − t₀, 0) + g`

- `T`: tiempo de corte, contado desde la pulsación.
- `q`: caudal después del llenado.
- `t₀`: tiempo aparente en que empieza a caer café.
- `g`: goteo después del corte.

Para una relación objetivo `R` con dosis `d`, el tiempo de corte es
`T = t₀ + (R·d − g)/q`.

## Cómo se hizo

Los datos son los 15 cafés de la serie E3.

- **Calibración:** los 10 cafés de calibración de E3. Se ajustan `q` y `t₀` con
  la masa medida en la báscula en `T` = 12, 13, …, 27 s. `g` es la media del
  goteo: la masa asentada de 5 a 15 s después del corte, menos la masa en el
  corte.
- **Comprobación:** los 5 cafés reservados (44-3, 44-5, 38-3, 32-3, 32-5), que
  no entran en el ajuste. Los indicadores son el error de la masa asentada a
  los 27 s y el error de la masa en la taza a los 20, 22 y 25 s.
- **Incertidumbre declarada:** la desviación de los residuos de los cafés de
  calibración, como intervalo de ±2σ para un café nuevo.
- **Rango medido:** 13 g, canastilla presurizada de dos tazas, 8,5 bar, metal
  entre 91,4 y 92,6 °C, moliendas de 32 a 44 clics y cortes de 12 a 27 s.

Los parámetros quedan en
[`masa_taza.json`](../../../../model/modelo_presion/masa_taza.json).

## Resultados

**Ajuste con los 10 cafés de calibración:**

`m_taza(T) = 2,084 · max(T − 10,03; 0) + 1,79` (g; T en s)

- `q = 2,084 g/s`, `t₀ = 10,03 s` y goteo `g = 1,79 g` (de −2,1 a 5,6 g).
- Dispersión entre cafés en la masa asentada: σ = 6,6 g, así que ±2σ = ±13 g.
  Punto a punto entre 12 y 27 s es menor: σ = 4,1 g.

**Comprobación con los 5 reservados:** el error de la masa asentada va de −4,3
a +5,4 g (media +1,0 g). Los 5 caen dentro de ±2σ. El error a los 20, 22 y
25 s está entre −4,3 y +6,0 g.

**Predicción para 13 g:**

| Corte | Masa asentada | Relación |
|---|---|---|
| 18 s | 18,4 ± 13 g | 1:1,4 |
| 20 s | 22,6 ± 13 g | 1:1,7 |
| 22 s | 26,7 ± 13 g | 1:2,1 |
| 24 s | 30,9 ± 13 g | 1:2,4 |
| 27 s | 37,2 ± 13 g | 1:2,9 |

En sentido inverso: 1:1,5 → 18,5 s; 1:2 → 21,6 s; 1:2,5 → 24,8 s.

Con las tres moliendas juntas, 1:2 sale a 21,6 s; solo con 38 clics sale a unos
20 s. La diferencia la pone sobre todo la serie de 44 clics, más lenta y
confundida con el molido al aire. Con este ajuste conjunto, a 20 s se esperan
22,6 g (1:1,7); con 38 clics, unos 26 g.

## Conclusión

El tiempo de corte explica la masa media: 2,1 g por segundo después de los
primeros 10 s. La dispersión entre cafés iguales es grande, ±13 g a 2σ para un
café nuevo. La forma no la explica, porque sale del café, de la preparación de
la pastilla y de la báscula, no del tiempo. Por eso la predicción se muestra
siempre con su intervalo, nunca como un número solo.

La predicción vale en el rango medido: 13 g, 8,5 bar, metal entre 91,4 y
92,6 °C, 32 a 44 clics, canastilla presurizada de dos tazas y cortes de 12 a
27 s. Fuera de él, el sistema debe declarar que extrapola. En particular, otra
dosis no se puede predecir, porque no se midió; tampoco hay datos por encima de
27 s ni a otra presión.
