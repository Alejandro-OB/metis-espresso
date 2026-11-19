within GemeloDigitalPreparacionCafe;
model FmuAdapterV21 "Adaptador al contrato FMI v2 (FMU 2.1.0): la v2 con el agua retenida que entra a la caldera y la configuración del portafiltro"
  // FMU espresso-detailed 2.1.0. Las mismas tres piezas que FmuAdapterV2, sin cambiar
  // ninguna de sus ecuaciones ni de sus parámetros:
  //   CalderaTermicaEstratificada  recalibración 10.2 (Documentacion/recalibracion-10.2.md)
  //   CircuitoHidraulicoMedido     paso 10.4 (Documentacion/modelo-presion-10.4.md)
  //   RecorridoGrupoPortafiltro    2026-10-18 (Documentacion/agua-portafiltro.md)
  // y dos cambios en cómo se conectan, con método, partición, ajuste y comprobación en
  // Documentacion/agua-retenida-2.1.md (réplica y ajuste en agua_retenida/):
  //
  // 1. La caldera recibe, además de lo que pasa la restricción, un agua retenida.
  //    La caldera de la 10.2 se ajustó con una entrada de caudal que incluía 48,6 g
  //    repartidos durante el llenado (modelo16.inflow), y la v2 solo le daba lo que
  //    pasa la restricción: en el café, unos 26 g de llenado de la canastilla y después
  //    el caudal a la taza. Con eso la caldera simulada se enfriaba en el café la mitad
  //    que la real. La caldera es la frontera y no se reajusta; lo que se define es el
  //    agua que le falta, por configuración:
  //      café:           masaRetenidaCafe gramos, a ritmo constante, en tiempoRetenidaCafe s
  //      sin canastilla: masaRetenidaSinCanastilla gramos, al ritmo de la bomba
  //    desde que arranca la bomba y mientras siga en marcha. La forma de cada una salió
  //    de la regla escrita antes de ajustar (la de un parámetro si no cuesta más de un
  //    10 % que la de dos). Es agua del depósito que la caldera recibe al empezar el café
  //    y la báscula no ve entonces; dónde queda no se afirma: es la masa que la caldera
  //    de la 10.2 necesita para enfriarse como la real, inferida, no un volumen medido.
  //
  // 2. La configuración del portafiltro es un parámetro, no una constante. La v2 usaba
  //    siempre la del café en canastilla presurizada de dos tazas, y sin canastilla la
  //    presión simulada subía a la del café y la taza no recibía nada mientras el
  //    llenado no se completaba. Sin canastilla el circuito usa ahora su curva sin
  //    llenado, y la taza recibe todo lo que pasa la restricción.
  //
  // configuracionPortafiltro es un número, 1 o 2, y no un booleano, para que una tercera
  // configuración medida (una taza, canastilla vacía) entre sin cambiar el contrato. Es
  // Real y no Integer a propósito: la plataforma fija los parámetros de la FMU con
  // setReal (espresso_simulation/fmi.py), y en FMI 2.0 las referencias de los Integer
  // son otro espacio; un Integer escrito con setReal cambiaría en silencio el Real que
  // comparte su número. Cualquier otro valor detiene la inicialización. El valor por
  // omisión es el café, el de la v2: quien no lo declare obtiene la v2 con su agua
  // retenida, no una configuración que nadie eligió.
  //
  // Vive en su propio archivo, como FmuAdapterV2: package.mo y FmuAdapterV2.mo tienen
  // su huella en model-registry-v1.json, y la 2.0.0 tiene que seguir reproduciéndose.
  //
  // Lo que no hace:
  //   - Una sola preparación por simulación, como la v2: ni el llenado de la canastilla
  //     ni el agua retenida se reinician al parar la bomba.
  //   - El recorrido al café sigue recibiendo lo que pasa la restricción, no el agua
  //     retenida: se ajustó con el caudal de la báscula (agua-portafiltro.md § 2).
  //   - No modela los purgados sin portafiltro, ni las canastillas vacías o de una taza.
  //   - No corrige lo que la comprobación dejó fuera: con el agua sin canastilla ajustada
  //     el 16, los cafés del 20 (102 °C) siguen enfriándose menos que la máquina
  //     (agua-retenida-2.1.md).
  //   - No modela el control.
  import SI = Modelica.Units.SI;
  parameter SI.Voltage tensionRed = 120.7 "Red supuesta si no se da: la de referencia de la bomba (10.4)";
  parameter SI.Temperature T_inicial = 296.83 "Metal, agua y sensor al empezar (parámetro de la caldera)";
  parameter SI.Temperature T_ambiente = 296.83 "Cuarto y agua del depósito";
  parameter Real configuracionPortafiltro(min = 1, max = 2) = 1 "1: café en canastilla presurizada de dos tazas; 2: portafiltro sin canastilla";
  parameter Real resistenciaLecho(unit = "bar.s/g") = 0.1503 "Café, ajustada en E3 (10.4)";
  parameter Real volumenLlenado(unit = "g") = 25.99 "Café (10.4)";
  parameter Real anchoLlenado(unit = "g") = 1.946 "Café (10.4)";
  parameter Real masaRetenidaCafe(unit = "g") = 22.83 "Ajustada con los cafés T3 de ajuste (agua-retenida-2.1.md)";
  parameter SI.Time tiempoRetenidaCafe = 4.65 "Ajustado con los mismos cafés";
  parameter Real masaRetenidaSinCanastilla(unit = "g") = 26.54 "Ajustada con E1-01 a E1-05; entra al ritmo de la bomba";
  Modelica.Blocks.Interfaces.RealInput heaterCommand(min = 0, max = 1, unit = "1") "Demanda media del calentador";
  Modelica.Blocks.Interfaces.RealInput pumpCommand(min = 0, max = 1, unit = "1") "Densidad PSM de la bomba";
  Modelica.Blocks.Interfaces.RealOutput temperatureC(unit = "degC") "Lo que lee el NTC del cuerpo";
  Modelica.Blocks.Interfaces.RealOutput pressureBar(unit = "bar") "Transductor entre la bomba y la caldera";
  Modelica.Blocks.Interfaces.RealOutput waterTemperatureC(unit = "degC") "Agua sobre el café, estimada";
  Modelica.Blocks.Interfaces.RealOutput cupFlowGs(unit = "g/s") "Caudal hacia la taza";
  Real masaRetenidaEntrada(unit = "g", start = 0, fixed = true) "Agua retenida que ya entró a la caldera";
  Real caudalRetenido(unit = "g/s") "Agua retenida que entra ahora";
  CalderaTermicaEstratificada caldera(
    usarDemanda = true,
    usarFlujo = true,
    T_inicial = T_inicial,
    T_ambiente = T_ambiente,
    T_aguaEntrada = T_ambiente);
  // Sin canastilla, la curva de la configuración es la sin canastilla y no hay lecho;
  // con conCanastilla = false la fracción llenada es 1 desde el principio. La curva de
  // dos tazas se repite aquí porque la clase no la expone por separado: es su valor
  // por omisión (10.4).
  CircuitoHidraulicoMedido hidraulica(
    conCanastilla = conCafe,
    curva = if conCafe then {5.4439, 0.99070, 0.17603} else {5.6686, 0.33875, 0},
    resistenciaLecho = if conCafe then resistenciaLecho else 0,
    volumenLlenado = volumenLlenado,
    anchoLlenado = anchoLlenado);
  RecorridoGrupoPortafiltro recorrido(T_ambiente = T_ambiente);
protected
  // Sin `final`: un parámetro final puede evaluarse al compilar, y entonces la FMU
  // ignoraría la configuración que se le dé al instanciarla.
  parameter Boolean conCafe = configuracionPortafiltro < 1.5;
  parameter Real masaRetenida(unit = "g") = if conCafe then masaRetenidaCafe else masaRetenidaSinCanastilla;
initial equation
  assert(abs(configuracionPortafiltro - 1) < 1e-9 or abs(configuracionPortafiltro - 2) < 1e-9,
    "configuracionPortafiltro debe ser 1 (café en canastilla presurizada de dos tazas) o 2 (sin canastilla)");
equation
  caldera.calentadorActivo = false;
  caldera.demandaCalentador = min(1, max(0, heaterCommand));
  hidraulica.densidad = min(1, max(0, pumpCommand));
  hidraulica.tensionRed = tensionRed;
  // Como en la v2: la viscosidad con el agua que sale de la caldera, que es un estado.
  hidraulica.temperaturaAgua = caldera.temperaturaAguaSalidaC + 273.15;
  // El agua retenida entra mientras la bomba está en marcha y hasta completar su masa.
  // La condición sobre masaRetenidaEntrada genera un evento al completarse: el resolvedor
  // lo localiza y la masa que entra es exactamente la declarada.
  caudalRetenido = if pumpCommand > 0 and masaRetenidaEntrada < masaRetenida then
    (if conCafe then masaRetenidaCafe/tiempoRetenidaCafe else hidraulica.caudalBomba) else 0;
  der(masaRetenidaEntrada) = caudalRetenido;
  caldera.flujoMasico = (hidraulica.caudalRestriccion + caudalRetenido)/1000;
  recorrido.flujoMasico = hidraulica.caudalRestriccion/1000;
  recorrido.temperaturaAguaCaldera = caldera.temperaturaAguaSalidaC + 273.15;
  recorrido.temperaturaMetal = caldera.temperaturaNucleo;
  temperatureC = caldera.temperaturaSensorC;
  pressureBar = hidraulica.presionBar;
  waterTemperatureC = recorrido.temperaturaAguaCafeC;
  cupFlowGs = hidraulica.caudalTazaGs;
  annotation(
    experiment(StartTime = 0, StopTime = 800, Interval = 0.1, Tolerance = 1e-6));
end FmuAdapterV21;
