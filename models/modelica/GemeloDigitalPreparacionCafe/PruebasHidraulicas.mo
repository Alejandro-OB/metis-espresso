within GemeloDigitalPreparacionCafe;
package PruebasHidraulicas "Pruebas de CircuitoHidraulicoMedido: compilación y reproducción de corridas (paso 10.4)"
  // Dos usos, como PruebasRecalibracion. CafeVigente comprueba que la clase compila y
  // simula sin archivos externos. ReproduccionCorrida la alimenta con la densidad de
  // bomba, la tensión de red y la temperatura del agua de una corrida medida (CSV que
  // escribe models/modelica/modelo_presion/verificar_modelica.py) para comparar su
  // presión con la réplica en Python con la que se ajustó. Los criterios de aceptación
  // viven en ese guion y en Documentacion/modelo-presion-10.4.md, no aquí.

  model CafeVigente "Preinfusión al 75 % durante 8,5 s y 55 % hasta los 27 s, con café, a 121,5 V y 86 °C"
    // Densidades redondas parecidas a las de un café de E3, no las de ninguno medido:
    // es una prueba de humo, no una reproducción.
    extends Modelica.Icons.Example;
    GemeloDigitalPreparacionCafe.CircuitoHidraulicoMedido circuito(resistenciaLecho = 0.1503,
      volumenLlenado = 25.99, anchoLlenado = 1.946, presionInicialBar = 5.0);
  equation
    circuito.densidad = if time < 8.5 then 0.75 else 0.55;
    circuito.tensionRed = 121.5;
    circuito.temperaturaAgua = 273.15 + 86.15;
    annotation(experiment(StartTime = 0, StopTime = 27, Interval = 0.02, Tolerance = 1e-6));
  end CafeVigente;

  model ReproduccionCorrida "Circuito alimentado con las entradas de una corrida, columna a columna"
    // Columnas del CSV: tiempo (s), densidad (0–1), tensión de red (V) y temperatura del
    // agua (K). Segmentos constantes: la densidad del PSM cambia a saltos.
    parameter String archivo = "" "CSV con las entradas";
    // Las columnas del CSV no llevan unidad; estas constantes la declaran. Sin ellas omc
    // le pone «V» a todo el vector de la tabla por la columna de tensión.
    parameter Modelica.Units.SI.Voltage unidadTension = 1 "La columna de tensión está en V";
    parameter Modelica.Units.SI.Temperature unidadTemperatura = 1 "La columna de temperatura está en K";
    GemeloDigitalPreparacionCafe.CircuitoHidraulicoMedido circuito;
    Modelica.Blocks.Sources.CombiTimeTable entradas(tableOnFile = true, fileName = archivo, tableName = "tabla",
      delimiter = ",", nHeaderLines = 1, columns = {2, 3, 4},
      smoothness = Modelica.Blocks.Types.Smoothness.ConstantSegments,
      extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
  equation
    circuito.densidad = entradas.y[1];
    circuito.tensionRed = entradas.y[2]*unidadTension;
    circuito.temperaturaAgua = entradas.y[3]*unidadTemperatura;
  end ReproduccionCorrida;
end PruebasHidraulicas;
