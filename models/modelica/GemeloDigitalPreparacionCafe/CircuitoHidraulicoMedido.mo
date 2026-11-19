within GemeloDigitalPreparacionCafe;
model CircuitoHidraulicoMedido "Bomba, circuito y canastilla con parámetros ajustados a E1, E2 y E3 (paso 10.4)"
  // Paso 10.4 del guion de puesta en servicio: cambios 1 a 3 de modelado-presion.md § 5
  // con lo que mostraron E1, E2 y E3, y el cambio 4 reducido a una resistencia
  // constante del lecho. Método, partición, ajuste y comprobación en
  // models/modelica/Documentacion/modelo-presion-10.4.md; réplica en Python y ajuste en
  // models/modelica/modelo_presion/.
  //
  // Sustituye, para la presión, a BombaVibratoria + TramoTuberia + resistenciaCafe
  // constante: la recta presionMaxima·factorTension y los 9 bar de referencia de
  // aquella bomba no salían de ninguna medición. No las reemplaza en package.mo, por la
  // misma razón que CalderaTermicaEstratificada vive en su propio archivo: package.mo
  // tiene su huella en model-registry-v1.json y la plataforma la verifica. La FMU
  // publicada sigue con la bomba antigua hasta que la 10.6 la reexporte.
  //
  // Lo que el modelo es (unidades de trabajo: bar, g/s, g):
  //   bomba:        q_b = g(d)·(V/V_ref)^n·(1 − P/P_max), g en tabla sobre la densidad
  //                 PSM d (cambio 2: «la característica medida, interpolada de una
  //                 tabla»). Por encima de d = 0,75 no extrapola: es el techo del acta y
  //                 nada se midió por encima.
  //   restricción:  P = A + B·q + C·q², mezcla de la curva sin canastilla y la de la
  //                 configuración según la fracción llenada x (abajo).
  //   llenado:      x = 1/(1 + exp(−(m − V_ll)/w)), m la masa que entró desde la
  //                 pulsación. Es la forma que tomó el cambio 1 con los datos: la subida
  //                 lenta de la presión es el llenado de la canastilla, no la
  //                 compliancia del circuito.
  //   compliancia:  C_h·dP/dt = q_b − q. C_h no se midió: la subida rápida queda bajo
  //                 0,2 s, oculta por el filtro analógico y el periodo de registro. Se
  //                 fija para una constante de tiempo de 0,05 s y se declara provisional.
  //   viscosidad:   el término lineal se escala con μ(T)/μ(T_cal), IAPWS-IF97 de la
  //                 Modelica Standard Library (cambio 3). Entra por la física y no está
  //                 validado: todas las corridas se tomaron en la misma banda.
  //
  // No modela: la descarga tras el corte ni la presión atrapada por la válvula (la
  // simulación arranca de la presión previa), el caudal hacia la taza a densidades
  // bajas (el llenado no se completa y la taza no recibe nada, frente a 0,2–0,7 g/s
  // medidos), la pulsación de la bomba (solo su media) ni R(t) variable del lecho (9.6:
  // con canastilla presurizada no se distingue).
  import SI = Modelica.Units.SI;

  // Bomba: ajuste a los regímenes de calibración de E2 y E3 (parametros.json).
  parameter Real nodosDensidad[5] = {0, 0.05, 0.20, 0.50, 0.75} "Densidad PSM de los nodos de la tabla";
  parameter Real gTabla[5](each unit = "g/s") = {0, 0.2840, 0.9396, 3.6262, 5.3681} "Caudal a presión nula en cada nodo, ajustado";
  parameter Real presionMaximaBar(unit = "bar") = 17.231 "Presión a caudal nulo, ajustada";
  parameter Real exponenteTension = 1.647 "Efecto de la tensión de red, ajustado con las sesiones a 120,7, 121,5 y 123,5 V";
  parameter SI.Voltage tensionReferencia = 120.7 "Red medida en la sesión 1 de E2";

  // Restricciones: P = a + b·q + c·q² con los regímenes de calibración (b, c ≥ 0).
  parameter Real curvaSinCanastilla[3] = {5.6686, 0.33875, 0} "Portafiltro sin canastilla: a (bar), b (bar·s/g), c (bar·s²/g²)";
  parameter Real curva[3] = {5.4439, 0.99070, 0.17603} "Configuración simulada; por omisión, dos tazas vacía";
  parameter Real resistenciaLecho(unit = "bar.s/g") = 0 "Resistencia constante del lecho; 0,1503 con café, ajustada en E3";
  parameter Boolean conCanastilla = true "false: portafiltro sin canastilla, sin llenado";
  parameter Real volumenLlenado(unit = "g") = 40.14 "Masa que entra antes de que actúe la restricción; 40,14 dos tazas, 25,10 una taza, 25,99 café";
  parameter Real anchoLlenado(unit = "g") = 7.341 "Ancho de la transición; 7,341 dos tazas, 3,212 una taza, 1,946 café";

  // Viscosidad: T_cal es la media del metal en las ventanas de régimen de calibración.
  parameter SI.Temperature temperaturaCalibracion = 273.15 + 86.15 "Metal durante los regímenes de calibración (79–93 °C)";
  parameter SI.Pressure presionPropiedades = 9e5 "Presión para evaluar μ; su efecto sobre μ es despreciable";

  parameter Real constanteTiempo(unit = "s") = 0.05 "Provisional: los datos solo acotan la subida rápida bajo 0,2 s";
  parameter Real presionInicialBar(unit = "bar") = 5.6 "Presión previa a la pulsación";

  Modelica.Blocks.Interfaces.RealInput densidad "Densidad PSM de la bomba, 0–1";
  Modelica.Blocks.Interfaces.RealInput tensionRed(unit = "V");
  Modelica.Blocks.Interfaces.RealInput temperaturaAgua(unit = "K") "Agua que atraviesa la canastilla";
  Modelica.Blocks.Interfaces.RealOutput presionBar(unit = "bar") "Donde está el transductor, entre la bomba y la caldera";
  Modelica.Blocks.Interfaces.RealOutput caudalTazaGs(unit = "g/s");
  Modelica.Blocks.Interfaces.RealOutput presion(unit = "Pa");
  Modelica.Blocks.Interfaces.RealOutput caudalMasico(unit = "kg/s") "Hacia la taza";

  Real P(unit = "bar", start = presionInicialBar, fixed = true);
  Real masaEntrada(unit = "g", start = 0, fixed = true) "Masa que pasó la restricción desde la pulsación";
  Real caudalBomba(unit = "g/s");
  Real caudalRestriccion(unit = "g/s");
  Real fraccionLlenada;
  Real factorViscosidad;
  Real gDensidad(unit = "g/s");
  // A en bar, B en bar·s/g, C en bar·s²/g²; sin atributo unit porque las curvas son
  // vectores de coeficientes de unidades distintas.
  Real A;
  Real B;
  Real C;
protected
  final parameter Real bConfig = curva[2] + resistenciaLecho;
  final parameter Real complianciaGBar(unit = "g/bar") = constanteTiempo/(bConfig + 2*curva[3]*2.0) "τ por la conductancia a 2 g/s";
  final parameter SI.DynamicViscosity viscosidadCalibracion = Modelica.Media.Water.StandardWater.dynamicViscosity(
    Modelica.Media.Water.StandardWater.setState_pT(presionPropiedades, temperaturaCalibracion));
equation
  // IF97 no está definida bajo 0 °C. El tope solo protege el arranque: omc evalúa esta
  // función antes de que una tabla de entradas entregue su primer valor.
  factorViscosidad = Modelica.Media.Water.StandardWater.dynamicViscosity(
    Modelica.Media.Water.StandardWater.setState_pT(presionPropiedades, max(temperaturaAgua, 273.16)))/viscosidadCalibracion;
  gDensidad = if densidad <= 0 then 0 else Modelica.Math.Vectors.interpolate(nodosDensidad, gTabla, min(densidad, nodosDensidad[end]));
  caudalBomba = max(gDensidad*(tensionRed/tensionReferencia)^exponenteTension*(1 - P/presionMaximaBar), 0);
  fraccionLlenada = if conCanastilla then 1/(1 + exp(-max(min((masaEntrada - volumenLlenado)/anchoLlenado, 50), -50))) else 1;
  A = (1 - fraccionLlenada)*curvaSinCanastilla[1] + fraccionLlenada*curva[1];
  B = factorViscosidad*((1 - fraccionLlenada)*curvaSinCanastilla[2] + fraccionLlenada*bConfig);
  C = (1 - fraccionLlenada)*curvaSinCanastilla[3] + fraccionLlenada*curva[3];
  // Forma estable cuando C tiende a cero; bajo A la válvula no deja pasar nada.
  caudalRestriccion = if P <= A then 0 else 2*(P - A)/(B + sqrt(B^2 + 4*C*(P - A)));
  complianciaGBar*der(P) = caudalBomba - caudalRestriccion;
  der(masaEntrada) = caudalRestriccion;
  caudalTazaGs = fraccionLlenada*caudalRestriccion;
  presionBar = P;
  presion = P*1e5;
  caudalMasico = caudalTazaGs/1000;
end CircuitoHidraulicoMedido;
