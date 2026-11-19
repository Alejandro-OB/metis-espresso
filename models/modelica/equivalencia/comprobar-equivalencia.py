#!/usr/bin/env python3
"""Comprueba que el artefacto recompilado ejecuta el mismo modelo (ADR-011 § 4.3).

Repite la ejecución de referencia con el artefacto que se le indique y compara
muestra a muestra contra `referencia-x86_64.json`. No decide nada por su cuenta:
aplica los umbrales que ADR-011 declara y dice si se cumplen.

La comparación es muestra a muestra y no por resumen: dos series pueden tener la
misma media y diferir en cada punto, y lo que se quiere acreditar es que el
modelo es el mismo, no que se parezca en promedio.

Uso:
    comprobar-equivalencia.py <artefacto.fmu> <referencia.json> [salida.json]
"""

from __future__ import annotations

import json
import math
import sys
import tempfile
from pathlib import Path

from espresso_simulation.artifacts import FileSeriesStore
from espresso_simulation.runner import run_simulation

# Umbrales de ADR-011 § 4.3. No se ajustan aquí: si hay que cambiarlos, se
# cambia la decisión primero.
UMBRALES = {
    "temperatureC": {"rmse": 0.025, "maxima": 0.05},
    "pressureBar": {"rmse": 0.01, "maxima": 0.02},
}


def perfil_de_referencia() -> dict:
    """El mismo perfil con el que se generó la referencia, según su README."""
    return {
        "targets": {"temperatureC": 93.0, "pressureBar": 9.0},
        "tolerances": {"temperatureC": 0.5, "pressureBar": 0.5},
    }


def ejecutar(artefacto: Path, duracion: float, paso: float) -> list[dict]:
    with tempfile.TemporaryDirectory() as tmp:
        almacen = FileSeriesStore(tmp)
        resultado = run_simulation(
            "sim-equivalencia",
            artefacto,
            duracion,
            perfil_de_referencia(),
            almacen,
            step_seconds=paso,
        )
        return almacen.read(resultado.artifact)


def comparar(referencia: list[dict], obtenida: list[dict]) -> dict:
    if len(referencia) != len(obtenida):
        raise SystemExit(
            f"las series no son comparables: {len(referencia)} muestras de "
            f"referencia frente a {len(obtenida)} obtenidas"
        )

    informe = {}
    for canal, umbral in UMBRALES.items():
        diferencias = [
            abs(r["channels"][canal] - o["channels"][canal])
            for r, o in zip(referencia, obtenida)
        ]
        rmse = math.sqrt(sum(d * d for d in diferencias) / len(diferencias))
        maxima = max(diferencias)
        informe[canal] = {
            "rmse": rmse,
            "diferenciaMaxima": maxima,
            "umbralRmse": umbral["rmse"],
            "umbralMaxima": umbral["maxima"],
            "cumple": rmse <= umbral["rmse"] and maxima <= umbral["maxima"],
            "muestraDeLaMayorDiferencia": diferencias.index(maxima),
        }
    return informe


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    artefacto = Path(sys.argv[1])
    referencia_path = Path(sys.argv[2])
    salida = Path(sys.argv[3]) if len(sys.argv) > 3 else None

    referencia = json.loads(referencia_path.read_text())
    muestras_ref = referencia["muestras"]

    obtenida = ejecutar(
        artefacto, referencia["duracionSegundos"], referencia["pasoSegundos"]
    )
    informe = comparar(muestras_ref, obtenida)

    equivalente = all(c["cumple"] for c in informe.values())

    print(f"Artefacto:  {artefacto}")
    print(f"Muestras:   {len(muestras_ref)}")
    for canal, datos in informe.items():
        estado = "cumple" if datos["cumple"] else "NO CUMPLE"
        print(
            f"  {canal:<14} rmse={datos['rmse']:.6f} (≤{datos['umbralRmse']}) "
            f"máx={datos['diferenciaMaxima']:.6f} (≤{datos['umbralMaxima']}) → {estado}"
        )
    print(f"Equivalente: {'sí' if equivalente else 'NO'}")

    if salida:
        salida.write_text(
            json.dumps(
                {
                    "artefacto": str(artefacto),
                    "referencia": str(referencia_path),
                    "muestras": len(muestras_ref),
                    "canales": informe,
                    "equivalente": equivalente,
                    "umbralesDe": "ADR-011 § 4.3",
                },
                indent=2,
                ensure_ascii=False,
            )
            + "\n"
        )

    return 0 if equivalente else 1


if __name__ == "__main__":
    raise SystemExit(main())
