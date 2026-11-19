within GemeloDigitalPreparacionCafe;
package PruebasAguaRetenida "Pruebas de FmuAdapterV21: compilación y reproducción de la réplica con que se ajustó (FMU 2.1.0)"
  // Dos usos, como PruebasRecalibracion. Las dos «Vigente» comprueban que el adaptador
  // compila y simula con sus valores vigentes en las dos configuraciones del
  // portafiltro, sin archivos externos. ReproduccionReplica lo alimenta con la demanda
  // y la densidad de bomba de una preparación (CSV que escribe
  // models/modelica/agua_retenida/verificar_modelica.py) para comparar su salida con la
  // réplica en Python. No contiene datos medidos ni criterios de aceptación: esos viven
  // en el guion de verificación (Documentacion/agua-retenida-2.1.md).

  model CafeVigente "Café de 27 s con la bomba al 60 % y el calentador al 50 %, desde 92 °C"
    // Entradas arbitrarias del orden de un café de E3: prueba de humo, no reproducción.
    extends Modelica.Icons.Example;
    GemeloDigitalPreparacionCafe.FmuAdapterV21 fmu(T_inicial = 365.15, configuracionPortafiltro = 1);
  equation
    fmu.heaterCommand = if time < 27 then 0.5 else 0.03;
    fmu.pumpCommand = if time < 27 then 0.6 else 0;
    annotation(experiment(StartTime = 0, StopTime = 120, Interval = 0.1, Tolerance = 1e-6));
  end CafeVigente;

  model SinCanastillaVigente "Agua sola sin canastilla, 20 s con la bomba al 75 % y el calentador al 62 %, desde 102 °C"
    // Las entradas de sim-00000342 redondeadas: prueba de humo, no reproducción.
    extends Modelica.Icons.Example;
    GemeloDigitalPreparacionCafe.FmuAdapterV21 fmu(T_inicial = 375.6, configuracionPortafiltro = 2);
  equation
    fmu.heaterCommand = if time < 20 then 0.62 else 0.1;
    fmu.pumpCommand = if time < 20 then 0.75 else 0;
    annotation(experiment(StartTime = 0, StopTime = 120, Interval = 0.1, Tolerance = 1e-6));
  end SinCanastillaVigente;

  model ReproduccionReplica "Adaptador alimentado con la demanda y la densidad de bomba de un CSV"
    // Columnas del CSV: tiempo (s), demanda (0–1) y densidad (0–1). Segmentos
    // constantes: la placa cambia las dos a saltos. Los parámetros del adaptador y de
    // sus piezas los fija quien extiende o modifica `fmu`.
    parameter String archivo = "" "CSV con las entradas";
    GemeloDigitalPreparacionCafe.FmuAdapterV21 fmu;
    Modelica.Blocks.Sources.CombiTimeTable entradas(tableOnFile = true, fileName = archivo, tableName = "tabla",
      delimiter = ",", nHeaderLines = 1, columns = {2, 3},
      smoothness = Modelica.Blocks.Types.Smoothness.ConstantSegments,
      extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
  equation
    fmu.heaterCommand = entradas.y[1];
    fmu.pumpCommand = entradas.y[2];
  end ReproduccionReplica;
end PruebasAguaRetenida;
