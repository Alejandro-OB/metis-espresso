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

## The twelve preparations

All preparations used 13 g of coffee and a set pressure of 8.5 bar.

| Run | Grind (clicks) | Time set / pumped (s) | Water set / estimated (°C) | Cup (g) | TDS (%) | Yield (%) | Liking (1–9) | Decision and defects |
|---|---|---|---|---|---|---|---|---|
| **Recipe adjustment** | | | | | | | | |
| 1 | 38 | 20 / 19.98 | 92.0 / — | 38.0 | 7.16 | 20.92 | 4 | Reject, acidity |
| 2 | 36 | 20 / 19.96 | 92.0 / 91.48 | 35.3 | 7.27 | 19.75 | 5 | Reject, acidity |
| 3 | 34 | 20 / 20.00 | 92.0 / 91.38 | 34.6 | 6.95 | 18.50 | 5 | Reject, acidity |
| 4 | 32 | 25 / 24.98 | 92.0 / 90.96 | 45.1 | 5.34 | 18.53 | 4 | Reject, acidity and astringency |
| 5 | 32 | 20 / 19.96 | 92.0 / 92.42 | 35.3 | 6.38 | 17.32 | 6 | Accept, none |
| 6 | 38 | 25 / 25.00 | 92.0 / 91.91 | 43.5 | 6.58 | 22.03 | 2 | Reject, acidity and astringency |
| 7 | 41 | 23 / 23.00 | 92.0 / 91.88 | 39.5 | 7.20 | 21.87 | 6 | Accept, none |
| **Recipe repetition** | | | | | | | | |
| 8 | 45 | 23 / 22.96 | 92.0 / 91.65 | 37.7 | 8.04 | 23.32 | 6 | Accept, none |
| 9 | 45 | 23 / 22.97 | 92.0 / 91.04 | 37.2 | 7.65 | 21.88 | 6 | Accept, none |
| 10 | 45 | 23 / 23.00 | 92.0 / 91.63 | 37.8 | 7.90 | 22.96 | 6 | Accept, none |
| **Intentional variations** | | | | | | | | |
| 11 | 45 | 27 / 27.00 | 92.0 / 91.16 | 45.2 | 7.10 | 24.70 | 4 | Reject, bitterness, astringency, vegetal flavor |
| 12 | 45 | 23 / 22.97 | 86.0 / 85.47 | 38.6 | 7.57 | 22.49 | 4 | Reject, acidity, astringency, vegetal flavor |

In runs 1 to 7 the barista adjusted grind and time to find the recipe. Runs 8 to 10 repeated the fixed recipe of 23 s and 92 °C. Runs 11 and 12 changed the time and the water temperature on purpose, one at a time.

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
