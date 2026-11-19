# Temperatura del cuarto cuando nadie la anota

El gemelo que estima el agua sobre el café necesita la temperatura del cuarto,
porque la pared del portafiltro se enfría hacia ella, y la máquina no tiene con
qué medirla. Si el barista no la anota, la plataforma supone el promedio medido
de la franja horaria del café (`application/room_temperature.py`, 26-oct-2026).
Lo que anota el barista manda siempre.

## Franjas

Hora de Colombia (UTC−5), sobre los tramos medidos de la tabla de abajo.

| Franja | Horas que cubre | °C | Tramos medidos |
|---|---|---|---|
| Mañana | 00–11 h (medida 06–10 h) | 23,2 | 8 |
| Tarde | 11–18 h (medida 12–18 h) | 25,4 | 11 |
| Noche | 18–24 h (medida 18–23 h) | 24,8 | 9 |

Las horas sin lecturas (10–12 h y 23–06 h) toman la franja medida más cercana.

Media por día, con cada día igual: **24,5 °C**, de 22,5 °C (13-oct) a 25,4 °C
(09-oct), con 0,85 °C de desviación entre días. Con la sensibilidad del modelo
del agua (`models/modelica/Documentacion/agua-portafiltro.md` § 3.5: ±3 °C de
cuarto, −0,24/+0,53 °C de agua), esa variación mueve la estimación en décimas.

No es un modelo del clima: nueve días no explican por qué un día fue más frío
que otro. Parte de la subida dentro del día puede ser la sonda junto a la máquina
caliente (inferido; no se separa del cuarto).

## Lecturas

Sonda DS18B20 al aire, según el texto de cada evidencia. Rutas desde
`device/docs/engineering/evidencias/` salvo que se diga otra cosa.

| Día | Tramo (hora local) | °C | Fuente |
|---|---|---|---|
| 07-oct | no consta | 24,25–24,50 | `../puesta-en-servicio.md:279` |
| 09-oct | 16:51–17:57 | media 25,43 | `5.6-enfriamiento-20261009/ambiente.csv` |
| 10-oct | 06:00–11:48 | media 24,12 (22,5–25,5) | `5.1-escalon-20261010/ambiente.csv.xz`, `sintonia-termica-20261010/ambiente.csv.xz` |
| 11-oct | 09:36–10:02 | media 23,96 | `7.9-prealimentacion-20261011/notas.txt`, `agua-caldera-20261011/auxiliar.csv.xz` |
| 11-oct | 12:50–14:05 | media 26,01 | `7.9-prealimentacion-20261011/notas.txt` |
| 12-oct | 17:13–17:27 | media 24,28 | `arranque-rapido-20261012/auxiliar-26c.csv.xz` |
| 12-oct | 18:08–18:23 | media 24,95 | `arranque-rapido-20261012/auxiliar-55c-ntc.csv.xz` |
| 13-oct | 08:00–08:16 | 22,00 | `arranque-rapido-20261013/auxiliar.csv.xz` |
| 13-oct | 16:48–16:56 | media 23,79 | `arranque-rapido-20261013/auxiliar-acta-v3.csv.xz` |
| 14-oct | 14:09–22:34 | 23,8–26,0 | `ntc-aux-2026-10-14.md` |
| 18-oct | 08:53–11:25 | 23,60 y 25,91 | `cafe-temperatura-20261018/ley-tau/sonda18.csv.xz` |
| 18-oct | 14:00–00:19 | media 26,14 | `models/modelica/agua_portafiltro/extracto.csv.xz` |
| 19-oct | 06:48–19:58 | 23,37 y 26,03 | `models/modelica/agua_portafiltro/extracto.csv.xz` |

Quedan fuera los días con la sonda en el depósito (14-oct de mañana, 15 y 16-oct),
el NTC auxiliar del 12 (±2 °C, sin hora) y todo valor que su archivo marca como
supuesto o por omisión (el 23,7 de las actas, el 27 °C por omisión del modelo).
