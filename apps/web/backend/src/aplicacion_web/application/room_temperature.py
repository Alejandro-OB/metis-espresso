"""La temperatura del cuarto que se supone cuando nadie la anotó.

El gemelo del agua sobre el café (`_twin_water`) necesita el cuarto porque la
pared del portafiltro se enfría hacia él, y la máquina no tiene con qué medirlo.
Hasta el 26-oct-2026, sin el dato anotado no estimaba el agua y el resumen del
café salía vacío. Ahora usa el promedio de la franja horaria del café, sacado
de las lecturas reales de la sonda DS18B20 al aire en nueve días de la puesta
en servicio (7 al 19 de octubre). La tabla, con la fuente de cada lectura,
está en `apps/web/docs/temperatura-del-cuarto.md`.

Por qué un promedio por franja y no un modelo: nueve días sin clima exterior
no explican por qué un día fue más frío que otro; la hora sí se ve en los datos
(la tarde sale unos 2 °C por encima de la mañana). La variación entre días es
de 0,85 °C, y por la sensibilidad del modelo del agua (`agua-portafiltro.md`
§ 3.5, ±3 °C de cuarto → −0,24/+0,53 °C de agua) eso mueve la estimación en
décimas, por debajo de su propio error (~1 °C).

El valor supuesto queda declarado en las suposiciones de la estimación
(`ambientAssumed`); el que anota el barista manda siempre. Este módulo no lee
nada ni sabe de la base: recibe el instante y devuelve el supuesto.
"""

from datetime import datetime, timedelta, timezone

# Hora del laboratorio (Colombia, sin horario de verano).
LAB_TIME = timezone(timedelta(hours=-5))

# (hasta la hora local, sin incluirla; franja; °C). Promedios de la tabla de
# `temperatura-del-cuarto.md`: mañana 06–10 h, tarde 12–18 h, noche 18–23 h.
# Las horas entre franjas, sin lecturas, toman la franja medida más cercana.
ROOM_BY_HOUR = (
    (11, "mañana", 23.2),
    (18, "tarde", 25.4),
    (24, "noche", 24.8),
)


def assumed_room_c(press: datetime) -> tuple[float, str]:
    """El cuarto supuesto para un café pulsado en `press` (con zona) y su franja."""
    hour = press.astimezone(LAB_TIME).hour
    for until, slot, value in ROOM_BY_HOUR:
        if hour < until:
            return value, slot
    raise AssertionError("ROOM_BY_HOUR cubre las 24 horas")
