within GemeloDigitalPreparacionCafe;
package PruebasRecalibracion "Pruebas de CalderaTermicaEstratificada: compilación y reproducción de registros (paso 10.2)"
  // Dos usos. EscalonVigente comprueba que la clase compila y simula con sus valores
  // vigentes, sin archivos externos. ReproduccionRegistro la alimenta con las entradas
  // de un registro (demanda, caudal, bomba y ambiente, en un CSV que escribe
  // model/recalibracion_termica/verificar_modelica.py) para comparar su
  // salida con la réplica en Python con la que se ajustó. No contiene datos medidos
  // ni criterios de aceptación: esos viven en el guion de verificación.

  model EscalonVigente "Escalón de 2 % desde el ambiente y enfriamiento libre, con los parámetros vigentes"
    extends Modelica.Icons.Example;
    GemeloDigitalPreparacionCafe.CalderaTermicaEstratificada caldera(usarDemanda = true);
  equation
    caldera.calentadorActivo = false;
    caldera.demandaCalentador = if time < 7200 then 0.02 else 0;
    annotation(experiment(StartTime = 0, StopTime = 10800, Interval = 10, Tolerance = 1e-6));
  end EscalonVigente;

  model CafeVigente "Un café de 27 s a 1,5 g/s desde el régimen a 92 °C, en lazo abierto, con los parámetros vigentes"
    // Sin control: la demanda de régimen (2,17 %) es la que sostiene 92 °C con
    // G_ambiente y P_nominal vigentes, y la del café es un escalón arbitrario. Es una
    // prueba de humo de la clase, no una reproducción de ningún café medido.
    extends Modelica.Icons.Example;
    GemeloDigitalPreparacionCafe.CalderaTermicaEstratificada caldera(usarFlujo = true, usarDemanda = true,
      T_inicial = 365.15, demandaInicial = 0.0217);
  equation
    caldera.calentadorActivo = false;
    caldera.flujoMasico = if time >= 60 and time < 87 then 0.0015 else 0;
    caldera.demandaCalentador = if time >= 60 and time < 87 then 0.3 else 0.0217;
    annotation(experiment(StartTime = 0, StopTime = 900, Interval = 0.5, Tolerance = 1e-6));
  end CafeVigente;

  model ReproduccionRegistro "Caldera alimentada con las entradas de un registro, columna a columna"
    // Columnas del CSV: tiempo (s), demanda (0–1), caudal (kg/s), bomba (0/1) y
    // ambiente (K). Segmentos constantes: la demanda del firmware cambia a saltos.
    parameter String archivo = "" "CSV con las entradas";
    // Las columnas del CSV no llevan unidad; estas constantes la declaran.
    parameter Modelica.Units.SI.MassFlowRate unidadCaudal = 1 "La columna de caudal está en kg/s";
    parameter Modelica.Units.SI.Temperature unidadTemperatura = 1 "La columna de ambiente está en K";
    GemeloDigitalPreparacionCafe.CalderaTermicaEstratificada caldera(usarFlujo = true, usarDemanda = true,
      usarEstadoBomba = true, usarAmbienteExterno = true);
    Modelica.Blocks.Sources.CombiTimeTable entradas(tableOnFile = true, fileName = archivo, tableName = "tabla",
      delimiter = ",", nHeaderLines = 1, columns = {2, 3, 4, 5},
      smoothness = Modelica.Blocks.Types.Smoothness.ConstantSegments,
      extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
  equation
    caldera.calentadorActivo = false;
    caldera.demandaCalentador = entradas.y[1];
    caldera.flujoMasico = entradas.y[2]*unidadCaudal;
    caldera.bombaActiva = entradas.y[3] > 0.5;
    caldera.temperaturaAmbiente = entradas.y[4]*unidadTemperatura;
  end ReproduccionRegistro;
end PruebasRecalibracion;
