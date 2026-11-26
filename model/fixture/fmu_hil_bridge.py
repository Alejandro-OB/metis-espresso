#!/usr/bin/env python3
"""Puente HIL FMU <-> ESP32-S3 por USB serie."""
from __future__ import annotations
import argparse
import csv
import time
from pathlib import Path
import serial
from fmu_adapter import FmuPlantAdapter

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--fmu", type=Path, default=Path(__file__).with_name("dist") / "EkoCoffeeMachine.fmu")
    parser.add_argument("--steps", type=int, default=100)
    parser.add_argument("--period", type=float, default=0.1)
    parser.add_argument("--csv", type=Path, default=Path("hil_fmu_run.csv"))
    args = parser.parse_args()
    if not args.fmu.is_file():
        raise SystemExit(f"No existe la FMU: {args.fmu}")
    with serial.Serial(args.port, 115200, timeout=0.25, write_timeout=1) as link:
        link.reset_input_buffer()
        with FmuPlantAdapter(args.fmu) as fmu, args.csv.open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(("step", "time_ms", "mode", "temperature_c", "pressure_bar", "heater", "pump"))
            heater = pump = 0.0
            for step in range(args.steps):
                result = fmu.step(heater_command=heater, pump_command=pump)
                # Sin vapor (ADR-006, rev. 2026-10-06): extracción, purga y reposo.
                mode = 1 if step < args.steps * 0.40 else 3 if step < args.steps * 0.55 else 0
                link.write(f"I,{result.elapsed_ms},{result.temperature_c:.6f},{result.pressure_bar:.6f},{mode}\n".encode("ascii"))
                deadline = time.monotonic() + 1.0
                response = None
                while time.monotonic() < deadline:
                    fields = link.readline().decode("ascii", errors="ignore").strip().split(",")
                    if len(fields) == 4 and fields[0] == "O" and int(fields[1]) == result.elapsed_ms:
                        response = fields
                        break
                if response is None:
                    raise RuntimeError(f"No se recibió respuesta HIL para t={result.elapsed_ms} ms")
                heater, pump = float(response[2]), float(response[3])
                writer.writerow((step, result.elapsed_ms, mode, result.temperature_c, result.pressure_bar, heater, pump))
                output.flush()
                time.sleep(max(0.0, args.period))
    print(f"HIL aprobado: {args.steps} pasos; resultados={args.csv}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
