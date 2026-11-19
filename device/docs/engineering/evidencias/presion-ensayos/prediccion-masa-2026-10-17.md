# Predicción de la masa en taza según el tiempo de corte · método escrito antes de ajustar (2026-10-17, 12:19)

Pedido por el usuario el 2026-10-17, en el hilo de temperatura, 12:18: que el
sistema le diga al barista qué relación saldrá con el tiempo que elija, o qué
tiempo poner para una relación dada. Aquí está la parte de la masa. Datos: los 15
cafés de E3 (`e3-2026-10-16.md`).

## Forma

Con la canastilla presurizada, el caudal a la taza es casi constante una vez
llena la canastilla, y casi no depende de la molienda (9.6; tabla de masas por
tiempo enviada el 2026-10-17). Por eso se usa:

`m_taza(T) = q · max(T − t₀, 0) + g`

- `T`: tiempo de corte, desde la pulsación.
- `q`: caudal después del llenado.
- `t₀`: tiempo aparente en que empieza a caer café.
- `g`: goteo después del corte.

Para una relación objetivo `R` con dosis `d`, se despeja `T = t₀ + (R·d − g)/q`.

## Ajuste y comprobación

- **Calibración**: los 10 cafés de calibración de E3. Se ajustan `q` y `t₀` con
  la masa medida en la báscula en `T` = 12, 13, …, 27 s, y `g` como la media del
  goteo (masa asentada de 5 a 15 s después del corte, menos la masa en el corte).
- **Comprobación**: los 5 cafés reservados (44-3, 44-5, 38-3, 32-3, 32-5),
  sin usarlos en el ajuste. Indicadores:
  - error de la masa asentada a los 27 s;
  - error de la masa en la taza a los 20, 22 y 25 s.
- **Incertidumbre que se declara**: la desviación de los residuos de los cafés
  de calibración, como intervalo de ±2σ para un café nuevo.

## Rango de validez que se declara

Lo único medido: 13 g, canastilla presurizada de dos tazas, 8,5 bar, metal entre
91,4 y 92,6 °C, moliendas de 32 a 44 clics y cortes de 12 a 27 s. Fuera de eso,
el sistema debe decir que es extrapolación:
- **Otra dosis no se puede predecir**: no se midió.
- **Por encima de 27 s**: E3 no tiene datos.
- **Otra presión**: no se midió.

## Resultados (calculados, 12:22)

Script: `masa_taza.py`.
Parámetros en
[`masa_taza.json`](../../../../../models/modelica/modelo_presion/masa_taza.json),
salida en
`masa_taza.txt`.

**Ajustado con los 10 cafés de calibración:**

`m_taza(T) = 2,084 · max(T − 10,03; 0) + 1,79` (g; T en s)

- `q = 2,084 g/s`, `t₀ = 10,03 s`, goteo `g = 1,79 g` (de −2,1 a 5,6 g).
- **Dispersión entre cafés: σ = 6,6 g** en la masa asentada, así que ±2σ = ±13 g.
  Punto a punto entre 12 y 27 s es menor: σ = 4,1 g.

**Comprobación con los 5 reservados:** el error de la masa asentada va de −4,3
a +5,4 g (media +1,0). Los 5 caen dentro de ±2σ. Error a los 20, 22 y 25 s:
entre −4,3 y +6,0 g.

**Para 13 g:**

| Corte | Masa asentada | Relación |
|---|---|---|
| 18 s | 18,4 ± 13 g | 1:1,4 |
| 20 s | 22,6 ± 13 g | 1:1,7 |
| 22 s | 26,7 ± 13 g | 1:2,1 |
| 24 s | 30,9 ± 13 g | 1:2,4 |
| 27 s | 37,2 ± 13 g | 1:2,9 |

Al revés: 1:1,5 → 18,5 s; **1:2 → 21,6 s**; 1:2,5 → 24,8 s.

## Lo que dicen

- **El tiempo explica la masa media**, con 2,1 g por segundo después de los
  primeros 10 s.
- **La dispersión entre cafés iguales es grande**: ±13 g para un café nuevo, a
  2σ. La forma no la explica, porque sale del café, de la preparación de la
  pastilla y de la báscula, no del tiempo. La predicción debe mostrarse con su
  intervalo, nunca como un número solo.
- **1:2 sale a 21,6 s con las tres moliendas juntas**, y a unos 20 s solo con
  38 clics (9.4). La diferencia la pone sobre todo la serie de 44 clics, más
  lenta y confundida con el molido al aire. El corte a 20 s del perfil v11 se
  calculó para 38 clics. Con este ajuste conjunto, a 20 s se esperan 22,6 g
  (1:1,7), y con 38 clics unos 26 g.
- **Rango de validez**, el del método: 13 g, 8,5 bar, banda 91,4–92,6 °C,
  32–44 clics, canastilla presurizada de dos tazas, de 12 a 27 s. Fuera de eso
  es extrapolación, y en particular **la dosis no se puede predecir**.
