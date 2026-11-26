"""Recta caldera → agua de la placa, y el agua que acepta en un perfil.

El perfil pide la temperatura del agua sobre el café y la placa la convierte a
referencia de caldera con la recta de su acta de temperatura,
`caldera = (agua − ordenada) / pendiente`. Solo admite el agua cuya caldera
quede dentro del intervalo medido; fuera de eso rechaza el perfil con
`temperature-out-of-range` al activarlo.

**La recta cambia con el acta.** La v6 (18-oct-2026) rigió hasta la v10; la v11
(28-oct-2026) la sustituye con la curva medida ese día, que da 2,6 °C menos de
agua para la misma caldera, y la v12 (28-oct-2026) la toma contra la referencia
que fija la placa en vez de contra el metal. Una preparación se lee con la recta con que la hizo
la placa, no con la vigente: con la vigente, un café de la v6 se juzgaría contra
una caldera que nadie buscó. Desde la v11 cada registro declara su recta
(`waterReference`); los anteriores no traen el campo y se hicieron con la v6.
Un registro que lo trae a null no consta, y no se le atribuye ninguna.

**Dos rectas, no una.** La de conversión es la que usa la placa para pasar del
agua a la referencia de caldera; la del metal da el agua desde el metal medido
al pulsar. Hasta la v11 eran la misma, porque se suponía el metal en la
referencia. Desde la v12 no: tras cada café la caldera se recupera pasándose y
llega al pulsar unos 0,8 °C sobre la referencia, y la recta de conversión lleva
ese repunte dentro. Estimar el agua desde el metal con ella la daría alta; para
eso sigue la del metal medida el 28-oct (`metal_line_of`).

`CURRENT` es la recta de conversión de la placa ahora: con ella se acepta un
perfil, se juzga la referencia y simula la planta virtual. `CURRENT_METAL` es la
del metal, con la que se estima el agua de lo que está pasando. Sus números
son los de `commissioned/temperature_plant.h`, y el rango del agua se deriva de
ellos en vez de copiar el par, para que un acta nueva solo pida cambiar los
mismos cuatro números que en el firmware.

**Qué no hace.** No convierte nada para la placa: la conversión es suya, y
hacerla también aquí enviaría una referencia convertida dos veces. La
conversión de `boiler_reference_c` es solo para la planta virtual y para
juzgar.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True, slots=True)
class WaterLine:
    """Una recta `agua = gain · caldera + offset_c`, medida en su intervalo."""

    method: str
    gain: float
    offset_c: float
    boiler_minimum_c: float
    boiler_maximum_c: float

    @property
    def water_minimum_c(self) -> float:
        return round(self.gain * self.boiler_minimum_c + self.offset_c, 2)

    @property
    def water_maximum_c(self) -> float:
        return round(self.gain * self.boiler_maximum_c + self.offset_c, 2)

    def boiler_reference_c(self, water_c: float) -> float:
        """La referencia de caldera que la placa fija para un agua pedida."""
        return (water_c - self.offset_c) / self.gain

    def water_c(self, boiler_c: float) -> float | None:
        """El agua que da una caldera, o None fuera del intervalo medido."""
        if not self.boiler_minimum_c <= boiler_c <= self.boiler_maximum_c:
            return None
        return round(self.gain * boiler_c + self.offset_c, 2)


#: Acta de temperatura v6 (18-oct-2026), vigente en la placa hasta la v10.
LINE_V6 = WaterLine("acta-temperatura-v6", 0.8063, 9.84, 88.6, 102.49)
#: Acta de temperatura v11 (28-oct-2026), contra el metal al pulsar.
LINE_V11 = WaterLine("acta-temperatura-20261028-v11", 0.8819, -0.49, 97.3, 105.6)
#: Acta de temperatura v12 (28-oct-2026), contra la referencia: la de la placa ahora.
CURRENT = WaterLine("acta-temperatura-20261028b-v12", 0.9093, -2.39, 96.9, 104.88)
#: La recta del metal que acompaña a la vigente.
CURRENT_METAL = LINE_V11

#: Para cada recta de conversión tomada contra la referencia, la del metal medida
#: con ella. Las que no figuran se tomaron contra el metal y valen para las dos
#: cosas; un acta nueva contra la referencia tiene que añadirse aquí.
_METAL_LINE_FOR = {CURRENT.method: LINE_V11}

WATER_GAIN = CURRENT.gain
WATER_OFFSET_C = CURRENT.offset_c
BOILER_MINIMUM_C = CURRENT.boiler_minimum_c
BOILER_MAXIMUM_C = CURRENT.boiler_maximum_c
WATER_MINIMUM_C = CURRENT.water_minimum_c
WATER_MAXIMUM_C = CURRENT.water_maximum_c


def line_of(extraction: dict[str, Any]) -> WaterLine | None:
    """La recta con que la placa hizo esta preparación, o None si no consta.

    Sin el campo, el registro es anterior a que se declarara y se hizo con la
    v6. Con el campo a null, o mal formado, no consta: se devuelve None y quien
    llame no convierte, en vez de suponer la vigente.
    """
    if "waterReference" not in extraction:
        return LINE_V6
    declared = extraction.get("waterReference")
    if not isinstance(declared, dict):
        return None
    try:
        line = WaterLine(
            str(declared["plant"]), float(declared["gain"]), float(declared["offsetC"]),
            float(declared["boilerMinimumC"]), float(declared["boilerMaximumC"]),
        )
    except (KeyError, TypeError, ValueError):
        return None
    if not line.method or line.gain <= 0 or line.boiler_minimum_c >= line.boiler_maximum_c:
        return None
    return line


def metal_line_of(extraction: dict[str, Any]) -> WaterLine | None:
    """La recta que da el agua desde el metal al pulsar de esta preparación."""
    line = line_of(extraction)
    return None if line is None else _METAL_LINE_FOR.get(line.method, line)


def boiler_reference_c(water_c: float, line: WaterLine = CURRENT) -> float:
    """La referencia de caldera que la placa fija para un agua pedida.

    Por omisión con la recta vigente, que es la que usa la planta virtual: el
    gemelo tiene que convertir igual que la placa para replicar lo mismo.
    """
    return line.boiler_reference_c(water_c)


def water_temperature_accepted(value_c: float) -> bool:
    """Si la placa aceptaría un perfil que pide esta temperatura del agua."""
    return WATER_MINIMUM_C <= value_c <= WATER_MAXIMUM_C


#: Propiedades con las que el catálogo pide la temperatura del agua.
WATER_PROPERTIES = ("water-temperature", "temperature")


def measures_boiler(extraction: dict[str, Any]) -> bool:
    """Si la temperatura de la serie es la del cuerpo de la caldera.

    Lo es en las preparaciones medidas: la placa mide el metal. La práctica de
    la aplicación simula directamente el agua que pide el perfil, así que su
    serie se juzga, como siempre, contra el objetivo del agua.
    """
    return extraction.get("source") == "measured"


def judged_specifications(
    specifications: list[dict[str, Any]],
    boiler_offset_c: float | None = None,
    line: WaterLine | None = CURRENT,
) -> list[dict[str, Any]]:
    """Las especificaciones del perfil más la de caldera que se deriva del agua.

    La placa no mide el agua sobre el café sino el cuerpo de la caldera, y lo
    regula contra la referencia que saca del agua pedida con la recta de su
    acta (`line`, la de la preparación: `line_of`). Juzgar el cuerpo contra el
    objetivo del agua comparaba dos magnitudes distintas: con 92 °C de agua la
    caldera va a 104,9 °C (101,9 con la v6) y saldría siempre «por encima». Lo que se puede juzgar con una medida es el cuerpo frente a
    su referencia, así que de cada especificación del agua se deriva una de
    caldera con la misma recta, y la del agua queda para lo estimado.

    El objetivo incluye la subida del primer uso que la placa aplicó en esa
    preparación, porque esa era su referencia. La tolerancia se convierte con
    la ganancia de la recta, que es lo que equivale a la del agua.

    **Qué no hace.** No modifica las especificaciones que recibe ni inventa una
    tolerancia: sin la del agua, la de caldera tampoco la tiene y queda sin
    juzgar, como cualquier objetivo sin margen. Sin recta (`line` None, la de la
    preparación no consta) no deriva ninguna de caldera: no se sabe qué buscaba.
    """
    result = [dict(item) for item in specifications]
    if line is None:
        return result
    offset = float(boiler_offset_c) if isinstance(boiler_offset_c, int | float) else 0.0
    for index, item in enumerate(specifications):
        if str(item.get("property")) not in WATER_PROPERTIES:
            continue
        derived: dict[str, Any] = {
            "property": "boiler-temperature",
            "unit": item.get("unit") or "°C",
            "strategy": item.get("strategy"),
            "derivedFrom": index,
            "method": line.method,
        }
        if item.get("strategy") == "target" and item.get("target") is not None:
            derived["target"] = round(line.boiler_reference_c(float(item["target"])) + offset, 2)
            tolerance = item.get("tolerance")
            derived["tolerance"] = (
                round(float(tolerance) / line.gain, 2) if tolerance is not None else None
            )
        else:
            for bound in ("minimum", "maximum"):
                value = item.get(bound)
                derived[bound] = (
                    round(line.boiler_reference_c(float(value)) + offset, 2)
                    if value is not None else None
                )
        result.append(derived)
    return result
