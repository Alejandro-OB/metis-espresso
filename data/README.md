# Tasting data

Data from the tasting session of 4 November 2026, in which a barista prepared
twelve espressos with Metis and the same coffee, using 13 g of coffee in the
pressurised two-cup basket and a set pressure of 8.5 bar. These files back the
evaluation reported in the article; they contain no personal data.

| File | Content |
|---|---|
| `preparations.csv` | One row per preparation: settings, measurements and the barista's assessment |
| `model-error.csv` | Root-mean-square error between the physical model and the measurements, per preparation |
| `prep09-measured.csv` | Boiler body temperature and line pressure measured during preparation 9 (every 0.5 s) |
| `prep09-pressure-20ms.csv` | Filtered line pressure and pump power density during preparation 9 (every 20 ms) |
| `prep09-simulated.csv` | The same quantities simulated by the physical model for preparation 9 |

Preparation 9 is one of the three repetitions of the fixed recipe (preparations
8 to 10), and the article uses it to compare measurement and simulation.

## `preparations.csv`

| Column | Meaning |
|---|---|
| `run` | Preparation number, in the order of the session |
| `id` | Identifier of the preparation in the platform |
| `grind_clicks` | Grinder setting, in clicks (higher is coarser) |
| `dose_g` | Ground coffee dose, in grams |
| `time_set_s` | Extraction time set in the recipe, in seconds |
| `time_pump_s` | Time the pump actually ran, in seconds |
| `water_set_C` | Water temperature set in the recipe, in °C |
| `water_est_C` | Water temperature estimated by the system; empty for preparation 1, the first after the machine rested, because the estimate is valid only once the group and portafilter are hot |
| `beverage_g` | Beverage mass in the cup, in grams |
| `tds_pct` | Total dissolved solids, in percent |
| `yield_pct` | Extraction yield, in percent |
| `liking` | Barista's overall liking, from 1 to 9 |
| `accepted` | 1 if the barista accepted the beverage, 0 otherwise |
| `decision_and_defects` | Barista's decision and the defects perceived |

## `model-error.csv`

| Column | Meaning |
|---|---|
| `run`, `id` | As in `preparations.csv` |
| `temperature_RMSE_C` | Error of the simulated boiler temperature, in °C |
| `pressure_RMSE_bar` | Error of the simulated line pressure, in bar |

## Time series of preparation 9

Time `t_s` is in seconds from the start of the extraction. Temperatures are in
°C and pressures in bar. `pump_density_pct` is the fraction of mains
half-cycles in which the controller powered the pump.

## Licence

These data are released under the Creative Commons Attribution 4.0
International licence (CC BY 4.0).
