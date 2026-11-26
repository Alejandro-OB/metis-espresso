within GemeloDigitalPreparacionCafe;
package PruebasAguaPortafiltro "Pruebas de RecorridoGrupoPortafiltro: compilación, cadena con la caldera y reproducción de la réplica"
  // Dos usos, como PruebasRecalibracion. UsoVigente monta la caldera de la 10.2 y el
  // recorrido y simula un uso de agua sin archivos externos. ReproduccionRecorrido
  // alimenta el recorrido con las entradas que vio la réplica en Python (CSV que escribe
  // model/agua_portafiltro/verificar_modelica.py) para comparar sus salidas.
  // Sin datos medidos ni criterios de aceptación: viven en ese guion y en
  // Documentacion/agua-portafiltro.md.

  model UsoVigente "Uso de agua de 20 s a 3,2 g/s desde el régimen a 92 °C, caldera y recorrido en cadena"
    // Lazo abierto. La demanda de régimen (2,17 %) es la que sostiene 92 °C con los
    // valores vigentes de la caldera; la del uso, 62 %, es la media por ventana de los
    // usos del 18 entre 2 y 19 s. Es una
    // prueba de humo de la cadena, no la reproducción de ningún uso medido.
    extends Modelica.Icons.Example;
    GemeloDigitalPreparacionCafe.CalderaTermicaEstratificada caldera(usarFlujo = true, usarDemanda = true,
      T_inicial = 365.15, demandaInicial = 0.0217, T_aguaEntrada = 300.15);
    GemeloDigitalPreparacionCafe.RecorridoGrupoPortafiltro recorrido(T_NTCInicial = 359.15);
    Real caudal(unit = "kg/s") = if time >= 60 and time < 80 then 0.0032 else 0;
  equation
    caldera.calentadorActivo = false;
    caldera.flujoMasico = caudal;
    caldera.demandaCalentador = if time >= 60 and time < 80 then 0.62 else 0.0217;
    recorrido.temperaturaAguaCaldera = Modelica.Units.Conversions.from_degC(caldera.temperaturaAguaSalidaC);
    recorrido.temperaturaMetal = caldera.temperaturaNucleo;
    recorrido.flujoMasico = caudal;
    annotation(experiment(StartTime = 0, StopTime = 300, Interval = 0.1, Tolerance = 1e-6));
  end UsoVigente;

  model ReproduccionRecorrido "Recorrido alimentado con las entradas de la réplica, columna a columna"
    // Columnas del CSV: tiempo (s), agua de la caldera (K), metal (K), caudal (kg/s) y
    // ambiente (K). Segmentos constantes, como la rejilla de 0,5 s de la réplica.
    parameter String archivo = "" "CSV con las entradas";
    // Las columnas del CSV no llevan unidad; estas constantes la declaran.
    parameter Modelica.Units.SI.Temperature unidadTemperatura = 1 "Las columnas de temperatura están en K";
    parameter Modelica.Units.SI.MassFlowRate unidadCaudal = 1 "La columna de caudal está en kg/s";
    GemeloDigitalPreparacionCafe.RecorridoGrupoPortafiltro recorrido(usarAmbienteExterno = true);
    Modelica.Blocks.Sources.CombiTimeTable entradas(tableOnFile = true, fileName = archivo, tableName = "tabla",
      delimiter = ",", nHeaderLines = 1, columns = {2, 3, 4, 5},
      smoothness = Modelica.Blocks.Types.Smoothness.ConstantSegments,
      extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
  equation
    recorrido.temperaturaAguaCaldera = entradas.y[1]*unidadTemperatura;
    recorrido.temperaturaMetal = entradas.y[2]*unidadTemperatura;
    recorrido.flujoMasico = entradas.y[3]*unidadCaudal;
    recorrido.temperaturaAmbiente = entradas.y[4]*unidadTemperatura;
  end ReproduccionRecorrido;
end PruebasAguaPortafiltro;
