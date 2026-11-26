package GemeloDigitalPreparacionCafe
  package Pruebas
    model EnsayoCalentamiento "Ensayo inicial de calentamiento y regulación alrededor de 100 degC"
      parameter Modelica.Units.SI.Temperature T_objetivo = 373.15 "Temperatura de desconexión: 100 degC";
      parameter Modelica.Units.SI.TemperatureDifference histeresis = 2 "Banda de rearme provisional";
      GemeloDigitalPreparacionCafe.CalderaTermica caldera(T_inicial = 293.90, T_ambiente = 294.65, tau_enfriamiento = 2600) "Caldera CM8502";
      discrete Boolean ordenCalentador(start = true, fixed = true) "Orden termostática simplificada";
    equation
      caldera.calentadorActivo = ordenCalentador;
      when caldera.T >= T_objetivo then
        ordenCalentador = false;
      elsewhen caldera.T <= T_objetivo - histeresis then
        ordenCalentador = true;
      end when;
      annotation(
        experiment(StartTime = 0, StopTime = 800, Interval = 1, Tolerance = 1e-6));
    end EnsayoCalentamiento;

    model EnsayoEnfriamiento "Ensayo inicial de enfriamiento natural desde 100 degC"
      GemeloDigitalPreparacionCafe.CalderaTermica caldera(T_inicial = 373.15, T_ambiente = 294.65, tau_enfriamiento = 2600) "Caldera inicialmente a 100 degC";
    equation
      caldera.calentadorActivo = false;
      annotation(
        experiment(StartTime = 0, StopTime = 4500, Interval = 1, Tolerance = 1e-6));
    end EnsayoEnfriamiento;

    model EnsayoCalentamientoYEnfriamiento "Calentamiento desde ambiente hasta 100 degC y enfriamiento natural posterior"
      parameter Modelica.Units.SI.Temperature T_apagado = 373.15 "Temperatura a la que el calentador se apaga permanentemente";
      GemeloDigitalPreparacionCafe.CalderaTermica caldera(T_inicial = 294.90, T_ambiente = 294.65, eta = 0.332, tau_enfriamiento = 3230) "Configuración provisional basada en el ensayo combinado número 2";
      discrete Boolean ordenCalentador(start = true, fixed = true) "Permanece activa hasta alcanzar la temperatura de apagado";
      discrete Modelica.Units.SI.Time tiempoApagado(start = 0, fixed = true) "Instante en que se alcanzan aproximadamente 100 degC";
    equation
      caldera.calentadorActivo = ordenCalentador;
      when caldera.T >= T_apagado then
        ordenCalentador = false;
        tiempoApagado = time;
      end when;
      annotation(
        experiment(StartTime = 0, StopTime = 5200, Interval = 1, Tolerance = 1e-6));
    end EnsayoCalentamientoYEnfriamiento;

    model ValidacionTermicaCSV "Comparación de la simulación térmica con el ensayo combinado número 2"
      extends GemeloDigitalPreparacionCafe.Pruebas.EnsayoCalentamientoYEnfriamiento;
      Modelica.Blocks.Sources.CombiTimeTable datosExperimentales(tableOnFile = true, fileName = Modelica.Utilities.Files.loadResource("modelica://GemeloDigitalPreparacionCafe/Resources/Data/Calentamiento_y_enfriamiento_caldera_2.csv"), delimiter = ",", nHeaderLines = 1, columns = {2}, smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments, extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint, verboseRead = true) "Temperatura medida cada segundo";
      Modelica.Units.SI.TemperatureDifference errorTemperatura "Temperatura simulada menos temperatura experimental";
      Real integralErrorCuadrado(start = 0, fixed = true) "Integral temporal del error al cuadrado";
      Real RMSE(unit = "K") "Raíz del error cuadrático medio acumulado";
      Real integralErrorAbsoluto(start = 0, fixed = true) "Integral temporal del valor absoluto del error";
      Real MAE(unit = "K") "Error absoluto medio acumulado";
    equation
      errorTemperatura = caldera.temperaturaC - datosExperimentales.y[1];
      der(integralErrorCuadrado) = errorTemperatura^2;
      RMSE = sqrt(integralErrorCuadrado/noEvent(max(time, 1e-6)));
      der(integralErrorAbsoluto) = abs(errorTemperatura);
      MAE = integralErrorAbsoluto/noEvent(max(time, 1e-6));
      annotation(
        experiment(StartTime = 0, StopTime = 5073, Interval = 1, Tolerance = 1e-6));
    end ValidacionTermicaCSV;

    model ValidacionTermicaConExtraccion "Validación térmica incluyendo entrada de agua fría durante una extracción"
      parameter Modelica.Units.SI.Temperature T_superior = 373.15 "Límite superior provisional del termostato";
      parameter Modelica.Units.SI.Temperature T_inferior = 371.15 "Límite inferior provisional del termostato";
      parameter Modelica.Units.SI.Time inicioExtraccion = 350 "Inicio aproximado observado";
      parameter Modelica.Units.SI.Time finExtraccion = 366 "Fin aproximado para un ciclo de 16 s";
      parameter Modelica.Units.SI.Time apagadoFinal = 440 "Inicio aproximado del enfriamiento definitivo";
      parameter Modelica.Units.SI.MassFlowRate caudalExtraccion = 0.002 "Estimación inicial: aproximadamente 120 mL/min";
      GemeloDigitalPreparacionCafe.CalderaTermica caldera(usarFlujo = true, T_inicial = 294.90, T_ambiente = 294.65, T_aguaEntrada = 294.65, eta = 0.332, tau_enfriamiento = 3230) "Caldera con término de transporte de energía por flujo";
      Modelica.Blocks.Sources.CombiTimeTable datosExperimentales(tableOnFile = true, fileName = Modelica.Utilities.Files.loadResource("modelica://GemeloDigitalPreparacionCafe/Resources/Data/Calentamiento_y_enfriamiento_caldera_2.csv"), delimiter = ",", nHeaderLines = 1, columns = {2}, smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments, extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint, verboseRead = true) "Temperatura experimental";
      discrete Boolean ordenTermostato(start = true, fixed = true) "Estado del termostato simplificado";
      Boolean ordenCalentador "Orden resultante aplicada al calentador";
      Modelica.Units.SI.MassFlowRate flujoProgramado "Caudal aplicado durante la extracción";
      Modelica.Units.SI.TemperatureDifference errorTemperatura "Temperatura simulada menos experimental";
      Real integralErrorCuadrado(start = 0, fixed = true);
      Real RMSE(unit = "K");
      Real integralErrorAbsoluto(start = 0, fixed = true);
      Real MAE(unit = "K");
    equation
      when caldera.T >= T_superior then
        ordenTermostato = false;
      elsewhen caldera.T <= T_inferior then
        ordenTermostato = true;
      end when;
      ordenCalentador = if time >= apagadoFinal then false elseif time >= inicioExtraccion and time < finExtraccion then false else ordenTermostato;
      flujoProgramado = if time >= inicioExtraccion and time < finExtraccion then caudalExtraccion else 0;
      caldera.calentadorActivo = ordenCalentador;
      caldera.flujoMasico = flujoProgramado;
      errorTemperatura = caldera.temperaturaC - datosExperimentales.y[1];
      der(integralErrorCuadrado) = errorTemperatura^2;
      RMSE = sqrt(integralErrorCuadrado/noEvent(max(time, 1e-6)));
      der(integralErrorAbsoluto) = abs(errorTemperatura);
      MAE = integralErrorAbsoluto/noEvent(max(time, 1e-6));
      annotation(
        experiment(StartTime = 0, StopTime = 5073, Interval = 1, Tolerance = 1e-6));
    end ValidacionTermicaConExtraccion;

    model ValidacionDosNodosCSV "Validación completa de calentamiento, extracción y enfriamiento"
      parameter Modelica.Units.SI.Temperature T_superior = 367.52756876 "94.3776 degC, umbral calibrado en la ubicación del sensor";
      parameter Modelica.Units.SI.TemperatureDifference histeresis = 2.117887927;
      final parameter Modelica.Units.SI.Temperature T_inferior = T_superior - histeresis;
      parameter Modelica.Units.SI.Time inicioExtraccion = 350;
      parameter Modelica.Units.SI.Time finExtraccion = 366;
      parameter Modelica.Units.SI.Time apagadoFinal = 440;
      parameter Modelica.Units.SI.MassFlowRate caudalExtraccion = 0.001665994722 "Estimación calibrada, aproximadamente 100 mL/min";
      GemeloDigitalPreparacionCafe.CalderaTermicaDosNodos caldera(usarFlujo = true);
      Modelica.Blocks.Sources.CombiTimeTable datosExperimentales(tableOnFile = true, fileName = Modelica.Utilities.Files.loadResource("modelica://GemeloDigitalPreparacionCafe/Resources/Data/Calentamiento_y_enfriamiento_caldera_2.csv"), delimiter = ",", nHeaderLines = 1, columns = {2}, smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments, extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint, verboseRead = true);
      discrete Boolean ordenTermostato(start = true, fixed = true);
      Boolean ordenCalentador;
      Modelica.Units.SI.MassFlowRate flujoProgramado;
      Modelica.Units.SI.TemperatureDifference errorTemperatura;
      Real integralErrorCuadrado(start = 0, fixed = true);
      Real RMSE(unit = "K");
      Real integralErrorAbsoluto(start = 0, fixed = true);
      Real MAE(unit = "K");
    equation
      when caldera.T_sensor >= T_superior then
        ordenTermostato = false;
      elsewhen caldera.T_sensor <= T_inferior then
        ordenTermostato = true;
      end when;
      ordenCalentador = if time >= apagadoFinal then false elseif time >= inicioExtraccion and time < finExtraccion then false else ordenTermostato;
      flujoProgramado = if time >= inicioExtraccion and time < finExtraccion then caudalExtraccion else 0;
      caldera.calentadorActivo = ordenCalentador;
      caldera.flujoMasico = flujoProgramado;
      errorTemperatura = caldera.temperaturaSensorC - datosExperimentales.y[1];
      der(integralErrorCuadrado) = errorTemperatura^2;
      RMSE = sqrt(integralErrorCuadrado/noEvent(max(time, 1e-6)));
      der(integralErrorAbsoluto) = abs(errorTemperatura);
      MAE = integralErrorAbsoluto/noEvent(max(time, 1e-6));
      annotation(
        experiment(StartTime = 0, StopTime = 5073, Interval = 1, Tolerance = 1e-6));
    end ValidacionDosNodosCSV;

    model CicloHidraulicoEspresso "Preinfusión, pausa y extracción con repetición del ciclo"
      parameter Modelica.Units.SI.Time periodo = 20;
      parameter Modelica.Units.SI.Time finPreinfusion = 4;
      parameter Modelica.Units.SI.Time inicioExtraccion = 5;
      parameter Modelica.Units.SI.Time finExtraccion = 16;
      parameter Modelica.Units.SI.Voltage tensionPreinfusion = 110;
      parameter Modelica.Units.SI.Voltage tensionExtraccion = 120;
      parameter Modelica.Units.SI.Density densidadAgua = 997;
      parameter Real resistenciaCafe(unit = "Pa.s/kg") = 9e5/0.001666 "Carga provisional; no corresponde todavía a una medición del café";
      GemeloDigitalPreparacionCafe.BombaVibratoria bomba;
      GemeloDigitalPreparacionCafe.TramoTuberia tuberia;
      Real fase(unit = "s");
      Boolean preinfusion;
      Boolean extraccion;
      Modelica.Units.SI.Pressure presionCafe;
      Real presionCafe_bar;
      Real caudal_mL_min;
      Modelica.Units.SI.Volume volumenAcumulado(start = 0, fixed = true);
    equation
      fase = mod(time, periodo);
      preinfusion = fase < finPreinfusion;
      extraccion = fase >= inicioExtraccion and fase < finExtraccion;
      bomba.activa = preinfusion or extraccion;
      bomba.tension = if preinfusion then tensionPreinfusion elseif extraccion then tensionExtraccion else 0;
      bomba.resistenciaCarga = resistenciaCafe + tuberia.resistenciaHidraulica;
      tuberia.caudalMasico = bomba.caudalMasico;
      presionCafe = resistenciaCafe*bomba.caudalMasico;
      presionCafe_bar = presionCafe/1e5;
      caudal_mL_min = bomba.caudalMasico/densidadAgua*60*1e6;
      der(volumenAcumulado) = bomba.caudalMasico/densidadAgua;
      annotation(
        experiment(StartTime = 0, StopTime = 40, Interval = 0.02, Tolerance = 1e-6));
    end CicloHidraulicoEspresso;

    model ValidacionTermohidraulicaIntegrada "Acoplamiento de bomba, tubería y caldera validado con el ensayo completo"
      parameter Modelica.Units.SI.Temperature T_superior = 367.52756876;
      parameter Modelica.Units.SI.TemperatureDifference histeresis = 2.117887927;
      final parameter Modelica.Units.SI.Temperature T_inferior = T_superior - histeresis;
      parameter Modelica.Units.SI.Time inicioPreinfusion = 350;
      parameter Modelica.Units.SI.Time finPreinfusion = 354;
      parameter Modelica.Units.SI.Time inicioExtraccion = 355;
      parameter Modelica.Units.SI.Time finExtraccion = 366;
      parameter Modelica.Units.SI.Time apagadoFinal = 440;
      parameter Modelica.Units.SI.Voltage tensionPreinfusion = 110;
      parameter Modelica.Units.SI.Voltage tensionExtraccion = 120;
      parameter Modelica.Units.SI.MassFlowRate caudalReferenciaIntegrado = 0.001817 "Parámetro calibrado térmicamente; pendiente de medición directa";
      parameter Real resistenciaCafe(unit = "Pa.s/kg") = 9e5/caudalReferenciaIntegrado "Carga equivalente provisional del café y portafiltro";
      parameter Modelica.Units.SI.Density densidadAgua = 997;
      GemeloDigitalPreparacionCafe.CalderaTermicaDosNodos caldera(usarFlujo = true);
      GemeloDigitalPreparacionCafe.BombaVibratoria bomba(caudalReferencia = caudalReferenciaIntegrado);
      GemeloDigitalPreparacionCafe.TramoTuberia tuberia;
      Modelica.Blocks.Sources.CombiTimeTable datosExperimentales(tableOnFile = true, fileName = Modelica.Utilities.Files.loadResource("modelica://GemeloDigitalPreparacionCafe/Resources/Data/Calentamiento_y_enfriamiento_caldera_2.csv"), delimiter = ",", nHeaderLines = 1, columns = {2}, smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments, extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint, verboseRead = true);
      discrete Boolean ordenTermostato(start = true, fixed = true);
      Boolean preinfusion;
      Boolean extraccion;
      Boolean cicloHidraulico;
      Boolean ordenCalentador;
      Modelica.Units.SI.Pressure presionCafe;
      Real presionCafe_bar;
      Real caudal_mL_min;
      Modelica.Units.SI.Volume volumenExtraido(start = 0, fixed = true);
      Real volumenExtraido_mL;
      Modelica.Units.SI.TemperatureDifference errorTemperatura;
      Real integralErrorCuadrado(start = 0, fixed = true);
      Real integralErrorAbsoluto(start = 0, fixed = true);
      Real RMSE(unit = "K");
      Real MAE(unit = "K");
    equation
      preinfusion = time >= inicioPreinfusion and time < finPreinfusion;
      extraccion = time >= inicioExtraccion and time < finExtraccion;
      cicloHidraulico = time >= inicioPreinfusion and time < finExtraccion;
      when caldera.T_sensor >= T_superior then
        ordenTermostato = false;
      elsewhen caldera.T_sensor <= T_inferior then
        ordenTermostato = true;
      end when;
      ordenCalentador = if time >= apagadoFinal then false elseif cicloHidraulico then false else ordenTermostato;
      caldera.calentadorActivo = ordenCalentador;
      bomba.activa = preinfusion or extraccion;
      bomba.tension = if preinfusion then tensionPreinfusion elseif extraccion then tensionExtraccion else 0;
      bomba.resistenciaCarga = resistenciaCafe + tuberia.resistenciaHidraulica;
      tuberia.caudalMasico = bomba.caudalMasico;
      caldera.flujoMasico = bomba.caudalMasico;
      presionCafe = resistenciaCafe*bomba.caudalMasico;
      presionCafe_bar = presionCafe/1e5;
      caudal_mL_min = bomba.caudalMasico/densidadAgua*60*1e6;
      der(volumenExtraido) = bomba.caudalMasico/densidadAgua;
      volumenExtraido_mL = volumenExtraido*1e6;
      errorTemperatura = caldera.temperaturaSensorC - datosExperimentales.y[1];
      der(integralErrorCuadrado) = errorTemperatura^2;
      der(integralErrorAbsoluto) = abs(errorTemperatura);
      RMSE = sqrt(integralErrorCuadrado/noEvent(max(time, 1e-6)));
      MAE = integralErrorAbsoluto/noEvent(max(time, 1e-6));
      annotation(
        experiment(StartTime = 0, StopTime = 5073, Interval = 1, Tolerance = 1e-6));
    end ValidacionTermohidraulicaIntegrada;

    model ValidacionArquitecturaCompletaCSV "Validación de la planta modular, su control secuencial y la interfaz de sensores"
      parameter Modelica.Units.SI.Temperature T_superior = 367.52756876;
      parameter Modelica.Units.SI.TemperatureDifference histeresis = 2.117887927;
      final parameter Modelica.Units.SI.Temperature T_inferior = T_superior - histeresis;
      parameter Modelica.Units.SI.Time apagadoFinal = 440;
      parameter Modelica.Units.SI.Density densidadAgua = 997;
      GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
      GemeloDigitalPreparacionCafe.ControlSecuenciaEspresso secuencia(tiempoInicio = 350, duracionPreinfusion = 4, duracionPausa = 1, duracionTotal = 16);
      Modelica.Blocks.Sources.CombiTimeTable datosExperimentales(tableOnFile = true, fileName = Modelica.Utilities.Files.loadResource("modelica://GemeloDigitalPreparacionCafe/Resources/Data/Calentamiento_y_enfriamiento_caldera_2.csv"), delimiter = ",", nHeaderLines = 1, columns = {2}, smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments, extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint, verboseRead = true);
      discrete Boolean ordenTermostato(start = true, fixed = true);
      Boolean ordenCalentador;
      Modelica.Units.SI.Volume volumenExtraido(start = 0, fixed = true);
      Real volumenExtraido_mL;
      Modelica.Units.SI.TemperatureDifference errorTemperatura;
      Real integralErrorCuadrado(start = 0, fixed = true);
      Real integralErrorAbsoluto(start = 0, fixed = true);
      Real RMSE(unit = "K");
      Real MAE(unit = "K");
    equation
      when planta.caldera.T_sensor >= T_superior then
        ordenTermostato = false;
      elsewhen planta.caldera.T_sensor <= T_inferior then
        ordenTermostato = true;
      end when;
      ordenCalentador = if time >= apagadoFinal then false elseif secuencia.cicloActivo then false else ordenTermostato;
      planta.ordenCalentador = ordenCalentador;
      planta.ordenBomba = secuencia.bombaActiva;
      planta.tensionBomba = secuencia.tensionBomba;
      der(volumenExtraido) = planta.caudalMasico/densidadAgua;
      volumenExtraido_mL = volumenExtraido*1e6;
      errorTemperatura = planta.temperaturaC - datosExperimentales.y[1];
      der(integralErrorCuadrado) = errorTemperatura^2;
      der(integralErrorAbsoluto) = abs(errorTemperatura);
      RMSE = sqrt(integralErrorCuadrado/noEvent(max(time, 1e-6)));
      MAE = integralErrorAbsoluto/noEvent(max(time, 1e-6));
      annotation(
        experiment(StartTime = 0, StopTime = 5073, Interval = 1, Tolerance = 1e-6));
    end ValidacionArquitecturaCompletaCSV;

    model ValidacionTresExtraccionesCSV "Reproducción del conjunto sintético de tres extracciones; no representa evidencia experimental"
      parameter String archivoDatos = Modelica.Utilities.Files.loadResource("modelica://GemeloDigitalPreparacionCafe/Resources/Data/datos_sinteticos_3_extracciones.csv");
      final parameter Boolean datosSonSinteticos = true;
      GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
      Modelica.Blocks.Sources.CombiTimeTable datosTemperatura(
        tableOnFile = true,
        fileName = archivoDatos,
        delimiter = ",",
        nHeaderLines = 1,
        columns = {2},
        smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments,
        extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
      Modelica.Blocks.Sources.CombiTimeTable datosPresion(
        tableOnFile = true,
        fileName = archivoDatos,
        delimiter = ",",
        nHeaderLines = 1,
        columns = {3},
        smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments,
        extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
      Modelica.Blocks.Sources.CombiTimeTable datosTension(
        tableOnFile = true,
        fileName = archivoDatos,
        delimiter = ",",
        nHeaderLines = 1,
        columns = {5},
        smoothness = Modelica.Blocks.Types.Smoothness.LinearSegments,
        extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
      Modelica.Blocks.Sources.CombiTimeTable datosDiscretos(
        tableOnFile = true,
        fileName = archivoDatos,
        delimiter = ",",
        nHeaderLines = 1,
        columns = {4, 6, 7, 8},
        smoothness = Modelica.Blocks.Types.Smoothness.ConstantSegments,
        extrapolation = Modelica.Blocks.Types.Extrapolation.HoldLastPoint);
      Real temperaturaExperimentalC(unit = "degC");
      Real presionExperimentalBar(unit = "bar");
      Boolean bombaExperimentalActiva;
      Boolean calentadorExperimentalActivo;
      Integer extraccionId;
      Integer faseCodigo;
      Modelica.Units.SI.TemperatureDifference errorTemperatura;
      Real errorPresion(unit = "bar");
      Real integralErrorTemperaturaCuadrado(start = 0, fixed = true);
      Real integralErrorPresionCuadrado(start = 0, fixed = true);
      Modelica.Units.SI.Time tiempoPresionActiva(start = 0, fixed = true);
      Real RMSETemperatura(unit = "K");
      Real RMSEPresion(unit = "bar");
    equation
      temperaturaExperimentalC = datosTemperatura.y[1];
      presionExperimentalBar = datosPresion.y[1];
      bombaExperimentalActiva = datosDiscretos.y[1] >= 0.5;
      calentadorExperimentalActivo = datosDiscretos.y[2] >= 0.5;
      extraccionId = integer(datosDiscretos.y[3]);
      faseCodigo = integer(datosDiscretos.y[4]);
      planta.ordenCalentador = calentadorExperimentalActivo;
      planta.ordenBomba = bombaExperimentalActiva;
      planta.tensionBomba = if bombaExperimentalActiva then datosTension.y[1] else 0;
      errorTemperatura = planta.temperaturaC - temperaturaExperimentalC;
      errorPresion = planta.presionBar - presionExperimentalBar;
      der(integralErrorTemperaturaCuadrado) = errorTemperatura^2;
      der(integralErrorPresionCuadrado) = if bombaExperimentalActiva then errorPresion^2 else 0;
      der(tiempoPresionActiva) = if bombaExperimentalActiva then 1 else 0;
      RMSETemperatura = sqrt(integralErrorTemperaturaCuadrado/noEvent(max(time, 1e-6)));
      RMSEPresion = sqrt(integralErrorPresionCuadrado/noEvent(max(tiempoPresionActiva, 1e-6)));
      annotation(
        experiment(StartTime = 0, StopTime = 1400, Interval = 1, Tolerance = 1e-6));
    end ValidacionTresExtraccionesCSV;

    model EnsayoControlPI "Primera prueba virtual del control PI con PWM"
      parameter Modelica.Units.SI.Temperature temperaturaObjetivoC = 100;
      parameter Modelica.Units.SI.Time periodoPWM = 1;
      GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
      GemeloDigitalPreparacionCafe.ControlTemperaturaPI controlador(Kp = 0.021, Ti = 400);
      Boolean calentadorPWM;
      Real integralErrorAbsoluto(start = 0, fixed = true);
      discrete Real temperaturaMaximaC(start = 21.75, fixed = true);
    equation
      controlador.referenciaC = temperaturaObjetivoC;
      controlador.medidaC = planta.temperaturaC;
      controlador.extraccionActiva = false;
      calentadorPWM = mod(time, periodoPWM) < controlador.accion*periodoPWM;
      planta.ordenCalentador = calentadorPWM;
      planta.ordenBomba = false;
      planta.tensionBomba = 0;
      der(integralErrorAbsoluto) = abs(controlador.error);
      when sample(0, 1) then
        temperaturaMaximaC = max(pre(temperaturaMaximaC), planta.temperaturaC);
      end when;
      annotation(
        experiment(StartTime = 0, StopTime = 800, Interval = 0.2, Tolerance = 1e-6));
    end EnsayoControlPI;

    partial model EscenarioComparacionControlTermico "Escenario común para comparar estrategias térmicas sin conectarlas a hardware"
      parameter Real temperaturaObjetivoC(unit = "degC") = 93 "Referencia de café usada solamente en simulación";
      parameter Modelica.Units.SI.Time inicioEvaluacion = 350 "Excluye el arranque frío de las métricas de regulación";
      parameter Modelica.Units.SI.Time periodoPWM = 1 "Ventana de modulación del calentador virtual";
      parameter Modelica.Units.SI.TemperatureDifference bandaEvaluacion = 1 "Banda usada para contabilizar tiempo fuera de objetivo";
      parameter Modelica.Units.SI.Time duracionVentanaRecuperacion = 60 "Tiempo posterior al ciclo incluido al medir la caída térmica retardada";
      replaceable GemeloDigitalPreparacionCafe.ControlTemperaturaHisteresis controlador constrainedby GemeloDigitalPreparacionCafe.InterfazControlTermicoNormalizado;
      GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
      GemeloDigitalPreparacionCafe.ControlSecuenciaEspresso secuencia(tiempoInicio = 450, duracionPreinfusion = 4, duracionPausa = 1, duracionTotal = 16);
      Boolean calentadorPWM;
      Modelica.Units.SI.TemperatureDifference errorSeguimiento "Temperatura simulada menos referencia";
      Real integralErrorCuadrado(start = 0, fixed = true);
      Real integralErrorAbsoluto(start = 0, fixed = true);
      Real RMSE(unit = "K");
      Real MAE(unit = "K");
      Modelica.Units.SI.Time tiempoFueraBanda(start = 0, fixed = true);
      Real fraccionTiempoFueraBanda "Fracción acotada numéricamente entre cero y uno";
      Modelica.Units.SI.Time tiempoCalentadorEncendido(start = 0, fixed = true) "Tiempo equivalente real de energización dentro de la evaluación";
      discrete Real temperaturaMaximaC(unit = "degC", start = 21.75, fixed = true);
      discrete Real temperaturaMinimaExtraccionC(unit = "degC", start = 200, fixed = true) "Mínimo desde el inicio del ciclo hasta terminar la ventana de recuperación";
      Real sobreimpulsoC(unit = "K");
      Real caidaMaximaExtraccionC(unit = "K");
    equation
      controlador.referenciaC = temperaturaObjetivoC;
      controlador.medidaC = planta.temperaturaC;
      controlador.extraccionActiva = secuencia.bombaActiva;
      calentadorPWM = mod(time, periodoPWM) < controlador.accion*periodoPWM;
      planta.ordenCalentador = calentadorPWM;
      planta.ordenBomba = secuencia.bombaActiva;
      planta.tensionBomba = secuencia.tensionBomba;
      errorSeguimiento = planta.temperaturaC - temperaturaObjetivoC;
      der(integralErrorCuadrado) = if time >= inicioEvaluacion then errorSeguimiento^2 else 0;
      der(integralErrorAbsoluto) = if time >= inicioEvaluacion then abs(errorSeguimiento) else 0;
      der(tiempoFueraBanda) = if time >= inicioEvaluacion and abs(errorSeguimiento) > bandaEvaluacion then 1 else 0;
      der(tiempoCalentadorEncendido) = if time >= inicioEvaluacion and calentadorPWM then 1 else 0;
      RMSE = sqrt(integralErrorCuadrado/noEvent(max(time - inicioEvaluacion, 1e-6)));
      MAE = integralErrorAbsoluto/noEvent(max(time - inicioEvaluacion, 1e-6));
      fraccionTiempoFueraBanda = noEvent(min(1, max(0, tiempoFueraBanda/max(time - inicioEvaluacion, 1e-6))));
      sobreimpulsoC = noEvent(max(0, temperaturaMaximaC - temperaturaObjetivoC));
      caidaMaximaExtraccionC = noEvent(max(0, temperaturaObjetivoC - temperaturaMinimaExtraccionC));
      when sample(0, 0.2) then
        temperaturaMaximaC = if time >= inicioEvaluacion then max(pre(temperaturaMaximaC), planta.temperaturaC) else pre(temperaturaMaximaC);
        temperaturaMinimaExtraccionC = if time >= secuencia.tiempoInicio and time <= secuencia.finCiclo + duracionVentanaRecuperacion then min(pre(temperaturaMinimaExtraccionC), planta.temperaturaC) else pre(temperaturaMinimaExtraccionC);
      end when;
      annotation(
        experiment(StartTime = 0, StopTime = 800, Interval = 0.2, Tolerance = 1e-6));
    end EscenarioComparacionControlTermico;

    model ComparacionControlHisteresis "Referencia vigente del simulador de firmware: control térmico por histéresis"
      extends GemeloDigitalPreparacionCafe.Pruebas.EscenarioComparacionControlTermico(
        redeclare GemeloDigitalPreparacionCafe.ControlTemperaturaHisteresis controlador(semibanda = 0.5));
    end ComparacionControlHisteresis;

    model ComparacionControlEscalonado "Candidato por regiones inspirado en Gaggiuino, sin utilizar flujo estimado"
      extends GemeloDigitalPreparacionCafe.Pruebas.EscenarioComparacionControlTermico(
        redeclare GemeloDigitalPreparacionCafe.ControlTemperaturaEscalonado controlador(
          bandaPotenciaTotal = 8,
          bandaPotenciaMedia = 2,
          accionMedia = 0.15,
          accionCercana = 0.04,
          refuerzoExtraccion = 0.8));
    end ComparacionControlEscalonado;

    model ComparacionControlPI "Candidato PI estándar con anti-windup y compensación por estado de bomba"
      extends GemeloDigitalPreparacionCafe.Pruebas.EscenarioComparacionControlTermico(
        redeclare GemeloDigitalPreparacionCafe.ControlTemperaturaPI controlador(
          Kp = 0.021,
          Ti = 400,
          Ni = 0.1,
          compensacionExtraccion = 0.7));
    end ComparacionControlPI;

    model EscenarioFalloSensoresSIL "Fallos de sensores, alarma enclavada y recuperación segura sin reinicio automático"
      parameter Real temperaturaObjetivoC(unit = "degC") = 93;
      parameter Modelica.Units.SI.Time periodoPWM = 1;
      GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
      GemeloDigitalPreparacionCafe.SensorTemperaturaSIL sensorTemperatura(
        retardo = 0.4,
        amplitudRuido = 0.15);
      GemeloDigitalPreparacionCafe.SensorPresionSIL sensorPresion(
        retardo = 0.05,
        amplitudRuido = 0.03,
        frecuenciaRuido = 3,
        inicioFallo = 400,
        finFallo = 402);
      GemeloDigitalPreparacionCafe.SupervisorSeguridadSensoresSIL supervisor(
        periodoMuestreo = 0.1,
        tiempoMaximoSinMuestra = 0.5,
        tiempoMaximoSinCambio = 1);
      GemeloDigitalPreparacionCafe.ControlTemperaturaEscalonado controlador;
      Real temperaturaSupervisadaC(unit = "degC");
      Real presionSupervisadaBar(unit = "bar");
      Boolean congelamientoTemperatura;
      Boolean perdidaMuestrasPresion;
      Boolean temperaturaFueraRango;
      Boolean paradaOperador;
      Boolean reconocimiento;
      Boolean habilitarOperacion;
      Boolean demandaBombaProgramada;
      Boolean solicitudCalentador;
      Boolean solicitudBomba;
      Boolean calentadorAutorizado;
      Boolean bombaAutorizada;
    equation
      sensorTemperatura.valorRealC = planta.temperaturaC;
      sensorPresion.valorRealBar = planta.presionBar;
      congelamientoTemperatura = time >= 100 and time < 104;
      perdidaMuestrasPresion = time >= 200 and time < 202;
      temperaturaFueraRango = time >= 300 and time < 302;
      temperaturaSupervisadaC = if congelamientoTemperatura then 50 elseif temperaturaFueraRango then 200 else sensorTemperatura.medidaC;
      presionSupervisadaBar = sensorPresion.medidaBar;
      paradaOperador = (time >= 105 and time < 110) or (time >= 203 and time < 208) or (time >= 303 and time < 308) or (time >= 403 and time < 408);
      reconocimiento = (time >= 102 and time < 102.2) or (time >= 104.2 and time < 104.4) or (time >= 106 and time < 106.2)
        or (time >= 201 and time < 201.2) or (time >= 202.2 and time < 202.4) or (time >= 204 and time < 204.2)
        or (time >= 301 and time < 301.2) or (time >= 302.2 and time < 302.4) or (time >= 304 and time < 304.2)
        or (time >= 401 and time < 401.2) or (time >= 402.2 and time < 402.4) or (time >= 404 and time < 404.2);
      habilitarOperacion = (time >= 108 and time < 108.2) or (time >= 206 and time < 206.2)
        or (time >= 306 and time < 306.2) or (time >= 406 and time < 406.2);
      demandaBombaProgramada = (time >= 95 and time < 110) or (time >= 195 and time < 210)
        or (time >= 295 and time < 310) or (time >= 395 and time < 410);
      controlador.referenciaC = temperaturaObjetivoC;
      controlador.medidaC = temperaturaSupervisadaC;
      controlador.extraccionActiva = solicitudBomba;
      solicitudCalentador = not paradaOperador and mod(time, periodoPWM) < controlador.accion*periodoPWM;
      solicitudBomba = not paradaOperador and demandaBombaProgramada;
      supervisor.temperaturaC = temperaturaSupervisadaC;
      supervisor.presionBar = presionSupervisadaBar;
      supervisor.reporteTemperaturaValida = sensorTemperatura.valida;
      supervisor.reportePresionValida = sensorPresion.valida;
      supervisor.nuevaMuestraTemperatura = sensorTemperatura.valida;
      supervisor.nuevaMuestraPresion = sensorPresion.valida and not perdidaMuestrasPresion;
      supervisor.esperarCambioTemperatura = solicitudCalentador;
      supervisor.esperarCambioPresion = solicitudBomba;
      supervisor.reconocimiento = reconocimiento;
      supervisor.condicionesSeguras = paradaOperador and not solicitudCalentador and not solicitudBomba;
      supervisor.habilitarOperacion = habilitarOperacion;
      calentadorAutorizado = solicitudCalentador and supervisor.autorizarActuadores;
      bombaAutorizada = solicitudBomba and supervisor.autorizarActuadores;
      planta.ordenCalentador = calentadorAutorizado;
      planta.ordenBomba = bombaAutorizada;
      planta.tensionBomba = if bombaAutorizada then 120 else 0;
      assert(supervisor.estado <> GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.alarma or not calentadorAutorizado and not bombaAutorizada, "La alarma debe inhibir ambos actuadores");
      assert(not supervisor.falloTemperatura or not calentadorAutorizado, "El calentador debe quedar inhibido ante un fallo térmico");
      assert(not supervisor.falloPresion or not bombaAutorizada, "La bomba debe quedar inhibida ante un fallo de presión");
      assert(not (time >= 101.2 and time < 104) or supervisor.temperaturaCongelada, "No se detectó el sensor térmico congelado");
      assert(not (time >= 200.6 and time < 202) or supervisor.presionObsoleta, "No se detectó la muestra de presión obsoleta");
      assert(not temperaturaFueraRango or not supervisor.temperaturaDentroRango, "No se detectó la temperatura fuera de rango");
      assert(sensorPresion.valida or supervisor.falloPresion, "No se propagó el fallo explícito del sensor de presión");
      assert(not ((time >= 104 and time < 106) or (time >= 202 and time < 204) or (time >= 302 and time < 304) or (time >= 402 and time < 404)) or supervisor.estado == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.alarma, "La alarma se liberó sin reconocimiento válido y condiciones seguras");
      assert(not ((time >= 106.2 and time < 108) or (time >= 204.2 and time < 206) or (time >= 304.2 and time < 306) or (time >= 404.2 and time < 406)) or supervisor.estado == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.seguroInactivo, "La recuperación no terminó en estado seguro inactivo");
      assert(not ((time >= 108.2 and time < 110) or (time >= 206.2 and time < 208) or (time >= 306.2 and time < 308) or (time >= 406.2 and time < 408)) or supervisor.estado == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.operativo, "La operación no requirió una habilitación independiente");
      annotation(
        experiment(StartTime = 0, StopTime = 450, Interval = 0.1, Tolerance = 1e-6));
    end EscenarioFalloSensoresSIL;

    model EscenarioModosOperativosSIL "Cobertura preliminar de café, extracción, vapor, purga y enfriamiento"
      parameter Modelica.Units.SI.Time periodoPWM = 1;
      GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
      GemeloDigitalPreparacionCafe.ControlTemperaturaEscalonado controlador;
      GemeloDigitalPreparacionCafe.ModoOperacionSIL modo;
      GemeloDigitalPreparacionCafe.ModeloVaporPurgaEnfriamiento vapor(
        masaAguaInicial=0.12);
      Real referenciaC(unit = "degC");
      Boolean calentadorPWM;
      Boolean bombaSolicitada;
    equation
      modo = if time < 400 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.calentamientoCafe
        elseif time < 450 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.preparado
        elseif time < 466 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.extraccion
        elseif time < 520 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.preparado
        elseif time < 700 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.calentamientoVapor
        elseif time < 750 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.vapor
        elseif time < 760 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.purga
        else GemeloDigitalPreparacionCafe.ModoOperacionSIL.enfriamiento;
      referenciaC = if modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.calentamientoVapor or modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.vapor then 125 else 93;
      bombaSolicitada = modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.extraccion or modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.purga;
      controlador.referenciaC = referenciaC;
      controlador.medidaC = planta.temperaturaC;
      controlador.extraccionActiva = bombaSolicitada;
      calentadorPWM = mod(time, periodoPWM) < controlador.accion*periodoPWM;
      planta.ordenCalentador = calentadorPWM and modo <> GemeloDigitalPreparacionCafe.ModoOperacionSIL.purga and modo <> GemeloDigitalPreparacionCafe.ModoOperacionSIL.enfriamiento;
      planta.ordenBomba = bombaSolicitada;
      planta.tensionBomba = if bombaSolicitada then 120 else 0;
      vapor.calentadorActivo = modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.calentamientoVapor or
        modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.vapor;
      vapor.purgaActiva = modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.purga;
      vapor.caudalEvaporacion = if vapor.calentadorActivo and modo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.vapor then 1e-5 else 0;
      vapor.caudalDescarga = if vapor.purgaActiva then 1e-5 else 0;
      assert(not (time >= 750.1 and time < 760) or vapor.masaDescargada > 0,
        "El modo purga no descargó vapor en la integración operacional");
      annotation(
        experiment(StartTime = 0, StopTime = 1000, Interval = 0.2, Tolerance = 1e-6));
    end EscenarioModosOperativosSIL;

    model EscenarioOperacionSeguraIntegradaSIL "Planta, modos, control y seguridad integrados con fallos durante extracción y vapor"
      parameter Modelica.Units.SI.Time periodoPWM = 1;
      GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
      GemeloDigitalPreparacionCafe.ControlTemperaturaEscalonado controlador;
      GemeloDigitalPreparacionCafe.SensorTemperaturaSIL sensorTemperatura(
        retardo = 0.4,
        amplitudRuido = 0.15);
      GemeloDigitalPreparacionCafe.SensorPresionSIL sensorPresion(
        retardo = 0.05,
        amplitudRuido = 0.03,
        frecuenciaRuido = 3,
        inicioFallo = 530,
        finFallo = 534);
      GemeloDigitalPreparacionCafe.SupervisorSeguridadSensoresSIL supervisor(
        periodoMuestreo = 0.1,
        tiempoMaximoSinMuestra = 0.5,
        tiempoMaximoSinCambio = 1);
      GemeloDigitalPreparacionCafe.ModoOperacionSIL modoProgramado;
      GemeloDigitalPreparacionCafe.ModoOperacionSIL modoEfectivo;
      Real referenciaC(unit = "degC");
      Real temperaturaSupervisadaC(unit = "degC");
      Boolean congelamientoTemperaturaVapor;
      Boolean paradaOperador;
      Boolean reconocimiento;
      Boolean habilitarOperacion;
      Boolean calentadorProgramado;
      Boolean bombaProgramada;
      Boolean solicitudCalentador;
      Boolean solicitudBomba;
      Boolean calentadorAutorizado;
      Boolean bombaAutorizada;
    equation
      modoProgramado = if time < 400 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.calentamientoCafe
        elseif time < 450 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.preparado
        elseif time < 466 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.extraccion
        elseif time < 500 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.preparado
        elseif time < 550 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.extraccion
        elseif time < 600 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.preparado
        elseif time < 700 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.calentamientoVapor
        elseif time < 760 then GemeloDigitalPreparacionCafe.ModoOperacionSIL.vapor
        else GemeloDigitalPreparacionCafe.ModoOperacionSIL.enfriamiento;
      congelamientoTemperaturaVapor = time >= 730 and time < 734;
      temperaturaSupervisadaC = if congelamientoTemperaturaVapor then 110 else sensorTemperatura.medidaC;
      paradaOperador = (time >= 535 and time < 550) or (time >= 735 and time < 760);
      reconocimiento = (time >= 532 and time < 532.2) or (time >= 534.2 and time < 534.4) or (time >= 540 and time < 540.2)
        or (time >= 732 and time < 732.2) or (time >= 734.2 and time < 734.4) or (time >= 740 and time < 740.2);
      habilitarOperacion = (time >= 545 and time < 545.2) or (time >= 745 and time < 745.2);
      modoEfectivo = if supervisor.estado == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.alarma then GemeloDigitalPreparacionCafe.ModoOperacionSIL.alarma
        elseif supervisor.estado == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.seguroInactivo or paradaOperador then GemeloDigitalPreparacionCafe.ModoOperacionSIL.seguroInactivo
        else modoProgramado;
      referenciaC = if modoProgramado == GemeloDigitalPreparacionCafe.ModoOperacionSIL.calentamientoVapor or modoProgramado == GemeloDigitalPreparacionCafe.ModoOperacionSIL.vapor then 125 else 93;
      bombaProgramada = modoProgramado == GemeloDigitalPreparacionCafe.ModoOperacionSIL.extraccion or modoProgramado == GemeloDigitalPreparacionCafe.ModoOperacionSIL.purga;
      controlador.referenciaC = referenciaC;
      controlador.medidaC = temperaturaSupervisadaC;
      controlador.extraccionActiva = solicitudBomba;
      calentadorProgramado = mod(time, periodoPWM) < controlador.accion*periodoPWM and modoProgramado <> GemeloDigitalPreparacionCafe.ModoOperacionSIL.purga and modoProgramado <> GemeloDigitalPreparacionCafe.ModoOperacionSIL.enfriamiento;
      solicitudCalentador = calentadorProgramado and not paradaOperador;
      solicitudBomba = bombaProgramada and not paradaOperador;
      sensorTemperatura.valorRealC = planta.temperaturaC;
      sensorPresion.valorRealBar = planta.presionBar;
      supervisor.temperaturaC = temperaturaSupervisadaC;
      supervisor.presionBar = sensorPresion.medidaBar;
      supervisor.reporteTemperaturaValida = sensorTemperatura.valida;
      supervisor.reportePresionValida = sensorPresion.valida;
      supervisor.nuevaMuestraTemperatura = sensorTemperatura.valida;
      supervisor.nuevaMuestraPresion = sensorPresion.valida;
      supervisor.esperarCambioTemperatura = solicitudCalentador;
      supervisor.esperarCambioPresion = solicitudBomba;
      supervisor.reconocimiento = reconocimiento;
      supervisor.condicionesSeguras = paradaOperador and not solicitudCalentador and not solicitudBomba;
      supervisor.habilitarOperacion = habilitarOperacion;
      calentadorAutorizado = solicitudCalentador and supervisor.autorizarActuadores;
      bombaAutorizada = solicitudBomba and supervisor.autorizarActuadores;
      planta.ordenCalentador = calentadorAutorizado;
      planta.ordenBomba = bombaAutorizada;
      planta.tensionBomba = if bombaAutorizada then 120 else 0;
      assert(supervisor.estado <> GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.alarma or not calentadorAutorizado and not bombaAutorizada, "Una alarma integrada debe inhibir calentador y bomba");
      assert(not (time >= 450.2 and time < 466) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.extraccion and bombaAutorizada, "La extracción normal integrada no se ejecutó");
      assert(not (time >= 530.2 and time < 534) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.alarma and not calentadorAutorizado and not bombaAutorizada, "El fallo de presión durante extracción no detuvo la operación");
      assert(not (time >= 534 and time < 540) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.alarma, "La extracción se reanudó antes del reconocimiento seguro");
      assert(not (time >= 540.2 and time < 545) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.seguroInactivo, "La recuperación de extracción no terminó en estado seguro inactivo");
      assert(not (time >= 545.2 and time < 550) or modoEfectivo <> GemeloDigitalPreparacionCafe.ModoOperacionSIL.extraccion, "La extracción interrumpida se reanudó automáticamente");
      assert(not (time >= 731.2 and time < 734) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.alarma and not calentadorAutorizado and not bombaAutorizada, "El sensor congelado durante vapor no detuvo la operación");
      assert(not (time >= 734 and time < 740) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.alarma, "El modo vapor se reanudó antes del reconocimiento seguro");
      assert(not (time >= 740.2 and time < 745) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.seguroInactivo, "La recuperación de vapor no terminó en estado seguro inactivo");
      assert(not (time >= 745.2 and time < 760) or modoEfectivo <> GemeloDigitalPreparacionCafe.ModoOperacionSIL.vapor, "El modo vapor interrumpido se reanudó automáticamente");
      assert(not (time >= 760.2) or modoEfectivo == GemeloDigitalPreparacionCafe.ModoOperacionSIL.enfriamiento and not bombaAutorizada, "No se alcanzó el enfriamiento seguro final");
      annotation(
        experiment(StartTime = 0, StopTime = 820, Interval = 0.1, Tolerance = 1e-6));
    end EscenarioOperacionSeguraIntegradaSIL;

    model PruebaInterfazGemeloDigital "Prueba funcional de los conectores destinados a temperatura y presión externas"
  GemeloDigitalPreparacionCafe.GemeloDigitalCafetera gemelo;
      parameter Modelica.Units.SI.Temperature T_superior = 367.52756876;
      parameter Modelica.Units.SI.Temperature T_inferior = T_superior - 2.117887927;
      discrete Boolean termostato(start = true, fixed = true);
      Boolean preinfusion;
      Boolean extraccion;
      Boolean cicloHidraulico;
    equation
      preinfusion = time >= 350 and time < 354;
      extraccion = time >= 355 and time < 366;
      cicloHidraulico = time >= 350 and time < 366;
      when gemelo.caldera.T_sensor >= T_superior then
        termostato = false;
      elsewhen gemelo.caldera.T_sensor <= T_inferior then
        termostato = true;
      end when;
      gemelo.ordenCalentador = if time >= 440 then false elseif cicloHidraulico then false else termostato;
      gemelo.ordenBomba = preinfusion or extraccion;
      gemelo.tensionBomba = if preinfusion then 110 elseif extraccion then 120 else 0;
      gemelo.datosSensoresValidos = true;
      gemelo.temperaturaMedidaC = gemelo.temperaturaSimuladaC - 0.8;
      gemelo.presionMedidaBar = if gemelo.ordenBomba then gemelo.presionSimuladaBar + 0.25 else 0;
      annotation(
        experiment(StartTime = 0, StopTime = 500, Interval = 0.2, Tolerance = 1e-6));
  end PruebaInterfazGemeloDigital;
  end Pruebas;

  model CalderaTermica "Modelo térmico concentrado y provisional de la caldera CM8502"
    parameter Modelica.Units.SI.Power P_nominal = 1050 "Potencia eléctrica nominal del calentador";
    parameter Real eta(min = 0, max = 1) = 0.336 "Factor térmico efectivo calibrado inicialmente con el calentamiento";
    parameter Modelica.Units.SI.Mass m_caldera = 0.43135 "Masa medida de las dos partes de la caldera";
    parameter Modelica.Units.SI.SpecificHeatCapacity cp_aluminio = 960 "Calor específico aproximado del aluminio ADC10";
    final parameter Modelica.Units.SI.HeatCapacity C_caldera = m_caldera*cp_aluminio "Capacidad térmica del aluminio";
    parameter Modelica.Units.SI.HeatCapacity C_efectiva = 1050 "Capacidad térmica efectiva provisional: caldera, agua y elementos asociados";
    parameter Modelica.Units.SI.Time tau_enfriamiento = 2600 "Constante de enfriamiento provisional";
    final parameter Modelica.Units.SI.ThermalConductance G_perdidas = C_efectiva/tau_enfriamiento "Conductancia térmica equivalente";
    parameter Modelica.Units.SI.Temperature T_ambiente = 294.65 "Temperatura ambiente: 21.5 degC";
    parameter Modelica.Units.SI.Temperature T_inicial = 294.65 "Temperatura inicial";
    parameter Boolean usarFlujo = false "Habilita la entrada de agua durante extracción";
    parameter Modelica.Units.SI.SpecificHeatCapacity cp_agua = 4180 "Calor específico del agua";
    parameter Modelica.Units.SI.Temperature T_aguaEntrada = 294.65 "Temperatura del agua que entra desde el tanque";
    Modelica.Blocks.Interfaces.BooleanInput calentadorActivo "Orden de encendido del calentador" annotation(
      Placement(transformation(extent = {{0, 0}, {0, 0}}), iconTransformation(origin = {-100, 20}, extent = {{-10, -10}, {10, 10}})));
    Modelica.Blocks.Interfaces.RealInput flujoMasico(unit = "kg/s") if usarFlujo "Caudal másico de agua que atraviesa la caldera" annotation(
      Placement(transformation(extent = {{0, 0}, {0, 0}}), iconTransformation(origin = {-100, -40}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Blocks.Interfaces.RealOutput temperatura(unit = "K") "Temperatura estimada de la caldera" annotation(
      Placement(transformation(extent = {{0, 0}, {0, 0}}), iconTransformation(origin = {100, 70}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Blocks.Interfaces.RealOutput temperaturaC(unit = "degC") "Temperatura estimada en grados Celsius" annotation(
      Placement(transformation(extent = {{0, 0}, {0, 0}}), iconTransformation(origin = {100, 35}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Blocks.Interfaces.RealOutput potenciaTermica(unit = "W") "Potencia térmica efectiva aplicada" annotation(
      Placement(transformation(extent = {{0, 0}, {0, 0}}), iconTransformation(origin = {100, 0}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Blocks.Interfaces.RealOutput perdidasTermicas(unit = "W") "Pérdidas térmicas estimadas al ambiente" annotation(
      Placement(transformation(extent = {{0, 0}, {0, 0}}), iconTransformation(origin = {100, -35}, extent = {{-10, -10}, {10, 10}})));
  Modelica.Blocks.Interfaces.RealOutput potenciaExtraidaAgua(unit = "W") "Potencia térmica retirada por el agua durante el flujo" annotation(
      Placement(transformation(extent = {{0, 0}, {0, 0}}), iconTransformation(origin = {100, -70}, extent = {{-10, -10}, {10, 10}})));
    Modelica.Units.SI.Temperature T(start = T_inicial, fixed = true) "Estado térmico concentrado";
  Modelica.Units.SI.MassFlowRate m_flow "Caudal interno usado por el balance";
  equation
  if usarFlujo then
      m_flow = flujoMasico;
    else
      m_flow = 0;
    end if;
    potenciaTermica = if calentadorActivo then eta*P_nominal else 0;
    perdidasTermicas = G_perdidas*(T - T_ambiente);
  potenciaExtraidaAgua = max(m_flow, 0)*cp_agua*(T - T_aguaEntrada);
  C_efectiva*der(T) = potenciaTermica - perdidasTermicas - potenciaExtraidaAgua;
    temperatura = T;
    temperaturaC = Modelica.Units.Conversions.to_degC(T);
    annotation(
      Icon(coordinateSystem(extent = {{-100, -100}, {100, 100}}, preserveAspectRatio = true, grid = {2, 2}), graphics = {Rectangle(lineColor = {60, 60, 60}, fillColor = {190, 195, 200}, fillPattern = FillPattern.VerticalCylinder, lineThickness = 0.8, extent = {{-58, -48}, {58, 48}}, radius = 8), Ellipse(lineColor = {60, 60, 60}, fillColor = {215, 220, 225}, fillPattern = FillPattern.Sphere, lineThickness = 0.8, extent = {{-58, 28}, {58, 68}}), Line(points = {{-35, -15}, {-20, 5}, {-5, -15}, {10, 5}, {25, -15}, {38, 5}}, color = {210, 45, 35}, thickness = 1.4), Text(extent = {{-90, -88}, {90, -58}}, textString = "%name", textColor = {20, 20, 20}, fontSize = 0, horizontalAlignment = TextAlignment.Center)}));
  end CalderaTermica;

  model CalderaTermicaDosNodos "Modelo térmico de dos nodos calibrado para la caldera CM8502"
  parameter Modelica.Units.SI.Power P_nominal = 1050 "Potencia eléctrica nominal";
    parameter Real eta(min = 0, max = 1) = 0.647228 "Factor térmico efectivo calibrado";
    parameter Modelica.Units.SI.HeatCapacity C_nucleo = 690.410 "Capacidad térmica efectiva del cuerpo caliente y agua";
    parameter Modelica.Units.SI.HeatCapacity C_sensor = 212.396 "Capacidad térmica efectiva de la zona superior medida";
    parameter Modelica.Units.SI.ThermalConductance G_interno = 13.2496 "Acoplamiento térmico entre los dos nodos";
    parameter Modelica.Units.SI.Time tau_enfriamiento = 2897.846 "Constante de pérdidas global";
    parameter Real fraccionPerdidasNucleo(min = 0, max = 1) = 0.236925 "Fracción de pérdidas asignada al nodo caliente";
    final parameter Modelica.Units.SI.ThermalConductance G_total = (C_nucleo + C_sensor)/tau_enfriamiento;
    final parameter Modelica.Units.SI.ThermalConductance G_nucleo = fraccionPerdidasNucleo*G_total;
    final parameter Modelica.Units.SI.ThermalConductance G_sensor = (1 - fraccionPerdidasNucleo)*G_total;
    parameter Modelica.Units.SI.Temperature T_ambiente = 294.65 "21.5 degC";
    parameter Modelica.Units.SI.Temperature T_inicialNucleo = 294.90;
    parameter Modelica.Units.SI.Temperature T_inicialSensor = 294.90;
    parameter Boolean usarFlujo = false;
    parameter Modelica.Units.SI.SpecificHeatCapacity cp_agua = 4180;
    parameter Modelica.Units.SI.Temperature T_aguaEntrada = 294.65;
    Modelica.Blocks.Interfaces.BooleanInput calentadorActivo;
    Modelica.Blocks.Interfaces.RealInput flujoMasico(unit = "kg/s") if usarFlujo;
    Modelica.Blocks.Interfaces.RealOutput temperaturaNucleo(unit = "K");
    Modelica.Blocks.Interfaces.RealOutput temperaturaSensor(unit = "K");
    Modelica.Blocks.Interfaces.RealOutput temperaturaNucleoC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput temperaturaSensorC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput potenciaTermica(unit = "W");
    Modelica.Blocks.Interfaces.RealOutput potenciaExtraidaAgua(unit = "W");
    Modelica.Units.SI.Temperature T_nucleo(start = T_inicialNucleo, fixed = true);
    Modelica.Units.SI.Temperature T_sensor(start = T_inicialSensor, fixed = true);
    Modelica.Units.SI.MassFlowRate m_flow;
    Modelica.Units.SI.HeatFlowRate Q_interno;
    Modelica.Units.SI.HeatFlowRate Q_perdidaNucleo;
    Modelica.Units.SI.HeatFlowRate Q_perdidaSensor;
  equation
    if usarFlujo then
      m_flow = flujoMasico;
    else
      m_flow = 0;
    end if;
    potenciaTermica = if calentadorActivo then eta*P_nominal else 0;
    potenciaExtraidaAgua = max(m_flow, 0)*cp_agua*(T_nucleo - T_aguaEntrada);
    Q_interno = G_interno*(T_nucleo - T_sensor);
    Q_perdidaNucleo = G_nucleo*(T_nucleo - T_ambiente);
    Q_perdidaSensor = G_sensor*(T_sensor - T_ambiente);
    C_nucleo*der(T_nucleo) = potenciaTermica - Q_interno - Q_perdidaNucleo - potenciaExtraidaAgua;
    C_sensor*der(T_sensor) = Q_interno - Q_perdidaSensor;
    temperaturaNucleo = T_nucleo;
    temperaturaSensor = T_sensor;
    temperaturaNucleoC = Modelica.Units.Conversions.to_degC(T_nucleo);
    temperaturaSensorC = Modelica.Units.Conversions.to_degC(T_sensor);
  end CalderaTermicaDosNodos;

  model BombaVibratoria "Modelo hidráulico simplificado de la bomba Jiayin JYPC-503A"
  parameter Modelica.Units.SI.Voltage tensionNominal = 120 "Tensión nominal usada en la cafetera";
    parameter Modelica.Units.SI.Power potenciaNominal = 55 "Dato de placa";
    parameter Modelica.Units.SI.Pressure presionMaxima = 15e5 "Presión máxima de placa: 15 bar";
    parameter Modelica.Units.SI.Pressure presionReferencia = 9e5 "Punto provisional para parametrizar la pendiente";
    parameter Modelica.Units.SI.MassFlowRate caudalReferencia = 0.001666 "Valor provisional procedente del ajuste térmico";
    final parameter Real resistenciaInterna(unit = "Pa.s/kg") = (presionMaxima - presionReferencia)/caudalReferencia "Pendiente lineal provisional de la curva presión-caudal";
    Modelica.Blocks.Interfaces.BooleanInput activa;
    Modelica.Blocks.Interfaces.RealInput tension(unit = "V");
    Modelica.Blocks.Interfaces.RealInput resistenciaCarga(unit = "Pa.s/kg");
    Modelica.Blocks.Interfaces.RealOutput caudalMasico(unit = "kg/s");
    Modelica.Blocks.Interfaces.RealOutput presionSalida(unit = "Pa");
    Modelica.Blocks.Interfaces.RealOutput potenciaElectrica(unit = "W");
    Real factorTension;
    Modelica.Units.SI.Pressure presionCierre;
  equation
    factorTension = min(max(tension/tensionNominal, 0), 1);
    presionCierre = if activa then presionMaxima*factorTension else 0;
    caudalMasico = if activa then presionCierre/max(resistenciaInterna + resistenciaCarga, 1) else 0;
    presionSalida = resistenciaCarga*caudalMasico;
    potenciaElectrica = if activa then potenciaNominal*factorTension^2 else 0;
  end BombaVibratoria;

  model TramoTuberia "Tramo entre bomba y caldera"
  parameter Modelica.Units.SI.Length longitud = 0.30 "Longitud aproximada medida";
    parameter Modelica.Units.SI.Diameter diametroInterno = 0.0019 "Diámetro interior informado";
    parameter Modelica.Units.SI.DynamicViscosity viscosidad = 0.001 "Agua cerca de temperatura ambiente";
    parameter Modelica.Units.SI.Density densidad = 997;
    final parameter Modelica.Units.SI.Area area = Modelica.Constants.pi*diametroInterno^2/4;
    final parameter Modelica.Units.SI.Volume volumenInterno = area*longitud;
    final parameter Real resistenciaHidraulica(unit = "Pa.s/kg") = 128*viscosidad*longitud/(densidad*Modelica.Constants.pi*diametroInterno^4) "Aproximación de Hagen-Poiseuille";
    Modelica.Blocks.Interfaces.RealInput caudalMasico(unit = "kg/s");
    Modelica.Blocks.Interfaces.RealOutput caidaPresion(unit = "Pa");
    Modelica.Blocks.Interfaces.RealOutput velocidad(unit = "m/s");
    Modelica.Blocks.Interfaces.RealOutput Reynolds;
  equation
    caidaPresion = resistenciaHidraulica*caudalMasico;
    velocidad = caudalMasico/(densidad*area);
    Reynolds = densidad*abs(velocidad)*diametroInterno/viscosidad;
  end TramoTuberia;

  type ModoOperacionSIL = enumeration(
    seguroInactivo "Operación detenida; requiere habilitación explícita",
    calentamientoCafe "Calentamiento hacia consigna de café",
    preparado "Regulación en consigna de café",
    extraccion "Bomba activa durante preparación",
    calentamientoVapor "Calentamiento térmico preliminar para vapor",
    vapor "Mantenimiento preliminar de temperatura de vapor",
    purga "Circulación de agua sin calentador",
    enfriamiento "Retorno pasivo hacia temperatura de café",
    alarma "Protección enclavada sin autorización de actuadores") "Estados usados solo por escenarios SIL";

  type EstadoSeguridadSIL = enumeration(
    operativo "Actuadores gobernados por las autorizaciones normales",
    alarma "Fallo enclavado; ningún actuador autorizado",
    seguroInactivo "Fallo reconocido y despejado; requiere habilitación independiente") "Estado del supervisor de seguridad usado solo en SIL";

  block SensorTemperaturaSIL "Sensor térmico virtual con retardo, sesgo, ruido determinista y pérdida"
    parameter Modelica.Units.SI.Time retardo(min = 0) = 0;
    parameter Real sesgo(unit = "K") = 0;
    parameter Real amplitudRuido(unit = "K", min = 0) = 0;
    parameter Modelica.Units.SI.Frequency frecuenciaRuido = 1;
    parameter Modelica.Units.SI.Time inicioFallo = Modelica.Constants.inf;
    parameter Modelica.Units.SI.Time finFallo = Modelica.Constants.inf;
    parameter Real valorFalloC(unit = "degC") = 0;
    Modelica.Blocks.Interfaces.RealInput valorRealC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput medidaC(unit = "degC");
    Modelica.Blocks.Interfaces.BooleanOutput valida;
  equation
    valida = not (time >= inicioFallo and time < finFallo);
    medidaC = if valida then delay(valorRealC, retardo) + sesgo + amplitudRuido*sin(2*Modelica.Constants.pi*frecuenciaRuido*time) else valorFalloC;
  end SensorTemperaturaSIL;

  block SensorPresionSIL "Sensor de presión virtual con retardo, sesgo, ruido determinista y pérdida"
    parameter Modelica.Units.SI.Time retardo(min = 0) = 0;
    parameter Real sesgo(unit = "bar") = 0;
    parameter Real amplitudRuido(unit = "bar", min = 0) = 0;
    parameter Modelica.Units.SI.Frequency frecuenciaRuido = 5;
    parameter Modelica.Units.SI.Time inicioFallo = Modelica.Constants.inf;
    parameter Modelica.Units.SI.Time finFallo = Modelica.Constants.inf;
    parameter Real valorFalloBar(unit = "bar") = 0;
    Modelica.Blocks.Interfaces.RealInput valorRealBar(unit = "bar");
    Modelica.Blocks.Interfaces.RealOutput medidaBar(unit = "bar");
    Modelica.Blocks.Interfaces.BooleanOutput valida;
  equation
    valida = not (time >= inicioFallo and time < finFallo);
    medidaBar = if valida then delay(valorRealBar, retardo) + sesgo + amplitudRuido*sin(2*Modelica.Constants.pi*frecuenciaRuido*time) else valorFalloBar;
  end SensorPresionSIL;

  block SupervisorSeguridadSensoresSIL "Detección SIL de invalidez, rango, obsolescencia y congelamiento con alarma enclavada"
    parameter Modelica.Units.SI.Time periodoMuestreo(min = 1e-3) = 0.1;
    parameter Modelica.Units.SI.Time tiempoMaximoSinMuestra(min = periodoMuestreo) = 0.5;
    parameter Modelica.Units.SI.Time tiempoMaximoSinCambio(min = periodoMuestreo) = 1;
    parameter Real temperaturaMinimaC(unit = "degC") = 0;
    parameter Real temperaturaMaximaC(unit = "degC") = 150;
    parameter Real presionMinimaBar(unit = "bar") = -0.5;
    parameter Real presionMaximaBar(unit = "bar") = 15;
    parameter Real cambioMinimoTemperatura(unit = "K", min = 0) = 0.02;
    parameter Real cambioMinimoPresion(unit = "bar", min = 0) = 0.001;
    Modelica.Blocks.Interfaces.RealInput temperaturaC(unit = "degC");
    Modelica.Blocks.Interfaces.RealInput presionBar(unit = "bar");
    Modelica.Blocks.Interfaces.BooleanInput reporteTemperaturaValida;
    Modelica.Blocks.Interfaces.BooleanInput reportePresionValida;
    Modelica.Blocks.Interfaces.BooleanInput nuevaMuestraTemperatura;
    Modelica.Blocks.Interfaces.BooleanInput nuevaMuestraPresion;
    Modelica.Blocks.Interfaces.BooleanInput esperarCambioTemperatura "Solo debe activarse cuando la física exige variación";
    Modelica.Blocks.Interfaces.BooleanInput esperarCambioPresion "Solo debe activarse cuando la física exige variación";
    Modelica.Blocks.Interfaces.BooleanInput reconocimiento "Reconocimiento explícito de la alarma";
    Modelica.Blocks.Interfaces.BooleanInput condicionesSeguras "Solicitudes de actuación retiradas y condiciones despejadas";
    Modelica.Blocks.Interfaces.BooleanInput habilitarOperacion "Orden independiente posterior al reconocimiento";
    Modelica.Blocks.Interfaces.BooleanOutput autorizarActuadores;
    Modelica.Blocks.Interfaces.BooleanOutput falloTemperatura;
    Modelica.Blocks.Interfaces.BooleanOutput falloPresion;
    Boolean temperaturaDentroRango;
    Boolean presionDentroRango;
    Boolean temperaturaObsoleta;
    Boolean presionObsoleta;
    Boolean temperaturaCongelada;
    Boolean presionCongelada;
    Boolean falloCritico;
    GemeloDigitalPreparacionCafe.EstadoSeguridadSIL estado;
  protected
    discrete Modelica.Units.SI.Time instanteUltimaMuestraTemperatura(start = 0, fixed = true);
    discrete Modelica.Units.SI.Time instanteUltimaMuestraPresion(start = 0, fixed = true);
    discrete Modelica.Units.SI.Time instanteUltimoCambioTemperatura(start = 0, fixed = true);
    discrete Modelica.Units.SI.Time instanteUltimoCambioPresion(start = 0, fixed = true);
    discrete Real ultimaTemperaturaC(unit = "degC", start = 21.75, fixed = true);
    discrete Real ultimaPresionBar(unit = "bar", start = 0, fixed = true);
    discrete GemeloDigitalPreparacionCafe.EstadoSeguridadSIL estadoInterno(
      start = GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.operativo,
      fixed = true);
  equation
    temperaturaDentroRango = temperaturaC >= temperaturaMinimaC and temperaturaC <= temperaturaMaximaC;
    presionDentroRango = presionBar >= presionMinimaBar and presionBar <= presionMaximaBar;
    temperaturaObsoleta = time - instanteUltimaMuestraTemperatura >= tiempoMaximoSinMuestra;
    presionObsoleta = time - instanteUltimaMuestraPresion >= tiempoMaximoSinMuestra;
    temperaturaCongelada = esperarCambioTemperatura and time - instanteUltimoCambioTemperatura >= tiempoMaximoSinCambio;
    presionCongelada = esperarCambioPresion and time - instanteUltimoCambioPresion >= tiempoMaximoSinCambio;
    falloTemperatura = not reporteTemperaturaValida or not temperaturaDentroRango or temperaturaObsoleta or temperaturaCongelada;
    falloPresion = not reportePresionValida or not presionDentroRango or presionObsoleta or presionCongelada;
    falloCritico = falloTemperatura or falloPresion;
    estado = estadoInterno;
    autorizarActuadores = estadoInterno == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.operativo and not falloCritico;
    when sample(0, periodoMuestreo) then
      instanteUltimaMuestraTemperatura = if nuevaMuestraTemperatura and reporteTemperaturaValida then time else pre(instanteUltimaMuestraTemperatura);
      instanteUltimaMuestraPresion = if nuevaMuestraPresion and reportePresionValida then time else pre(instanteUltimaMuestraPresion);
      instanteUltimoCambioTemperatura = if not esperarCambioTemperatura or nuevaMuestraTemperatura and reporteTemperaturaValida and abs(temperaturaC - pre(ultimaTemperaturaC)) >= cambioMinimoTemperatura then time else pre(instanteUltimoCambioTemperatura);
      instanteUltimoCambioPresion = if not esperarCambioPresion or nuevaMuestraPresion and reportePresionValida and abs(presionBar - pre(ultimaPresionBar)) >= cambioMinimoPresion then time else pre(instanteUltimoCambioPresion);
      ultimaTemperaturaC = if nuevaMuestraTemperatura and reporteTemperaturaValida then temperaturaC else pre(ultimaTemperaturaC);
      ultimaPresionBar = if nuevaMuestraPresion and reportePresionValida then presionBar else pre(ultimaPresionBar);
    end when;
    when falloCritico then
      estadoInterno = GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.alarma;
    elsewhen reconocimiento then
      estadoInterno = if pre(estadoInterno) == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.alarma and condicionesSeguras and not falloCritico then GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.seguroInactivo else pre(estadoInterno);
    elsewhen habilitarOperacion then
      estadoInterno = if pre(estadoInterno) == GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.seguroInactivo and condicionesSeguras and not falloCritico then GemeloDigitalPreparacionCafe.EstadoSeguridadSIL.operativo else pre(estadoInterno);
    end when;
  end SupervisorSeguridadSensoresSIL;

  block InterfazSensores "Conversión de las variables internas a señales del gemelo digital"
  parameter Modelica.Units.SI.Density densidadAgua = 997;
    Modelica.Blocks.Interfaces.RealInput temperaturaC(unit = "degC");
    Modelica.Blocks.Interfaces.RealInput presionPa(unit = "Pa");
    Modelica.Blocks.Interfaces.RealInput caudalMasico(unit = "kg/s");
    Modelica.Blocks.Interfaces.BooleanInput estadoBomba;
    Modelica.Blocks.Interfaces.BooleanInput estadoCalentador;
    Modelica.Blocks.Interfaces.RealOutput temperaturaMedidaC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput presionMedidaBar(unit = "bar");
    Modelica.Blocks.Interfaces.RealOutput caudalSimulado_mL_min(unit = "mL/min") "Estimación interna; no es una medición";
    Modelica.Blocks.Interfaces.BooleanOutput bombaActiva;
    Modelica.Blocks.Interfaces.BooleanOutput calentadorActivo;
  equation
    temperaturaMedidaC = temperaturaC;
    presionMedidaBar = presionPa/1e5;
    caudalSimulado_mL_min = caudalMasico/densidadAgua*60*1e6;
    bombaActiva = estadoBomba;
    calentadorActivo = estadoCalentador;
  end InterfazSensores;

  model SistemaTermohidraulico "Planta virtual integrada de la cafetera"
  parameter Modelica.Units.SI.MassFlowRate caudalReferencia = 0.001817 "Calibración indirecta pendiente de medición";
    parameter Modelica.Units.SI.Pressure presionCafeReferencia = 9e5 "Suposición provisional para la carga de extracción";
    final parameter Real resistenciaCafe(unit = "Pa.s/kg") = presionCafeReferencia/caudalReferencia;
    GemeloDigitalPreparacionCafe.CalderaTermicaDosNodos caldera(usarFlujo = true);
    GemeloDigitalPreparacionCafe.BombaVibratoria bomba(caudalReferencia = caudalReferencia, presionReferencia = presionCafeReferencia);
    GemeloDigitalPreparacionCafe.TramoTuberia tuberia;
    GemeloDigitalPreparacionCafe.InterfazSensores sensores;
    Modelica.Blocks.Interfaces.BooleanInput ordenCalentador;
    Modelica.Blocks.Interfaces.BooleanInput ordenBomba;
    Modelica.Blocks.Interfaces.RealInput tensionBomba(unit = "V");
    Modelica.Blocks.Interfaces.RealOutput temperaturaC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput temperaturaNucleoC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput presionBar(unit = "bar");
    Modelica.Blocks.Interfaces.RealOutput caudalSimulado_mL_min(unit = "mL/min") "Estimación interna no exportable a la ontología";
    Modelica.Blocks.Interfaces.RealOutput caudalMasico(unit = "kg/s");
  equation
    bomba.activa = ordenBomba;
    bomba.tension = tensionBomba;
    bomba.resistenciaCarga = resistenciaCafe + tuberia.resistenciaHidraulica;
    tuberia.caudalMasico = bomba.caudalMasico;
    caldera.flujoMasico = bomba.caudalMasico;
    caldera.calentadorActivo = ordenCalentador;
    sensores.temperaturaC = caldera.temperaturaSensorC;
    sensores.presionPa = resistenciaCafe*bomba.caudalMasico;
    sensores.caudalMasico = bomba.caudalMasico;
    sensores.estadoBomba = ordenBomba;
    sensores.estadoCalentador = ordenCalentador;
    temperaturaC = sensores.temperaturaMedidaC;
    temperaturaNucleoC = caldera.temperaturaNucleoC;
    presionBar = sensores.presionMedidaBar;
    caudalSimulado_mL_min = sensores.caudalSimulado_mL_min;
    caudalMasico = bomba.caudalMasico;
  end SistemaTermohidraulico;

  block ControlSecuenciaEspresso "Programación temporal de un ciclo de espresso"
  parameter Modelica.Units.SI.Time tiempoInicio = 350;
    parameter Modelica.Units.SI.Time duracionPreinfusion = 4;
    parameter Modelica.Units.SI.Time duracionPausa = 1;
    parameter Modelica.Units.SI.Time duracionTotal = 16;
    parameter Modelica.Units.SI.Voltage tensionPreinfusion = 110;
    parameter Modelica.Units.SI.Voltage tensionExtraccion = 120;
    final parameter Modelica.Units.SI.Time inicioExtraccion = tiempoInicio + duracionPreinfusion + duracionPausa;
    final parameter Modelica.Units.SI.Time finCiclo = tiempoInicio + duracionTotal;
    Modelica.Blocks.Interfaces.BooleanOutput preinfusion;
    Modelica.Blocks.Interfaces.BooleanOutput pausa;
    Modelica.Blocks.Interfaces.BooleanOutput extraccion;
    Modelica.Blocks.Interfaces.BooleanOutput bombaActiva;
    Modelica.Blocks.Interfaces.BooleanOutput cicloActivo;
    Modelica.Blocks.Interfaces.RealOutput tensionBomba(unit = "V");
  equation
    preinfusion = time >= tiempoInicio and time < tiempoInicio + duracionPreinfusion;
    pausa = time >= tiempoInicio + duracionPreinfusion and time < inicioExtraccion;
    extraccion = time >= inicioExtraccion and time < finCiclo;
    cicloActivo = time >= tiempoInicio and time < finCiclo;
    bombaActiva = preinfusion or extraccion;
    tensionBomba = if preinfusion then tensionPreinfusion elseif extraccion then tensionExtraccion else 0;
  end ControlSecuenciaEspresso;

  partial block InterfazControlTermicoNormalizado "Interfaz común de los candidatos de control térmico"
    Modelica.Blocks.Interfaces.RealInput referenciaC(unit = "degC");
    Modelica.Blocks.Interfaces.RealInput medidaC(unit = "degC");
    Modelica.Blocks.Interfaces.BooleanInput extraccionActiva "Orden conocida de bomba; no representa caudal medido ni estimado";
    Modelica.Blocks.Interfaces.RealOutput accion(min = 0, max = 1) "Fracción solicitada de potencia";
    Modelica.Blocks.Interfaces.RealOutput error(unit = "K") "Referencia menos medición";
  end InterfazControlTermicoNormalizado;

  block ControlTemperaturaHisteresis "Control térmico de referencia equivalente al simulador actual del firmware"
    extends GemeloDigitalPreparacionCafe.InterfazControlTermicoNormalizado;
    parameter Modelica.Units.SI.TemperatureDifference semibanda = 0.5;
    discrete Boolean calentadorEncendido(start = true, fixed = true);
  equation
    error = referenciaC - medidaC;
    accion = if calentadorEncendido then 1 else 0;
    when initial() then
      calentadorEncendido = medidaC < referenciaC;
    elsewhen medidaC <= referenciaC - semibanda then
      calentadorEncendido = true;
    elsewhen medidaC >= referenciaC + semibanda then
      calentadorEncendido = false;
    end when;
  end ControlTemperaturaHisteresis;

  block ControlTemperaturaEscalonado "Control por regiones inspirado funcionalmente en Gaggiuino y parametrizado para esta planta virtual"
    extends GemeloDigitalPreparacionCafe.InterfazControlTermicoNormalizado;
    parameter Modelica.Units.SI.TemperatureDifference bandaPotenciaTotal = 8 "Error por encima del cual se aplica potencia total";
    parameter Modelica.Units.SI.TemperatureDifference bandaPotenciaMedia = 2 "Frontera entre potencia media y cercana";
    parameter Real accionMedia(min = 0, max = 1) = 0.15;
    parameter Real accionCercana(min = 0, max = 1) = 0.04;
    parameter Real refuerzoExtraccion(min = 0, max = 1) = 0.8 "Potencia adicional candidata al conocer la orden de bomba; no usa caudal";
    Real accionPorRegion(min = 0, max = 1);
  equation
    error = referenciaC - medidaC;
    accionPorRegion = if error >= bandaPotenciaTotal then 1 elseif error >= bandaPotenciaMedia then accionMedia elseif error > 0 then accionCercana else 0;
    accion = min(1, accionPorRegion + (if extraccionActiva then refuerzoExtraccion else 0));
  end ControlTemperaturaEscalonado;

  block ControlTemperaturaPI "Control PI normalizado mediante LimPID estándar con anti-windup por retrocálculo"
    extends GemeloDigitalPreparacionCafe.InterfazControlTermicoNormalizado;
    parameter Real Kp(unit = "1/K", min = Modelica.Constants.small) = 0.021 "Ganancia proporcional candidata obtenida solamente en simulación";
    parameter Modelica.Units.SI.Time Ti = 400 "Tiempo integral candidato obtenido solamente en simulación";
    parameter Real Ni(min = 100*Modelica.Constants.eps) = 0.1 "Ni*Ti es la constante de seguimiento del anti-windup";
    parameter Real compensacionExtraccion(min = 0, max = 1) = 0 "Prealimentación por orden de bomba, sin estimar caudal";
    parameter Real uMin = 0;
    parameter Real uMax = 1;
    Modelica.Blocks.Continuous.LimPID pi(
      controllerType = Modelica.Blocks.Types.SimpleController.PI,
      k = Kp,
      Ti = Ti,
      Ni = Ni,
      yMin = uMin,
      yMax = uMax,
      withFeedForward = true,
      kFF = compensacionExtraccion,
      strict = true,
      initType = Modelica.Blocks.Types.Init.InitialState,
      xi_start = 0);
    Real estadoIntegral(unit = "K");
    Real accionSinSaturar;
  equation
    pi.u_s = referenciaC;
    pi.u_m = medidaC;
    pi.u_ff = if extraccionActiva then 1 else 0;
    error = pi.controlError;
    accion = pi.y;
    estadoIntegral = pi.I.y;
    accionSinSaturar = pi.limiter.u;
  end ControlTemperaturaPI;

  model GemeloDigitalCafetera "Núcleo termo-hidráulico preparado para recibir sensores externos"
  parameter Modelica.Units.SI.MassFlowRate caudalReferencia = 0.001817 "Valor calibrado provisional";
    parameter Real resistenciaCafe(unit = "Pa.s/kg") = 9e5/caudalReferencia "Carga hidráulica equivalente provisional";
    parameter Modelica.Units.SI.Density densidadAgua = 997;
    GemeloDigitalPreparacionCafe.CalderaTermicaDosNodos caldera(usarFlujo = true);
    GemeloDigitalPreparacionCafe.BombaVibratoria bomba(caudalReferencia = caudalReferencia);
    GemeloDigitalPreparacionCafe.TramoTuberia tuberia;
    Modelica.Blocks.Interfaces.BooleanInput ordenCalentador "Orden aplicada al calentador";
    Modelica.Blocks.Interfaces.BooleanInput ordenBomba "Orden aplicada a la bomba";
    Modelica.Blocks.Interfaces.RealInput tensionBomba(unit = "V") "Tensión eficaz aplicada a la bomba";
    Modelica.Blocks.Interfaces.RealInput temperaturaMedidaC(unit = "degC") "Temperatura recibida del sensor físico";
    Modelica.Blocks.Interfaces.RealInput presionMedidaBar(unit = "bar") "Presión manométrica recibida del sensor físico";
    Modelica.Blocks.Interfaces.BooleanInput datosSensoresValidos "Indica que las dos mediciones externas son utilizables";
    Modelica.Blocks.Interfaces.RealOutput temperaturaSimuladaC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput presionSimuladaBar(unit = "bar");
    Modelica.Blocks.Interfaces.RealOutput caudalSimulado_mL_min(unit = "mL/min") "Estimación interna; no es una medición";
    Modelica.Blocks.Interfaces.RealOutput errorTemperatura(unit = "K") "Temperatura simulada menos medida";
    Modelica.Blocks.Interfaces.RealOutput errorPresion(unit = "bar") "Presión simulada menos medida";
    Modelica.Blocks.Interfaces.RealOutput volumenProcesado_mL(unit = "mL") "Estimación interna integrada";
    Modelica.Units.SI.Pressure presionCafe;
    Modelica.Units.SI.Volume volumenProcesado(start = 0, fixed = true);
  equation
    caldera.calentadorActivo = ordenCalentador;
    bomba.activa = ordenBomba;
    bomba.tension = tensionBomba;
    bomba.resistenciaCarga = resistenciaCafe + tuberia.resistenciaHidraulica;
    tuberia.caudalMasico = bomba.caudalMasico;
    caldera.flujoMasico = bomba.caudalMasico;
    presionCafe = resistenciaCafe*bomba.caudalMasico;
    temperaturaSimuladaC = caldera.temperaturaSensorC;
    presionSimuladaBar = presionCafe/1e5;
    caudalSimulado_mL_min = bomba.caudalMasico/densidadAgua*60*1e6;
    der(volumenProcesado) = bomba.caudalMasico/densidadAgua;
    volumenProcesado_mL = volumenProcesado*1e6;
    errorTemperatura = if datosSensoresValidos then temperaturaSimuladaC - temperaturaMedidaC else 0;
    errorPresion = if datosSensoresValidos then presionSimuladaBar - presionMedidaBar else 0;
  end GemeloDigitalCafetera;

  model FmuAdapterV1 "Adaptador estable del modelo físico al contrato FMI v1 de simulación"
    parameter Modelica.Units.SI.Time periodoPWM = 1 "Periodo del calentador virtual";
    parameter Modelica.Units.SI.Voltage tensionBombaNominal = 120;
    Modelica.Blocks.Interfaces.RealInput heaterCommand(min = 0, max = 1, unit = "1") "Demanda normalizada de simulación";
    Modelica.Blocks.Interfaces.RealInput pumpCommand(min = 0, max = 1, unit = "1") "Demanda normalizada de simulación";
    Modelica.Blocks.Interfaces.RealOutput temperatureC(unit = "degC");
    Modelica.Blocks.Interfaces.RealOutput pressureBar(unit = "bar");
    GemeloDigitalPreparacionCafe.SistemaTermohidraulico planta;
  protected
    Real heaterCommandLimited(min = 0, max = 1);
    Real pumpCommandLimited(min = 0, max = 1);
    Boolean heaterPWM;
  equation
    heaterCommandLimited = min(1, max(0, heaterCommand));
    pumpCommandLimited = min(1, max(0, pumpCommand));
    heaterPWM = mod(time, periodoPWM) < heaterCommandLimited*periodoPWM;
    planta.ordenCalentador = heaterPWM;
    planta.ordenBomba = pumpCommandLimited > Modelica.Constants.small;
    planta.tensionBomba = tensionBombaNominal*pumpCommandLimited;
    temperatureC = planta.temperaturaC;
    pressureBar = planta.presionBar;
    annotation(
      experiment(StartTime = 0, StopTime = 800, Interval = 0.1, Tolerance = 1e-6));
  end FmuAdapterV1;

  model ModeloVaporPurgaEnfriamiento "Balance preliminar con tasas de fase y descarga externas"
    parameter Modelica.Units.SI.Volume volumenGeometrico = 152e-6
      "Volumen geométrico aproximado de Boiler 2; no es volumen útil";
    parameter Modelica.Units.SI.Mass masaAguaInicial = 0
      "Debe sustituirse por masa de llenado medida";
    parameter Modelica.Units.SI.SpecificHeatCapacity cpAgua = 4180;
    parameter Modelica.Units.SI.SpecificEnergy calorLatente = 2.26e6
      "Valor provisional; sustituir por propiedad IAPWS evaluada a la temperatura de trabajo";
    parameter Modelica.Units.SI.HeatCapacity capacidadTermica = 902.806;
    parameter Modelica.Units.SI.ThermalConductance conductanciaPerdidas = 0.3116;
    parameter Modelica.Units.SI.AbsolutePressure presionMaxima = 15e5
      "Límite de modelado; no sustituye la protección mecánica";
    parameter Modelica.Units.SI.Temperature temperaturaAmbiente = 294.65;
    Modelica.Blocks.Interfaces.BooleanInput calentadorActivo;
    Modelica.Blocks.Interfaces.BooleanInput purgaActiva;
    Modelica.Blocks.Interfaces.RealInput caudalEvaporacion(unit="kg/s", min=0)
      "Entrada provisional: tasa de evaporación identificada o medida";
    Modelica.Blocks.Interfaces.RealInput caudalDescarga(unit="kg/s", min=0)
      "Entrada provisional: descarga total por purga/vapor";
    Modelica.Blocks.Interfaces.RealOutput temperatura(unit="K");
    Modelica.Blocks.Interfaces.RealOutput presionAbsoluta(unit="Pa");
    Modelica.Blocks.Interfaces.RealOutput presionManometrica(unit="bar");
    Modelica.Blocks.Interfaces.RealOutput masaAgua(unit="kg");
    Modelica.Blocks.Interfaces.RealOutput masaVapor(unit="kg");
    Modelica.Blocks.Interfaces.RealOutput masaDescargada(unit="kg");
    Modelica.Units.SI.Temperature T(start=294.65, fixed=true);
    Modelica.Units.SI.Mass mL(start=masaAguaInicial, fixed=true);
    Modelica.Units.SI.Mass mV(start=0, fixed=true);
    Modelica.Units.SI.Mass mD(start=0, fixed=true);
    Modelica.Units.SI.HeatFlowRate potenciaEntrada;
    Modelica.Units.SI.HeatFlowRate potenciaPerdida;
    Modelica.Units.SI.Temperature temperaturaMaxima;
  equation
    potenciaEntrada = if calentadorActivo then 0.647228*1050 else 0;
    potenciaPerdida = conductanciaPerdidas*(T - temperaturaAmbiente);
    temperaturaMaxima = Modelica.Media.Water.StandardWater.saturationTemperature(presionMaxima);
    capacidadTermica*der(T) = if T >= temperaturaMaxima and
      potenciaEntrada - potenciaPerdida - caudalEvaporacion*calorLatente > 0 then 0 else
      potenciaEntrada - potenciaPerdida - caudalEvaporacion*calorLatente;
    der(mL) = -caudalEvaporacion;
    der(mV) = caudalEvaporacion - (if purgaActiva then caudalDescarga else 0);
    der(mD) = if purgaActiva then caudalDescarga else 0;
    assert(mL >= -1e-9, "La masa líquida no puede ser negativa");
    assert(mV >= -1e-9, "La masa de vapor no puede ser negativa");
    temperatura = T;
    presionAbsoluta = min(presionMaxima,
      Modelica.Media.Water.StandardWater.saturationPressure(max(T, 273.15)));
    presionManometrica = max(0, (presionAbsoluta - 101325)/1e5);
    masaAgua = max(0, mL);
    masaVapor = max(0, mV);
    masaDescargada = mD;
    annotation(experiment(StartTime=0, StopTime=800, Interval=0.1, Tolerance=1e-6));
  end ModeloVaporPurgaEnfriamiento;

  model PruebaVaporPurgaEnfriamiento "Prueba temporal de vapor, purga y enfriamiento"
    GemeloDigitalPreparacionCafe.ModeloVaporPurgaEnfriamiento caldera(
      masaAguaInicial=0.12,
      capacidadTermica=902.806,
      conductanciaPerdidas=0.3116);
    Boolean calentador;
    Boolean purga;
    Real evaporacion(unit="kg/s");
    Real descarga(unit="kg/s");
  equation
    calentador = time < 500;
    purga = time >= 500 and time < 560;
    evaporacion = if time >= 300 and time < 500 then 1e-5 else 0;
    descarga = if purga then 1e-5 else 0;
    caldera.calentadorActivo = calentador;
    caldera.purgaActiva = purga;
    caldera.caudalEvaporacion = evaporacion;
    caldera.caudalDescarga = descarga;
    assert(not (time >= 500.1 and time < 560) or caldera.masaDescargada > 0,
      "La purga no produjo descarga");
    assert(not (time >= 560) or caldera.masaDescargada > 0,
      "La masa descargada debe conservarse después de la purga");
    annotation(experiment(StartTime=0, StopTime=800, Interval=0.5, Tolerance=1e-6));
  end PruebaVaporPurgaEnfriamiento;

  model CalderaTermicaVaporIntegrada "Caldera térmica y balance de fase en un único componente"
    parameter Modelica.Units.SI.Power P_nominal=1050;
    parameter Real eta(min=0,max=1)=0.647228;
    parameter Modelica.Units.SI.HeatCapacity C_nucleo=690.410;
    parameter Modelica.Units.SI.HeatCapacity C_sensor=212.396;
    parameter Modelica.Units.SI.ThermalConductance G_interno=13.2496;
    parameter Modelica.Units.SI.Time tau_enfriamiento=2897.846;
    parameter Real fraccionPerdidasNucleo(min=0,max=1)=0.236925;
    parameter Modelica.Units.SI.Temperature T_ambiente=294.65;
    parameter Modelica.Units.SI.SpecificHeatCapacity cp_agua=4180;
    parameter Modelica.Units.SI.Temperature T_aguaEntrada=294.65;
    parameter Modelica.Units.SI.Mass masaAguaInicial=0;
    parameter Modelica.Units.SI.SpecificEnergy calorLatente=2.26e6;
    parameter Modelica.Units.SI.AbsolutePressure presionMaxima=15e5;
    final parameter Modelica.Units.SI.ThermalConductance G_total=(C_nucleo+C_sensor)/tau_enfriamiento;
    final parameter Modelica.Units.SI.ThermalConductance G_nucleo=fraccionPerdidasNucleo*G_total;
    final parameter Modelica.Units.SI.ThermalConductance G_sensor=(1-fraccionPerdidasNucleo)*G_total;
    Modelica.Blocks.Interfaces.BooleanInput calentadorActivo;
    Modelica.Blocks.Interfaces.BooleanInput purgaActiva;
    Modelica.Blocks.Interfaces.RealInput flujoMasico(unit="kg/s");
    Modelica.Blocks.Interfaces.RealInput caudalEvaporacion(unit="kg/s",min=0);
    Modelica.Blocks.Interfaces.RealInput caudalDescarga(unit="kg/s",min=0);
    Modelica.Blocks.Interfaces.RealInput caudalCondensacion(unit="kg/s",min=0);
    Modelica.Blocks.Interfaces.RealOutput temperaturaSensorC(unit="degC");
    Modelica.Blocks.Interfaces.RealOutput temperaturaNucleoC(unit="degC");
    Modelica.Blocks.Interfaces.RealOutput presionBar(unit="bar");
    Modelica.Blocks.Interfaces.RealOutput masaAgua(unit="kg");
    Modelica.Blocks.Interfaces.RealOutput masaVapor(unit="kg");
    Modelica.Blocks.Interfaces.RealOutput masaDescargada(unit="kg");
    Modelica.Units.SI.Temperature T_nucleo(start=294.65,fixed=true);
    Modelica.Units.SI.Temperature T_sensor(start=294.65,fixed=true);
    Modelica.Units.SI.Mass mL(start=masaAguaInicial,fixed=true);
    Modelica.Units.SI.Mass mV(start=0,fixed=true);
    Modelica.Units.SI.Mass mD(start=0,fixed=true);
    Modelica.Units.SI.HeatFlowRate Q_interno;
    Modelica.Units.SI.HeatFlowRate Q_perdidaNucleo;
    Modelica.Units.SI.HeatFlowRate Q_perdidaSensor;
    Modelica.Units.SI.HeatFlowRate Q_extraida;
    Modelica.Units.SI.Temperature T_max;
  equation
    T_max=Modelica.Media.Water.StandardWater.saturationTemperature(presionMaxima);
    Q_interno=G_interno*(T_nucleo-T_sensor);
    Q_perdidaNucleo=G_nucleo*(T_nucleo-T_ambiente);
    Q_perdidaSensor=G_sensor*(T_sensor-T_ambiente);
    Q_extraida=max(flujoMasico,0)*cp_agua*(T_nucleo-T_aguaEntrada);
    C_nucleo*der(T_nucleo)= (if calentadorActivo then eta*P_nominal else 0)
      -Q_interno-Q_perdidaNucleo-Q_extraida-caudalEvaporacion*calorLatente;
    C_sensor*der(T_sensor)=Q_interno-Q_perdidaSensor;
    der(mL)=-caudalEvaporacion+caudalCondensacion;
    der(mV)=caudalEvaporacion-caudalCondensacion-(if purgaActiva then caudalDescarga else 0);
    der(mD)=if purgaActiva then caudalDescarga else 0;
    assert(mL>=-1e-9,"La masa líquida no puede ser negativa");
    assert(mV>=-1e-9,"La masa de vapor no puede ser negativa");
    temperaturaSensorC=Modelica.Units.Conversions.to_degC(T_sensor);
    temperaturaNucleoC=Modelica.Units.Conversions.to_degC(T_nucleo);
    presionBar=max(0,(min(presionMaxima,Modelica.Media.Water.StandardWater.saturationPressure(max(T_sensor,273.15)))-101325)/1e5);
    masaAgua=max(0,mL);
    masaVapor=max(0,mV);
    masaDescargada=mD;
  end CalderaTermicaVaporIntegrada;

  model SistemaTermohidraulicoVapor "Sistema principal con una caldera térmica y de vapor compartida"
    parameter Modelica.Units.SI.MassFlowRate caudalReferencia=0.001817;
    parameter Modelica.Units.SI.Pressure presionCafeReferencia=9e5;
    final parameter Real resistenciaCafe(unit="Pa.s/kg")=presionCafeReferencia/caudalReferencia;
    CalderaTermicaVaporIntegrada caldera(masaAguaInicial=0.12);
    BombaVibratoria bomba(caudalReferencia=caudalReferencia,presionReferencia=presionCafeReferencia);
    TramoTuberia tuberia;
    Modelica.Blocks.Interfaces.BooleanInput ordenCalentador;
    Modelica.Blocks.Interfaces.BooleanInput ordenBomba;
    Modelica.Blocks.Interfaces.BooleanInput purgaActiva;
    Modelica.Blocks.Interfaces.BooleanInput modoVapor;
    Modelica.Blocks.Interfaces.RealInput tensionBomba(unit="V");
    Modelica.Blocks.Interfaces.RealInput caudalEvaporacion(unit="kg/s");
    Modelica.Blocks.Interfaces.RealInput caudalDescarga(unit="kg/s");
    Modelica.Blocks.Interfaces.RealInput caudalCondensacion(unit="kg/s");
    Modelica.Blocks.Interfaces.RealOutput temperaturaC(unit="degC");
    Modelica.Blocks.Interfaces.RealOutput presionBar(unit="bar");
    Modelica.Blocks.Interfaces.RealOutput masaVapor(unit="kg");
    Modelica.Blocks.Interfaces.RealOutput masaDescargada(unit="kg");
  equation
    bomba.activa=ordenBomba;
    bomba.tension=tensionBomba;
    bomba.resistenciaCarga=resistenciaCafe+tuberia.resistenciaHidraulica;
    tuberia.caudalMasico=bomba.caudalMasico;
    caldera.flujoMasico=bomba.caudalMasico;
    caldera.calentadorActivo=ordenCalentador;
    caldera.purgaActiva=purgaActiva;
    caldera.caudalEvaporacion=max(caudalEvaporacion,0);
    caldera.caudalDescarga=max(caudalDescarga,0);
    caldera.caudalCondensacion=max(caudalCondensacion,0);
    temperaturaC=caldera.temperaturaSensorC;
    presionBar=if modoVapor or purgaActiva then caldera.presionBar else resistenciaCafe*bomba.caudalMasico/1e5;
    masaVapor=caldera.masaVapor;
    masaDescargada=caldera.masaDescargada;
  end SistemaTermohidraulicoVapor;
end GemeloDigitalPreparacionCafe;
