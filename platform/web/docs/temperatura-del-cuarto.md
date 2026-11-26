# Temperatura del cuarto cuando nadie la anota

El gemelo que estima la temperatura del agua sobre el café necesita la
temperatura del cuarto, porque la pared del portafiltro se enfría hacia ella, y
la máquina no tiene con qué medirla. Si el barista no la anota, la plataforma
supone el promedio medido de la franja horaria del café
(`backend/src/aplicacion_web/application/room_temperature.py`). El valor que
anota el barista manda siempre.

## Franjas

Las horas son de Colombia (UTC−5) y los promedios salen de los tramos medidos de
la tabla de lecturas.

| Franja | Horas que cubre | °C | Tramos medidos |
|---|---|---|---|
| Mañana | 00–11 h (medida 06–10 h) | 23,2 | 8 |
| Tarde | 11–18 h (medida 12–18 h) | 25,4 | 11 |
| Noche | 18–24 h (medida 18–23 h) | 24,8 | 9 |

Las horas sin lecturas (10–12 h y 23–06 h) toman la franja medida más cercana.

La media por día, con todos los días con el mismo peso, es **24,5 °C**. Va de
22,5 °C (13-oct) a 25,4 °C (09-oct), con 0,85 °C de desviación entre días. Según
la sensibilidad del modelo del agua, ±3 °C de cuarto mueven la temperatura del
agua entre −0,24 y +0,53 °C. Por eso esa variación mueve la estimación en
décimas de grado.

Esto no es un modelo del clima: nueve días no explican por qué un día fue más
frío que otro. Parte de la subida dentro del día puede deberse a la sonda
cercana a la máquina caliente. Esto es una inferencia, porque el efecto no se
separa del cuarto.

## Lecturas

Las lecturas son de una sonda DS18B20 al aire, tomadas durante las pruebas de
puesta en servicio de octubre de 2026. Los registros de origen no se publican en
este repositorio.

| Día | Tramo (hora local) | °C |
|---|---|---|
| 07-oct | no consta | 24,25–24,50 |
| 09-oct | 16:51–17:57 | media 25,43 |
| 10-oct | 06:00–11:48 | media 24,12 (22,5–25,5) |
| 11-oct | 09:36–10:02 | media 23,96 |
| 11-oct | 12:50–14:05 | media 26,01 |
| 12-oct | 17:13–17:27 | media 24,28 |
| 12-oct | 18:08–18:23 | media 24,95 |
| 13-oct | 08:00–08:16 | 22,00 |
| 13-oct | 16:48–16:56 | media 23,79 |
| 14-oct | 14:09–22:34 | 23,8–26,0 |
| 18-oct | 08:53–11:25 | 23,60 y 25,91 |
| 18-oct | 14:00–00:19 | media 26,14 |
| 19-oct | 06:48–19:58 | 23,37 y 26,03 |

Quedan fuera los días con la sonda en el depósito (14-oct en la mañana, 15 y
16-oct), el NTC auxiliar del 12-oct (±2 °C, sin hora) y todo valor que su
registro marca como supuesto o por omisión (el 23,7 °C de las actas y los 27 °C
por omisión del modelo).
