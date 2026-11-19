within GemeloDigitalPreparacionCafe;
model CalderaTermicaEstratificada "Caldera de tres masas: elemento, metal y agua en dos nodos estratificados (recalibración 10.2)"
  // Paso 10.2 del guion de puesta en servicio. Amplía CalderaTermicaDosNodos con la
  // estructura que separa lo que la máquina real tiene separado: el elemento
  // calefactor, el cuerpo de aluminio (donde está el NTC) y el agua, partida en dos
  // nodos en serie con el flujo. Ecuaciones, fuentes y origen de cada número en
  // models/modelica/procedencia-ecuaciones.md § 3.4; ajuste, comprobación y lo que no
  // reproduce en models/modelica/Documentacion/recalibracion-10.2.md.
  //
  // Por qué en un archivo propio y no dentro de package.mo: el registro de modelos
  // (model-registry-v1.json) guarda el sha256 de package.mo como fuente de la FMU
  // publicada, y el trabajador de simulación de la plataforma lo verifica antes de
  // cada simulación. Tocar un byte de package.mo deja la plataforma sin modelo hasta
  // reexportar (paso 10.6). Un archivo de clase dentro del directorio del paquete es
  // parte del paquete (Modelica 3.6, § 13.4.1) y no cambia esa huella.
  //
  // Vigencia: es el modelo térmico calibrado vigente desde la 10.2. La FMU publicada
  // (espresso-detailed@1.0.0, FmuAdapterV1 → SistemaTermohidraulico) sigue usando
  // CalderaTermicaDosNodos hasta que la 10.6 la reexporte; esta clase no la cambia.
  //
  // Interfaz compatible con CalderaTermicaDosNodos: mismas entradas (calentadorActivo,
  // flujoMasico) y mismas salidas (temperaturaSensorC, temperaturaNucleoC, ...), para
  // que la 10.6 pueda sustituirla sin tocar SistemaTermohidraulico más que en la
  // declaración. «Núcleo» es aquí el metal.
  //
  // No modela: ebullición, la ventana de ciclo integral del firmware, el portafiltro ni
  // el grupo como masas propias, y la dependencia continua del acople pared–agua con
  // el caudal (dos valores, con y sin bomba).
  import SI = Modelica.Units.SI;

  // --- Elemento y potencia ---
  parameter SI.Power P_nominal = 1085.9 "123,3² V²/14,0 Ω: tensión del 10-10 y resistencia en frío, medidas; la resistencia en caliente no se midió";
  parameter SI.HeatCapacity C_elemento = 43.436 "Fijo, de la 7.9: no separable de G_elementoMetal con una sola temperatura";
  parameter SI.ThermalConductance G_elementoMetal = 4.917 "Ajustado (10.2, partición A)";
  // --- Metal ---
  parameter SI.HeatCapacity C_metal = 399.7 "Ajustado (10.2, partición A); 431 g de aluminio a 960 J/(kg·K) son 414 J/K";
  parameter SI.ThermalConductance G_ambiente = 0.3350 "Conductancia al ambiente a DeltaT_ref; ajustada en T1 y T2 con ambiente y tensión medidos (10.2)";
  parameter Real exponentePerdidas(min = 0, max = 1) = 0.25 "Convección natural laminar, h ∝ ΔT^(1/4) (Bergman § 9.6); 0 da la pérdida lineal, descartada en la 10.2";
  parameter SI.TemperatureDifference DeltaT_ref = 60 "Solo normaliza G_ambiente";
  // --- Agua ---
  parameter SI.Mass m_agua = 0.081 "Agua útil medida, 81 ± 3 mL (0.4)";
  parameter SI.SpecificHeatCapacity cp_agua = 4186 "Agua a 20–90 °C (Bergman, tabla A.6)";
  parameter Real fraccionAguaInferior(min = 0.01, max = 0.99) = 0.5 "Discretización uniforme de Duffie y Beckman, § 8.4";
  parameter SI.ThermalConductance G_aguaForzada = 83.70 "Metal–agua con la bomba en marcha; ajustado (10.2, partición A)";
  parameter SI.ThermalConductance G_aguaNatural = 2.693 "Metal–agua con el agua quieta; ajustado (10.2, partición A)";
  // --- Sensor ---
  parameter SI.Time tau_sensor = 9.628 "Retardo del NTC en el metal; ajustado (10.2, partición A)";
  // --- Condiciones ---
  parameter SI.Temperature T_ambiente = 296.83 "Ambiente medio del escalón T2 (23,68 °C, medido), cuando no se usa la entrada externa";
  parameter SI.Temperature T_aguaEntrada = 296.83 "Agua del depósito: la del cuarto (supuesto, como en la 7.9)";
  parameter SI.Temperature T_inicial = 296.83 "Metal, agua y sensor al empezar";
  parameter Real demandaInicial(min = 0, max = 1) = 0 "Régimen de partida: el elemento arranca por encima del metal lo que pide su conductancia";
  parameter SI.MassFlowRate caudalMinimoBomba = 1e-6 "Por encima, se considera que la bomba mueve agua";
  // --- Entradas opcionales ---
  parameter Boolean usarFlujo = false "Habilita la entrada de caudal";
  parameter Boolean usarDemanda = false "true: demanda real 0–1; false: orden booleana del calentador";
  parameter Boolean usarEstadoBomba = false "true: el régimen pared–agua lo decide la entrada bombaActiva";
  parameter Boolean usarAmbienteExterno = false "true: ambiente desde la entrada temperaturaAmbiente";

  final parameter SI.HeatCapacity C_agua = m_agua*cp_agua;
  final parameter SI.HeatCapacity C_aguaSuperior = (1 - fraccionAguaInferior)*C_agua;
  final parameter SI.HeatCapacity C_aguaInferior = fraccionAguaInferior*C_agua;
  final parameter SI.HeatCapacity C_total = C_elemento + C_metal + C_agua "Comparable con los 753 J/K calculados";

  Modelica.Blocks.Interfaces.BooleanInput calentadorActivo "Orden del calentador (si usarDemanda = false)";
  Modelica.Blocks.Interfaces.RealInput demandaCalentador(min = 0, max = 1, unit = "1") if usarDemanda "Demanda media del calentador";
  Modelica.Blocks.Interfaces.RealInput flujoMasico(unit = "kg/s") if usarFlujo "Caudal de agua que entra del depósito";
  Modelica.Blocks.Interfaces.BooleanInput bombaActiva if usarEstadoBomba "Bomba en marcha";
  Modelica.Blocks.Interfaces.RealInput temperaturaAmbiente(unit = "K") if usarAmbienteExterno "Ambiente medido";
  Modelica.Blocks.Interfaces.RealOutput temperaturaNucleo(unit = "K") "Metal";
  Modelica.Blocks.Interfaces.RealOutput temperaturaSensor(unit = "K") "Lo que lee el NTC del cuerpo";
  Modelica.Blocks.Interfaces.RealOutput temperaturaNucleoC(unit = "degC") "Metal";
  Modelica.Blocks.Interfaces.RealOutput temperaturaSensorC(unit = "degC") "Lo que lee el NTC del cuerpo";
  Modelica.Blocks.Interfaces.RealOutput temperaturaAguaSalidaC(unit = "degC") "Nodo superior, el que sale al grupo";
  Modelica.Blocks.Interfaces.RealOutput temperaturaElementoC(unit = "degC");
  Modelica.Blocks.Interfaces.RealOutput potenciaTermica(unit = "W");
  Modelica.Blocks.Interfaces.RealOutput potenciaExtraidaAgua(unit = "W") "Entalpía que se lleva el agua que sale";

  SI.Temperature T_elemento(start = T_inicial + P_nominal*demandaInicial/G_elementoMetal, fixed = true);
  SI.Temperature T_metal(start = T_inicial, fixed = true);
  SI.Temperature T_aguaSup(start = T_inicial, fixed = true);
  SI.Temperature T_aguaInf(start = T_inicial, fixed = true);
  SI.Temperature T_sensor(start = T_inicial, fixed = true);
  SI.MassFlowRate m_flow;
  Real demanda(unit = "1");
  Boolean bomba;
  SI.Temperature T_amb;
  SI.ThermalConductance G_aguaMetal;
  SI.HeatFlowRate Q_elementoMetal;
  SI.HeatFlowRate Q_aguaSup;
  SI.HeatFlowRate Q_aguaInf;
  SI.HeatFlowRate Q_ambiente;
equation
  if usarFlujo then
    m_flow = max(flujoMasico, 0);
  else
    m_flow = 0;
  end if;
  if usarDemanda then
    demanda = min(max(demandaCalentador, 0), 1);
  else
    demanda = if calentadorActivo then 1 else 0;
  end if;
  if usarEstadoBomba then
    bomba = bombaActiva;
  else
    bomba = m_flow > caudalMinimoBomba;
  end if;
  if usarAmbienteExterno then
    T_amb = temperaturaAmbiente;
  else
    T_amb = T_ambiente;
  end if;
  // Convección forzada con flujo y natural sin él (Bergman cap. 8 y 9).
  G_aguaMetal = if bomba then G_aguaForzada else G_aguaNatural;
  potenciaTermica = P_nominal*demanda;
  Q_elementoMetal = G_elementoMetal*(T_elemento - T_metal);
  // La conductancia de cada nodo de agua, en proporción a su volumen: no añade parámetros.
  Q_aguaSup = (1 - fraccionAguaInferior)*G_aguaMetal*(T_metal - T_aguaSup);
  Q_aguaInf = fraccionAguaInferior*G_aguaMetal*(T_metal - T_aguaInf);
  Q_ambiente = G_ambiente*(T_metal - T_amb)*(abs(T_metal - T_amb)/DeltaT_ref)^exponentePerdidas;
  C_elemento*der(T_elemento) = potenciaTermica - Q_elementoMetal;
  C_metal*der(T_metal) = Q_elementoMetal - Q_aguaSup - Q_aguaInf - Q_ambiente;
  // El agua del depósito entra abajo, sube y sale por arriba al grupo (Duffie y Beckman § 8.4).
  C_aguaSuperior*der(T_aguaSup) = Q_aguaSup + m_flow*cp_agua*(T_aguaInf - T_aguaSup);
  C_aguaInferior*der(T_aguaInf) = Q_aguaInf + m_flow*cp_agua*(T_aguaEntrada - T_aguaInf);
  tau_sensor*der(T_sensor) = T_metal - T_sensor;
  potenciaExtraidaAgua = m_flow*cp_agua*(T_aguaSup - T_aguaEntrada);
  temperaturaNucleo = T_metal;
  temperaturaSensor = T_sensor;
  temperaturaNucleoC = Modelica.Units.Conversions.to_degC(T_metal);
  temperaturaSensorC = Modelica.Units.Conversions.to_degC(T_sensor);
  temperaturaAguaSalidaC = Modelica.Units.Conversions.to_degC(T_aguaSup);
  temperaturaElementoC = Modelica.Units.Conversions.to_degC(T_elemento);
end CalderaTermicaEstratificada;
