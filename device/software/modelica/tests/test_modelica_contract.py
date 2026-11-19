from __future__ import annotations

import importlib.util
import json
import math
import unittest
import zipfile
from pathlib import Path
from xml.etree import ElementTree

from software.modelica.fmu_adapter import FmuPlantAdapter
from software.modelica.reference_model import ReferencePlant


MODEL_ROOT = Path(__file__).parents[1]
FMPY_AVAILABLE = importlib.util.find_spec("fmpy") is not None


class ModelicaContractTest(unittest.TestCase):
    def test_contract_declares_expected_fmi_interface(self) -> None:
        contract = json.loads((MODEL_ROOT / "fmu-contract.json").read_text())
        self.assertEqual(contract["fmiVersion"], "2.0")
        self.assertEqual(contract["kind"], "co-simulation")
        self.assertEqual(contract["authority"], "simulation-only")
        self.assertEqual({item["name"] for item in contract["inputs"]}, {"heaterCommand", "pumpCommand"})
        self.assertEqual({item["name"] for item in contract["outputs"]}, {"temperatureC", "pressureBar"})

    def test_reference_plant_generates_temperature_and_pressure(self) -> None:
        plant = ReferencePlant()
        for _ in range(100):
            output = plant.step(heater_command=1.0, pump_command=0.75)
        self.assertGreater(output.temperature_c, 22.0)
        self.assertAlmostEqual(output.pressure_bar, 9.0, places=5)
        self.assertAlmostEqual(output.time_seconds, 10.0, places=9)

    def test_reference_plant_rejects_invalid_commands(self) -> None:
        with self.assertRaises(ValueError):
            ReferencePlant().step(heater_command=1.1, pump_command=0.0)

    def test_exported_fmu_matches_contract_if_present(self) -> None:
        fmu_path = MODEL_ROOT / "dist" / "EkoCoffeeMachine.fmu"
        if not fmu_path.exists():
            self.skipTest("la FMU todavía no fue exportada")
        contract = json.loads((MODEL_ROOT / "fmu-contract.json").read_text())
        with zipfile.ZipFile(fmu_path) as archive:
            root = ElementTree.fromstring(archive.read("modelDescription.xml"))
        model_variables = root.find("ModelVariables")
        self.assertIsNotNone(model_variables)
        variables = {
            variable.attrib["name"]: variable
            for variable in list(model_variables) if model_variables is not None
        }
        expected = {
            item["name"]: item
            for item in contract["inputs"] + contract["outputs"]
        }
        self.assertTrue(expected.keys() <= variables.keys())
        for name, item in expected.items():
            self.assertEqual(variables[name].attrib.get("causality"), item["causality"])
            real = variables[name].find("Real")
            self.assertIsNotNone(real)
            self.assertEqual(real.attrib.get("unit"), item["unit"])

    @unittest.skipUnless(FMPY_AVAILABLE, "se requiere FMPy para ejecutar la FMU")
    def test_exported_fmu_advances_and_matches_reference(self) -> None:
        fmu_path = MODEL_ROOT / "dist" / "EkoCoffeeMachine.fmu"
        if not fmu_path.exists():
            self.skipTest("la FMU todavía no fue exportada")
        reference = ReferencePlant()
        with FmuPlantAdapter(fmu_path) as adapter:
            for _ in range(100):
                actual = adapter.step(heater_command=1.0, pump_command=0.75)
                expected = reference.step(heater_command=1.0, pump_command=0.75)
        self.assertEqual(actual.elapsed_ms, 10_000)
        self.assertAlmostEqual(actual.temperature_c, expected.temperature_c, places=9)
        self.assertAlmostEqual(actual.pressure_bar, expected.pressure_bar, places=9)

    @unittest.skipUnless(FMPY_AVAILABLE, "se requiere FMPy para ejecutar la FMU")
    def test_detailed_fmu_responds_to_heater_and_pump(self) -> None:
        detailed_fmu = (
            MODEL_ROOT.parents[2]
            / "models"
            / "modelica"
            / "dist"
            / "FmuAdapterV1.fmu"
        )
        if not detailed_fmu.exists():
            self.skipTest("la FMU física detallada no está disponible en este árbol de trabajo")
        with FmuPlantAdapter(detailed_fmu) as adapter:
            initial = adapter.step(heater_command=0.0, pump_command=0.0)
            maximum_pressure = 0.0
            for index in range(1, 1_200):
                elapsed = index * 0.1
                output = adapter.step(
                    heater_command=1.0,
                    pump_command=0.75 if 60.0 <= elapsed < 90.0 else 0.0,
                )
                maximum_pressure = max(maximum_pressure, output.pressure_bar)
        self.assertTrue(math.isfinite(output.temperature_c))
        self.assertTrue(math.isfinite(output.pressure_bar))
        self.assertGreater(output.temperature_c, initial.temperature_c + 50.0)
        self.assertGreater(maximum_pressure, 5.0)
        self.assertLess(abs(output.pressure_bar), 0.1)


if __name__ == "__main__":
    unittest.main()
