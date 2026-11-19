within GemeloDigitalPreparacionCafe;
model FmuAdapterV2 "Adaptador al contrato FMI v2: caldera 10.2, circuito hidráulico 10.4 y recorrido del agua al café"
  // Paso 10.6 del guion de puesta en servicio: la FMU que usa la plataforma pasa de
  // SistemaTermohidraulico, el modelo de partida sin calibrar, a las tres piezas
  // ajustadas a registros medidos:
  //   CalderaTermicaEstratificada  recalibración 10.2 (Documentacion/recalibracion-10.2.md)
  //   CircuitoHidraulicoMedido     paso 10.4 (Documentacion/modelo-presion-10.4.md)
  //   RecorridoGrupoPortafiltro    2026-10-18 (Documentacion/agua-portafiltro.md)
  //
  // Vive en su propio archivo por la misma razón que esas tres clases: package.mo
  // lleva su huella en model-registry-v1.json, y la versión 1 de la FMU tiene que
  // seguir reproduciéndose mientras el registro la cite.
  //
  // Por qué cambia el contrato y no solo la planta:
  //   - temperatureC es ahora lo que lee el NTC del cuerpo, que es lo que la placa
  //     publica como temperatura medida. En la v1 el canal se declaraba agua, y la
  //     comparación con la telemetría enfrentaba magnitudes distintas.
  //   - waterTemperatureC es nuevo: el agua que cae sobre el café, estimada. En uso
  //     normal nadie la mide (el NTC del portafiltro es de puesta en servicio), así
  //     que la plataforma solo puede mostrarla como estimación, nunca compararla.
  //   - pumpCommand es la densidad PSM de la bomba, 0–1, que es lo que manda la placa
  //     y lo que tabula el circuito de la 10.4. En la v1 era una tensión normalizada
  //     de una bomba cuya curva no salía de ninguna medición.
  //   - heaterCommand es la demanda media del calentador, 0–1, aplicada como media y
  //     no como ventana de 1 s: la caldera se calibró con la demanda media por ventana.
  //
  // Lo que no hace:
  //   - Una sola preparación por simulación. La masa que llenó la canastilla no se
  //     reinicia al parar la bomba (CircuitoHidraulicoMedido no lo modela), así que un
  //     segundo café en la misma corrida arrancaría con la canastilla ya llena.
  //   - No modela el control: el lazo es de quien maneje las entradas.
  //   - No predice el agua fuera de lo que cubre cada pieza; los límites de cada una
  //     están en su documentación.
  import SI = Modelica.Units.SI;
  parameter SI.Voltage tensionRed = 120.7 "Red supuesta si no se da: la de referencia de la bomba (10.4)";
  parameter SI.Temperature T_inicial = 296.83 "Metal, agua y sensor al empezar (parámetro de la caldera)";
  parameter SI.Temperature T_ambiente = 296.83 "Cuarto y agua del depósito";
  parameter Real resistenciaLecho(unit = "bar.s/g") = 0.1503 "Café, ajustada en E3 (10.4)";
  parameter Real volumenLlenado(unit = "g") = 25.99 "Café (10.4)";
  parameter Real anchoLlenado(unit = "g") = 1.946 "Café (10.4)";
  Modelica.Blocks.Interfaces.RealInput heaterCommand(min = 0, max = 1, unit = "1") "Demanda media del calentador";
  Modelica.Blocks.Interfaces.RealInput pumpCommand(min = 0, max = 1, unit = "1") "Densidad PSM de la bomba";
  Modelica.Blocks.Interfaces.RealOutput temperatureC(unit = "degC") "Lo que lee el NTC del cuerpo";
  Modelica.Blocks.Interfaces.RealOutput pressureBar(unit = "bar") "Transductor entre la bomba y la caldera";
  Modelica.Blocks.Interfaces.RealOutput waterTemperatureC(unit = "degC") "Agua sobre el café, estimada";
  Modelica.Blocks.Interfaces.RealOutput cupFlowGs(unit = "g/s") "Caudal hacia la taza";
  CalderaTermicaEstratificada caldera(
    usarDemanda = true,
    usarFlujo = true,
    T_inicial = T_inicial,
    T_ambiente = T_ambiente,
    T_aguaEntrada = T_ambiente);
  CircuitoHidraulicoMedido hidraulica(
    resistenciaLecho = resistenciaLecho,
    volumenLlenado = volumenLlenado,
    anchoLlenado = anchoLlenado);
  RecorridoGrupoPortafiltro recorrido(T_ambiente = T_ambiente);
equation
  // Con usarDemanda la orden booleana no se usa, pero la entrada existe siempre.
  caldera.calentadorActivo = false;
  caldera.demandaCalentador = min(1, max(0, heaterCommand));
  hidraulica.densidad = min(1, max(0, pumpCommand));
  hidraulica.tensionRed = tensionRed;
  // La viscosidad se evalúa con el agua que sale de la caldera, que es un estado, y no
  // con la del recorrido: esa depende del caudal, y el lazo algebraico caudal–
  // viscosidad–temperatura no converge con el resolvedor de paso fijo de la FMU. La
  // diferencia entre ambas es de pocos grados, y el efecto de la viscosidad en el
  // término lineal, del orden del 1 % por grado.
  hidraulica.temperaturaAgua = caldera.temperaturaAguaSalidaC + 273.15;
  // El agua que pasa la restricción es la que sale de la caldera y recorre el grupo;
  // la que llena la canastilla también salió de la caldera.
  caldera.flujoMasico = hidraulica.caudalRestriccion/1000;
  recorrido.flujoMasico = hidraulica.caudalRestriccion/1000;
  recorrido.temperaturaAguaCaldera = caldera.temperaturaAguaSalidaC + 273.15;
  recorrido.temperaturaMetal = caldera.temperaturaNucleo;
  temperatureC = caldera.temperaturaSensorC;
  pressureBar = hidraulica.presionBar;
  waterTemperatureC = recorrido.temperaturaAguaCafeC;
  cupFlowGs = hidraulica.caudalTazaGs;
  annotation(
    experiment(StartTime = 0, StopTime = 800, Interval = 0.1, Tolerance = 1e-6));
end FmuAdapterV2;
