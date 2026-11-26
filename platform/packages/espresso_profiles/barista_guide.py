"""Lo que la plataforma le dice al barista antes de preparar, con su origen.

Pedido del usuario del 17-oct-2026: información útil para el barista, cada dato
marcado como **medido** o **estimado**, con el ámbito en que vale, y fuera de ese
ámbito la respuesta es «no hay datos». Tres datos, todos cerrados el 18-oct:

- **Masa en taza según el tiempo de corte, y tiempo para un ratio** (estimado):
  el modelo de E3, `m = q·max(T − t0, 0) + g` (`model/modelo_presion/
  masa_taza.json`; método en `prediccion-masa-2026-10-17.md`). Se lee del JSON
  y no se copian los coeficientes, para que un reajuste no deje dos versiones.
  La incertidumbre es ±2σ.
- **Cuánto esperar después de «lista»** y **la temperatura del agua** (medidos,
  acta v6, `temperatura-cierre-2026-10-17.md`).

**Qué no hace.** No extrapola: un tiempo, una dosis o un ratio fuera de lo que
cubren los ensayos devuelve «no hay datos» con el motivo. No ajusta ningún
modelo ni decide nada por el barista.
"""

from __future__ import annotations

import json
from functools import cache
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
MASS_MODEL = ROOT / "model/modelo_presion/masa_taza.json"
MASS_METHOD = "docs/engineering/evidencias/presion-ensayos/prediccion-masa-2026-10-17.md"
WATER_EVIDENCE = "docs/engineering/evidencias/temperatura-cierre-2026-10-17.md"

MEASURED = "measured"
ESTIMATED = "estimated"
NO_DATA = "no-data"


@cache
def _mass_model() -> dict[str, Any]:
    return json.loads(MASS_MODEL.read_text(encoding="utf-8"))


def _scope() -> dict[str, Any]:
    valid = _mass_model()["valido"]
    return {
        "doseG": valid["dosis_g"],
        "pressureBar": valid["presion_bar"],
        "cutSeconds": list(valid["tiempo_s"]),
        "grindClicks": list(valid["molienda_clics"]),
        "basket": valid["canastilla"],
    }


def _uncertainty_g() -> float:
    return round(2 * _mass_model()["sigma_g"], 1)


def cup_mass(cut_seconds: float, dose_g: float) -> dict[str, Any]:
    """Masa en taza estimada para un corte, o «no hay datos» fuera del ámbito."""
    model = _mass_model()
    low, high = model["valido"]["tiempo_s"]
    base = {"source": MASS_METHOD, "scope": _scope(), "cutSeconds": cut_seconds,
            "doseG": dose_g}
    if dose_g != model["dosis_g"]:
        return {**base, "status": NO_DATA,
                "reason": f"solo se midió con {model['dosis_g']:g} g de café"}
    if not low <= cut_seconds <= high:
        return {**base, "status": NO_DATA,
                "reason": f"solo se midieron cortes entre {low} y {high} s"}
    mass = model["q_g_s"] * max(cut_seconds - model["t0_s"], 0.0) + model["goteo_g"]
    return {**base, "status": ESTIMATED, "massG": round(mass, 1),
            "uncertaintyG": _uncertainty_g()}


def time_for_ratio(ratio: float, dose_g: float) -> dict[str, Any]:
    """Tiempo de corte estimado para un ratio taza/café, o «no hay datos»."""
    model = _mass_model()
    low, high = model["valido"]["tiempo_s"]
    base = {"source": MASS_METHOD, "scope": _scope(), "ratio": ratio, "doseG": dose_g}
    if dose_g != model["dosis_g"]:
        return {**base, "status": NO_DATA,
                "reason": f"solo se midió con {model['dosis_g']:g} g de café"}
    target = ratio * dose_g
    seconds = model["t0_s"] + (target - model["goteo_g"]) / model["q_g_s"]
    if not low <= seconds <= high:
        return {**base, "status": NO_DATA,
                "reason": f"ese ratio pediría un corte fuera de {low}–{high} s, que no se midió"}
    return {**base, "status": ESTIMATED, "cutSeconds": round(seconds, 1),
            "massG": round(target, 1), "uncertaintyG": _uncertainty_g()}


def machine_facts() -> list[dict[str, Any]]:
    """Lo medido sobre la espera y el agua, con su origen (acta v6, 18-oct)."""
    return [
        {
            "id": "wait-after-ready",
            "status": MEASURED,
            "summary": "Después de «lista», espere de 1 a 2 min antes de pulsar; esperar más "
                       "no cambia el agua. Pulsar justo en «lista» da de 1 a 2 °C menos.",
            "source": WATER_EVIDENCE,
        },
        {
            "id": "water-continuous-use",
            "status": MEASURED,
            "summary": "En uso continuo el agua llega al café a unos 92,2 °C con 92 °C pedidos.",
            "source": WATER_EVIDENCE,
        },
        {
            "id": "water-after-rest",
            "status": MEASURED,
            "summary": "Después de unos 20 min sin usar la máquina, los primeros 4 o 5 cafés "
                       "salen de 1 a 4 °C más fríos.",
            "source": WATER_EVIDENCE,
        },
    ]
