"""Réplica en la planta virtual del control que corre en la placa (`board-replica`).

La réplica de una preparación real (paso 10.6) tiene que gobernar la FMU v2 con
el mismo control que gobernó la cafetera: si no, la diferencia entre la serie
medida y la simulada mezcla el error del modelo con el de un controlador que no
existe. `ProfileTrackingController` es eso, un control inventado para la planta
virtual —calienta al 80 % durante el café y apaga la bomba cuando el metal baja
de la banda—, y la primera réplica real (sim-00000342, 2026-10-20) dio 7,9 °C y
1,5 bar de RMSE por él.

Fuente de cada pieza, todas del firmware de `device/firmware/esp32-s3-single-mcu/main/`:

- **PI unilateral** (`pi_controller.c`, `pi_controller_update_holding`):
  referencia interna con limitador que solo frena subidas, integral candidata
  adoptada solo si la salida no satura o si el error empuja hacia dentro.
- **Sintonía SIMC** (`control_tuning.c`): Kp = τ / (K·(λ + θ)),
  Ti = min(τ, 4·(λ + θ)), con el retardo no menor que medio periodo.
- **Filtro de primer orden** (`control_filter.c`): α = T / (τf + T), cebado con
  la primera muestra.
- **Lazo de presión** (`control_runtime.c`, `run_pressure`): cada 20 ms; la
  preinfusión (`pressure_preinfusion.c`) lleva la bomba al techo del acta hasta
  que la presión, con su filtro propio, llega a la referencia menos el margen,
  y entrega el PI con la integral de arranque; densidad PSM por redondeo
  (`psm_scheduler_quantize`).
- **Lazo térmico** (`control_runtime.c`, `run_temperature`): cada 200 ms, con la
  densidad que la presión ordenó en ese mismo ciclo; prealimentación `ff-v1`
  sumada fuera del PI y con su corte por falta de agua (`heater_feedforward.c`);
  demanda entera por redondeo (`heater_modulator_quantize`).

Los números salen de las actas, a través de la definición versionada
(`models/modelica/controllers/board-replica-v1.json`), que cita cada una con su
versión y su huella; este módulo no lleva ninguno propio salvo los periodos, que
son de la arquitectura del lazo y no de la planta (ADR-007 § 3.3, ADR-009 § 3.6).

**Qué no hace**, y por qué:

- **No replica la preparación entera, solo desde el pulsado.** Arranca con la
  bomba autorizada y la extracción en curso, como la serie que compara. El
  estado inicial se declara en `BoardReplicaController.__init__`.
- **No reproduce el arranque rápido ni la retención o devolución de la
  integral.** El arranque rápido solo actúa en «calentando» y «lista», nunca en
  la extracción (`heater_fast_start_step` con `heating` falso), y el acta v7 no
  trae retención ni devolución (`COMMISSIONED_TEMPERATURE_RECOVERY_*` no está en
  `commissioned/temperature_plant.h`).
- **No reproduce al guardián ni a los límites duros.** Deciden por su ruta y
  detienen la máquina; replicarlos haría que un pico del modelo cortara la
  simulación, lo que diría algo del modelo y nada del control.
- **No modula dentro de la ventana de 1 s ni emite pulsos PSM.** La FMU v2
  recibe la demanda media del calentador y la densidad media de la bomba
  (`fmu-contract-v2.json`), que es con lo que se calibraron sus piezas.
- **No ve la medida más que cada paso de comunicación.** La FMU entrega una
  salida cada 0,1 s; los cinco ciclos de 20 ms de presión de ese intervalo usan
  la misma medida, que es un retardo de muestreo que la placa no tiene.
- **No conoce el reloj ni la planta**: avanza su propio contador de ciclos con
  el paso que se le declara y decide con lo que recibe. Calcula en doble
  precisión; el firmware, en `float`.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

#: Identificador de la definición (`board-replica-v1.json`) que activa esta réplica.
BOARD_REPLICA_ID = "board-replica"

#: Periodos de `control_runtime.c`: 20 ms la presión (ADR-007 § 3.3) y diez
#: ciclos de ella la temperatura (ADR-009 § 3.6). Son de la arquitectura, no del
#: acta, y por eso están aquí y no en la definición.
PRESSURE_PERIOD_S = 0.020
THERMAL_DECIMATION = 10
#: Escala completa de la densidad PSM y de la demanda del calentador (0–100).
FULL_SCALE_PERCENT = 100.0


def _clamp(value: float, minimum: float, maximum: float) -> float:
    return min(max(value, minimum), maximum)


def simc_gains(
    gain: float, time_constant_s: float, dead_time_s: float, lambda_s: float, period_s: float
) -> tuple[float, float]:
    """Kp y Ki como los calcula `control_tuning_from_identified`.

    El retardo no baja de medio periodo (`effective_dead_time`): con θ = 0 la
    fórmula no sería una sintonía sino una división por cero.
    """
    dead_time = max(dead_time_s, 0.5 * period_s)
    horizon = lambda_s + dead_time
    if gain <= 0 or time_constant_s <= 0 or horizon <= 0:
        raise ValueError("planta del acta no sintonizable")
    proportional = time_constant_s / (gain * horizon)
    integral_time = min(time_constant_s, 4.0 * horizon)
    return proportional, proportional / integral_time


@dataclass
class FirstOrderFilter:
    """`control_filter.c`: primer orden discreto, cebado con la primera muestra."""

    time_constant_s: float
    period_s: float
    value: float = 0.0
    primed: bool = False

    @property
    def alpha(self) -> float:
        if self.time_constant_s <= 0:
            return 1.0
        return self.period_s / (self.time_constant_s + self.period_s)

    def reset(self) -> None:
        self.value = 0.0
        self.primed = False

    def push(self, sample: float) -> float:
        if not math.isfinite(sample):
            return sample
        if not self.primed:
            self.value, self.primed = sample, True
            return self.value
        self.value += self.alpha * (sample - self.value)
        return self.value


@dataclass
class PiController:
    """`pi_controller.c`: PI unilateral con anti-*windup* condicional."""

    proportional_gain: float
    integral_gain: float
    period_s: float
    output_maximum: float
    setpoint_rate: float
    output_minimum: float = 0.0
    integral: float = 0.0
    internal_setpoint: float = 0.0
    primed: bool = False

    def reset(self, measurement: float, integral: float = 0.0) -> None:
        """`pi_controller_reset` y, con integral, `pi_controller_reset_with_integral`."""
        self.integral = _clamp(integral, self.output_minimum, self.output_maximum)
        self.internal_setpoint = measurement if math.isfinite(measurement) else 0.0
        self.primed = True

    def _advance_setpoint(self, authorized: float) -> float:
        # Solo frena las subidas: una referencia que baja no puede producir una
        # aplicación instantánea del 100 %, que es lo que el limitador evita.
        if self.setpoint_rate <= 0:
            return authorized
        maximum_step = self.setpoint_rate * self.period_s
        if authorized - self.internal_setpoint > maximum_step:
            return self.internal_setpoint + maximum_step
        return authorized

    def update(self, authorized: float, measurement: float, hold_integral: bool = False) -> float:
        if not (math.isfinite(authorized) and math.isfinite(measurement)):
            return self.output_minimum
        if not self.primed:
            self.reset(measurement)
        self.internal_setpoint = self._advance_setpoint(authorized)
        error = self.internal_setpoint - measurement
        candidate = self.integral + self.integral_gain * error * self.period_s
        integral_term = self.integral if hold_integral else candidate
        unlimited = self.proportional_gain * error + integral_term
        output = _clamp(unlimited, self.output_minimum, self.output_maximum)
        saturated = unlimited != output
        leaving = (unlimited > self.output_maximum and error < 0) or (
            unlimited < self.output_minimum and error > 0
        )
        if not hold_integral and (not saturated or leaving):
            self.integral = candidate
        return output


@dataclass
class Preinfusion:
    """`pressure_preinfusion.c`: bomba al techo hasta que la pastilla cierra."""

    margin_bar: float
    integral_start_percent: float
    regulating: bool = False

    def step(self, setpoint_bar: float, pressure_bar: float) -> tuple[bool, bool, float]:
        """(regula, entrega en este ciclo, integral de arranque), con el techo > 0.

        La réplica solo existe con la bomba autorizada y el techo del acta, así
        que la fase «inactivo» del firmware dura cero ciclos y no se representa.
        """
        hand_over = False
        if not self.regulating and setpoint_bar > 0 and pressure_bar >= setpoint_bar - self.margin_bar:
            self.regulating = hand_over = True
        # Con la presión ya en la referencia al pasar, el PI arranca en cero
        # (corrección del 2026-10-12, noche): precargarla dio un golpe de bomba.
        start = self.integral_start_percent if pressure_bar < setpoint_bar else 0.0
        return self.regulating, hand_over, start


@dataclass
class Feedforward:
    """`heater_feedforward.c`, `ff-v1`: potencia proporcional a la densidad de bomba."""

    gain: float
    rise_limit_c: float
    pumping: bool = False
    start_temperature_c: float = 0.0
    no_water: bool = False

    def step(self, density: int, temperature_c: float) -> float:
        # La réplica siempre tiene la bomba autorizada (empieza en el pulsado).
        if not self.pumping:
            self.pumping = True
            self.no_water = not math.isfinite(temperature_c)
            self.start_temperature_c = temperature_c
        if (
            not math.isfinite(temperature_c)
            or temperature_c > self.start_temperature_c + self.rise_limit_c
        ):
            self.no_water = True
        if self.no_water or density == 0:
            return 0.0
        return self.gain * min(float(density), FULL_SCALE_PERCENT)


#: Margen de redondeo con que `water_reference_boiler_setpoint` lleva al
#: extremo una caldera que cae justo fuera de lo medido (`WATER_REFERENCE_EDGE_C`).
WATER_REFERENCE_EDGE_C = 0.001


def boiler_setpoint_c(
    definition: dict[str, Any], water_c: float, boiler_offset_c: float = 0.0
) -> float:
    """La referencia de caldera que fija la placa para el agua pedida.

    `water_reference_boiler_setpoint` con la recta del acta, y después la subida
    del primer uso, que `supervisor_runtime.c` suma tras convertir. Fuera de lo
    medido la placa rechaza el perfil: aquí se rechaza la réplica, que no tiene
    con qué regular.
    """
    relation = definition["configuration"]["temperature"]["waterReference"]
    boiler = (water_c - relation["offsetC"]) / relation["gain"]
    minimum, maximum = relation["boilerMinimumC"], relation["boilerMaximumC"]
    if not (minimum - WATER_REFERENCE_EDGE_C <= boiler <= maximum + WATER_REFERENCE_EDGE_C):
        raise ValueError(f"agua de {water_c:g} °C fuera de lo que mide el acta para la caldera")
    return _clamp(boiler, minimum, maximum) + boiler_offset_c


def quantize_pump(output_percent: float) -> int:
    """`psm_scheduler_quantize`: redondeo al entero más cercano (`lroundf`)."""
    if not math.isfinite(output_percent) or output_percent <= 0:
        return 0
    if output_percent >= FULL_SCALE_PERCENT:
        return int(FULL_SCALE_PERCENT)
    return int(math.floor(output_percent + 0.5))


def quantize_heater(output_percent: float) -> int:
    """`heater_modulator_quantize`: `(uint8_t)(x + 0.5)`."""
    if not math.isfinite(output_percent) or output_percent <= 0:
        return 0
    if output_percent >= FULL_SCALE_PERCENT:
        return int(FULL_SCALE_PERCENT)
    return int(output_percent + 0.5)


@dataclass
class BoardReplicaController:
    """Los dos lazos de la placa durante una extracción, desde el pulsado.

    `command(temperatura, presión)` recibe la salida de la planta al inicio de
    un paso de comunicación y devuelve las entradas de la FMU v2 para ese paso:
    la demanda media del calentador y la densidad media de la bomba, en 0–1.

    Estado inicial supuesto, porque la preparación no registra el interno de
    la placa:

    - **Lazo térmico en régimen.** La máquina estaba en «lista»: el filtro ya
      sigue a la medida, la referencia interna ya alcanzó la autorizada (el
      limitador solo actúa mientras sube) y la integral es la potencia que
      sostiene el metal medido. Esa potencia se toma de la ganancia estática
      del acta, (T_metal − T_ambiente) / K, porque es la ley con que se sintonizó
      el lazo y la única que la placa conoce; acotada al techo del PI, como la
      acota el firmware.
    - **Lazo de presión recién habilitado.** Antes del pulsado el guardián no
      autoriza la bomba y la compuerta mantiene el PI cebado en la medida con
      la integral en cero; la preinfusión empieza mojando la pastilla. Su filtro
      propio, en cambio, sigue la presión también antes del pulsado
      (`run_pressure` lo alimenta en cada ciclo con medida válida), y por eso
      se ceba con la presión inicial, que en una preparación real es la que
      quedó atrapada de la anterior.
    """

    boiler_setpoint_c: float
    pressure_setpoint_bar: float
    step_seconds: float
    initial_temperature_c: float
    initial_pressure_bar: float
    ambient_c: float
    configuration: dict[str, Any]
    _ticks_per_step: int = field(init=False)
    _tick: int = field(init=False, default=0)
    _heater_demand: int = field(init=False, default=0)

    @classmethod
    def from_definition(
        cls,
        boiler_setpoint_c: float,
        pressure_setpoint_bar: float,
        step_seconds: float,
        initial_temperature_c: float,
        initial_pressure_bar: float,
        definition: dict[str, Any],
        ambient_c: float | None = None,
    ) -> BoardReplicaController:
        configuration = definition["configuration"]
        return cls(
            boiler_setpoint_c,
            pressure_setpoint_bar,
            step_seconds,
            initial_temperature_c,
            initial_pressure_bar,
            configuration["temperature"]["initialState"]["ambientC"]
            if ambient_c is None
            else float(ambient_c),
            configuration,
        )

    def __post_init__(self) -> None:
        ticks = round(self.step_seconds / PRESSURE_PERIOD_S)
        if ticks < 1 or abs(ticks * PRESSURE_PERIOD_S - self.step_seconds) > 1e-9:
            raise ValueError("el paso de comunicación debe ser múltiplo de 20 ms")
        self._ticks_per_step = ticks
        thermal = self.configuration["temperature"]
        pressure = self.configuration["pressure"]
        thermal_period = PRESSURE_PERIOD_S * THERMAL_DECIMATION

        kp, ki = simc_gains(
            thermal["plant"]["gain"],
            thermal["plant"]["timeConstantS"],
            thermal["plant"]["deadTimeS"],
            thermal["tuning"]["lambdaS"],
            thermal_period,
        )
        self.thermal_pi = PiController(
            kp, ki, thermal_period, thermal["tuning"]["outputMaximum"],
            thermal["tuning"]["setpointRate"],
        )
        holding = (self.initial_temperature_c - self.ambient_c) / thermal["plant"]["gain"]
        self.thermal_pi.reset(self.boiler_setpoint_c, holding)
        self.thermal_filter = FirstOrderFilter(
            thermal["tuning"]["filterTimeConstantS"], thermal_period
        )
        self.thermal_filter.push(self.initial_temperature_c)
        self.feedforward = Feedforward(
            thermal["feedforward"]["gain"], thermal["feedforward"]["riseLimitC"]
        )

        kp, ki = simc_gains(
            pressure["plant"]["gain"],
            pressure["plant"]["timeConstantS"],
            pressure["plant"]["deadTimeS"],
            pressure["tuning"]["lambdaS"],
            PRESSURE_PERIOD_S,
        )
        self.pressure_ceiling = pressure["tuning"]["outputMaximum"]
        self.pressure_pi = PiController(
            kp, ki, PRESSURE_PERIOD_S, self.pressure_ceiling, pressure["tuning"]["setpointRate"]
        )
        self.pressure_pi.reset(self.initial_pressure_bar)
        self.pressure_filter = FirstOrderFilter(
            pressure["tuning"]["filterTimeConstantS"], PRESSURE_PERIOD_S
        )
        self.preinfusion_filter = FirstOrderFilter(
            pressure["preinfusion"]["filterTimeConstantS"], PRESSURE_PERIOD_S
        )
        self.preinfusion_filter.push(self.initial_pressure_bar)
        self.preinfusion = Preinfusion(
            pressure["preinfusion"]["marginBar"], pressure["preinfusion"]["integralStartPercent"]
        )
        self._pressure_closing = False

    def _pressure_tick(self, pressure_bar: float) -> int:
        filtered_for_step = self.preinfusion_filter.push(pressure_bar)
        regulate, hand_over, start = self.preinfusion.step(
            self.pressure_setpoint_bar, filtered_for_step
        )
        # La compuerta de `update_gate`: al cambiar de mojando a regulando se
        # reinicia filtro y PI, y la entrega carga la integral de arranque.
        if regulate != self._pressure_closing:
            self._pressure_closing = regulate
            self.pressure_filter.reset()
            self.pressure_pi.reset(pressure_bar)
        if regulate and hand_over:
            self.pressure_pi.reset(pressure_bar, start)
        if regulate:
            output = self.pressure_pi.update(
                self.pressure_setpoint_bar, self.pressure_filter.push(pressure_bar)
            )
        else:
            self.pressure_filter.reset()
            self.pressure_pi.reset(pressure_bar)
            output = self.pressure_ceiling
        return quantize_pump(output)

    def _thermal_tick(self, temperature_c: float, density: int) -> int:
        filtered = self.thermal_filter.push(temperature_c)
        feedforward = self.feedforward.step(density, filtered)
        output = self.thermal_pi.update(self.boiler_setpoint_c, filtered)
        # Puerta 2 (`heater_feedforward_apply`): el PI satura su parte en su
        # techo y la prealimentación se suma después, hasta el 100 %.
        if feedforward > 0:
            output = min(output + feedforward, FULL_SCALE_PERCENT)
        return quantize_heater(output)

    def command(self, temperature_c: float, pressure_bar: float) -> tuple[float, float]:
        densities = []
        heater_ticks = 0
        for _ in range(self._ticks_per_step):
            density = self._pressure_tick(pressure_bar)
            if self._tick % THERMAL_DECIMATION == 0:
                self._heater_demand = self._thermal_tick(temperature_c, density)
            densities.append(density)
            heater_ticks += self._heater_demand
            self._tick += 1
        steps = self._ticks_per_step * FULL_SCALE_PERCENT
        return heater_ticks / steps, sum(densities) / steps
