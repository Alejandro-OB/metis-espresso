within GemeloDigitalPreparacionCafe;
model RecorridoGrupoPortafiltro "Agua de la caldera al café: pared del grupo y el portafiltro montado, con tope de ebullición (ajustado el 2026-10-18)"
  // Paso del agua desde el nodo superior de CalderaTermicaEstratificada hasta la cavidad
  // del portafiltro montado, donde cae sobre el café. Ajustado con los usos de agua del
  // 2026-10-18 (curva caldera → agua sobre el café,
  // docs/engineering/evidencias/temperatura-cierre-2026-10-17.md); método,
  // partición, comprobación y lo que no reproduce en
  // model/Documentacion/agua-portafiltro.md; réplica, ajuste y verificación en
  // model/agua_portafiltro/.
  //
  // En archivo propio, como CalderaTermicaEstratificada y CircuitoHidraulicoMedido: la
  // huella de package.mo está en model-registry-v1.json y la plataforma la verifica.
  //
  // Lo que el modelo es:
  //   pared:   τ_g·dT_g/dt = α·T_metal + (1 − α)·T_amb − T_g
  //            El grupo y el portafiltro como una pared cuya temperatura sigue, con
  //            retardo de primer orden, a una mezcla fija del metal de la caldera y el
  //            cuarto. Es la capacitancia concentrada de Bergman et al., Fundamentals of
  //            Heat and Mass Transfer, 7.ª ed., 2011, § 5.2, con C_g/(G_mg + G_ga) = τ_g y
  //            G_mg/(G_mg + G_ga) = α. El calor que el agua le deja a la pared no aparece:
  //            con los datos del 18 no se distingue C_g = 300 J/K de C_g infinito (perfil
  //            en agua_portafiltro/ajuste.txt), y se toma el límite.
  //   agua:    T_salida = T_g + (T_caldera − T_g)·exp(−UA/(ṁ·c_p))
  //            Intercambiador con pared a temperatura uniforme, método ε–NTU con C_r = 0
  //            (Bergman et al., § 11.4, ec. 11.35a; § 8.3.3, ec. 8.42b).
  //   tope:    T_salida ≤ T_ebullicion. La cavidad está abierta al aire; a la presión de
  //            Popayán el agua hierve a 94,2 ± 0,1 °C (guion de puesta en servicio, 0.8), y
  //            lo que llegue por encima se va en vapor. Se decidió incluirlo antes de rehacer
  //            el ajuste con la potencia por ventana corregida, pero después de ver la meseta
  //            de los máximos a 100 °C en la primera versión: ver el informe.
  //   NTC:     τ_n·dT_n/dt = T_salida − T_n con flujo; sin flujo se congela. Representa el
  //            NTC-3 de la cavidad con el que se ajustó; τ_n recoge también el tránsito del
  //            agua. No es parte del agua: es para comparar con registros.
  //
  // Interfaz: temperaturaAguaCaldera es el nodo superior de la caldera (su
  // temperaturaAguaSalidaC pasada a K), temperaturaMetal su temperaturaNucleo y
  // flujoMasico el mismo caudal que entra a la caldera.
  //
  // Acople en un sentido: el calor que la pared toma del metal no se resta de la caldera.
  // La caldera de la 10.2 se ajustó con el grupo puesto y ese calor ya está en su
  // G_ambiente.
  //
  // No modela: el agua de la caldera fuera de equilibrio con el metal al pulsar (lo pone
  // la caldera, no esta clase: es el error dominante en el primer uso tras cambiar la
  // referencia), el vapor ni su masa, la canastilla ni el café (se ajustó sin canastilla,
  // con la bomba al tope y unos 3,2 g/s), la lectura del NTC entre usos (el aire de la
  // cavidad) y la dependencia de UA con el caudal (se ajustó con un solo caudal).
  import SI = Modelica.Units.SI;

  parameter SI.Time tau_grupo = 32.26 "Ajustado (2026-10-18, Pinf_eb); poco determinado: σ relativa 1,2";
  parameter Real fraccionMetal(min = 0, max = 1) = 0.7499 "α: peso del metal en el equilibrio de la pared; ajustado (σ relativa 0,08)";
  parameter SI.ThermalConductance UA = 2.791 "Intercambio agua–pared en el recorrido; ajustado (σ relativa 0,29, correlación 0,99 con α)";
  parameter SI.Time tau_NTC = 1.659 "NTC-3 de la cavidad más el tránsito del agua; ajustado (σ relativa 0,11)";
  parameter SI.SpecificHeatCapacity cp_agua = 4186 "Agua a 20–90 °C (Bergman, tabla A.6), el de la caldera";
  parameter Boolean limitarEbullicion = true "Tope de la salida en la ebullición local";
  parameter SI.Temperature T_ebullicion = 367.35 "94,2 °C: ebullición en Popayán, 819–821 hPa (guion 0.8)";
  parameter SI.MassFlowRate caudalMinimo = 1e-6 "Por debajo no hay agua pasando";
  parameter Boolean usarAmbienteExterno = false "true: ambiente desde la entrada temperaturaAmbiente";
  parameter SI.Temperature T_ambiente = 300.15 "27 °C: la sonda R de la tarde del 18, cuando no se usa la entrada";
  parameter Boolean paredEnEquilibrio = true "true: la pared arranca en el equilibrio con el metal y el ambiente";
  parameter SI.Temperature T_paredInicial = 354.15 "Pared al empezar si paredEnEquilibrio = false";
  parameter SI.Temperature T_NTCInicial = 354.15 "Lectura del NTC al empezar";

  Modelica.Blocks.Interfaces.RealInput temperaturaAguaCaldera(unit = "K") "Agua del nodo superior de la caldera";
  Modelica.Blocks.Interfaces.RealInput temperaturaMetal(unit = "K") "Metal de la caldera";
  Modelica.Blocks.Interfaces.RealInput flujoMasico(unit = "kg/s") "Caudal que atraviesa el recorrido";
  Modelica.Blocks.Interfaces.RealInput temperaturaAmbiente(unit = "K") if usarAmbienteExterno "Cuarto";
  Modelica.Blocks.Interfaces.RealOutput temperaturaAguaCafe(unit = "K") "Agua que cae sobre el café; sin flujo, la pared";
  Modelica.Blocks.Interfaces.RealOutput temperaturaAguaCafeC(unit = "degC") "Agua que cae sobre el café; sin flujo, la pared";
  Modelica.Blocks.Interfaces.RealOutput temperaturaParedC(unit = "degC") "Grupo y portafiltro";
  Modelica.Blocks.Interfaces.RealOutput temperaturaNTCC(unit = "degC") "Lo que leería el NTC-3 de la cavidad";
  Modelica.Blocks.Interfaces.BooleanOutput hayAgua "Hay agua pasando";

  SI.Temperature T_pared(start = T_paredInicial);
  SI.Temperature T_NTC(start = T_NTCInicial, fixed = true);
  SI.Temperature T_amb;
  SI.Temperature T_refPared "Equilibrio de la pared";
  SI.Temperature T_sinTope "Salida antes del tope";
  Real efectividad(unit = "1") "ε = 1 − exp(−UA/(ṁ·c_p))";
initial equation
  if paredEnEquilibrio then
    T_pared = T_refPared;
  else
    T_pared = T_paredInicial;
  end if;
equation
  if usarAmbienteExterno then
    T_amb = temperaturaAmbiente;
  else
    T_amb = T_ambiente;
  end if;
  hayAgua = flujoMasico > caudalMinimo;
  efectividad = if hayAgua then 1 - exp(-UA/(max(flujoMasico, caudalMinimo)*cp_agua)) else 0;
  T_refPared = fraccionMetal*temperaturaMetal + (1 - fraccionMetal)*T_amb;
  tau_grupo*der(T_pared) = T_refPared - T_pared;
  T_sinTope = T_pared + (temperaturaAguaCaldera - T_pared)*(1 - efectividad);
  temperaturaAguaCafe = if not hayAgua then T_pared elseif limitarEbullicion then min(T_sinTope, T_ebullicion) else T_sinTope;
  tau_NTC*der(T_NTC) = if hayAgua then temperaturaAguaCafe - T_NTC else 0;
  temperaturaAguaCafeC = Modelica.Units.Conversions.to_degC(temperaturaAguaCafe);
  temperaturaParedC = Modelica.Units.Conversions.to_degC(T_pared);
  temperaturaNTCC = Modelica.Units.Conversions.to_degC(T_NTC);
end RecorridoGrupoPortafiltro;
