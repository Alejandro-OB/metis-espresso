model EkoCoffeeMachine
  "Planta reducida para pruebas de integración; parámetros aún no identificados"

  parameter Real ambientTemperatureC(unit = "degC") = 22.0
    "Temperatura ambiente de simulación";
  parameter Real initialTemperatureC(unit = "degC") = 22.0;
  parameter Real heaterRateCPerSecond(unit = "K/s") = 8.0
    "Pendiente ideal con calentador al 100 %";
  parameter Real thermalLossPerSecond(unit = "1/s") = 0.04;
  parameter Real maximumPumpPressureBar(unit = "bar") = 12.0;
  parameter Real pressureTimeConstant(unit = "s") = 0.4;

  input Real heaterCommand(min = 0.0, max = 1.0, unit = "1")
    "Demanda virtual normalizada";
  input Real pumpCommand(min = 0.0, max = 1.0, unit = "1")
    "Demanda virtual normalizada";

  output Real temperatureC(unit = "degC", start = initialTemperatureC, fixed = true);
  output Real pressureBar(unit = "bar", start = 0.0, fixed = true);

protected
  Real pressureTargetBar(unit = "bar");

equation
  pressureTargetBar = maximumPumpPressureBar * pumpCommand;
  der(temperatureC) = heaterRateCPerSecond * heaterCommand
                    - thermalLossPerSecond * (temperatureC - ambientTemperatureC);
  der(pressureBar) = (pressureTargetBar - pressureBar) / pressureTimeConstant;

  annotation(
    experiment(StartTime = 0.0, StopTime = 30.0, Interval = 0.1, Tolerance = 1e-6),
    Documentation(info = "<html><p>Modelo de orden reducido para comprobar el contrato FMI. No representa todavía una caracterización física de la cafetera ni define parámetros de control o seguridad.</p></html>"));
end EkoCoffeeMachine;
