# Diagramas oficiales

Los diagramas vigentes del núcleo EKO 3.0.0 son:

- `ontologia/ontologia-diagrama.html`, `.png` y `.svg`: diagrama general del
  núcleo de la ontología, con sus clases y relaciones principales;
- `ontologia/ontologia-diagrama-integral.drawio`, `.png` y `.svg`: mapa
  generado del esquema intensional completo, incluidas las relaciones
  obsoletas visibles únicamente para compatibilidad;
- `ontologia/recorrido-semantico-principal.drawio`, `.png` y `.svg`: recorrido
  que separa evidencia, base de conocimiento y agente generador;
- `caso-1/caso-1-diagrama.html`, `.png` y `.svg`: diagrama definitivo del caso 1 para
  `TechnicalDeviation`, `Diagnosis` y `ExplanationAssertion`.
- `caso-2/caso-2-diagrama.html`, `.png` y `.svg`: recorrido del caso 2 desde los dos baristas
  y el perfil compartido hasta `ObservedDifference`, las desviaciones térmicas,
  el diagnóstico y la explicación probable.
- `caso-4/caso-4-diagrama.html`, `.png` y `.svg`: secuencia temporal de tres extracciones y
  dos ajustes manuales de molienda; contrasta el primer cambio sin respuesta
  con el segundo cambio que recupera masa y tiempo dentro del perfil. Desde 2.3.0,
  esas pruebas se formalizan además mediante `targetsOutcome`,
  `evaluatesOutcomeOf` y categorías insuficiente y esperada en el grafo
  materializado.
- `caso-7/caso-7-diagrama.html`, `.png` y `.svg`: ruta de calibración con acidez excesiva,
  ajuste del tiempo 28→31 s y amargor posterior; compara acidez y amargor en
  ambas evaluaciones con puntuaciones sintéticas, distingue los hechos del
  diagnóstico probable y propone una prueba de confirmación. La segunda
  extracción evalúa explícitamente el ajuste, cuyo objetivo declarado es
  `ExcessiveAcidity`, y su resultado se clasifica como adverso sin afirmar que
  la acidez desapareció.
- `caso-8/caso-8-diagrama.html`, `.png` y `.svg`: secuencia mañana–tarde–corrección que
  relaciona humedad, condición de la cama, subextracción, acidez excesiva,
  ajuste de molino y evidencia posterior de recuperación.
- `caso-9/caso-9-diagrama.html`, `.png` y `.svg`: distingue la conformidad técnica de la
  aceptación sensorial cuando una extracción cumple las ocho especificaciones
  del perfil, pero el barista rechaza la calibración por un sabor desagradable
  persistente. El diagnóstico no inventa una desviación técnica ni una causa
  única y propone una nueva prueba de calibración controlada.
- `caso-10/caso-10-diagrama.html`, `.png` y `.svg`: contraste reversible de tres extracciones
  técnicamente conformes. La cama cambia uniforme–desigual–uniforme y el agrado
  8–4–8 mientras los controles permanecen iguales; esto sustenta la condición
  de la cama como origen probable del defecto sin afirmar causalidad universal.

El diagrama general incorpora conformidad técnica agregada y aceptación
sensorial explícita. Los diagramas de casos preservan sus rutas principales;
la matriz documental, PC22 y PC23 muestran la clasificación completa de cada
extracción sin convertir puntuaciones sensoriales en decisiones automáticas.
PC24 reconstruye la causa real de finalización, el tiempo máximo del perfil y
los datos externos introducidos manualmente para calcular el rendimiento. El
mapa general incluye `TerminationCause`, `recordedBy` y `recordingTime` sin
representar una parada por masa.

Los casos muestran `ProfileConformanceCriterion` para la comparación
determinista entre una observación y el perfil. Los diagnósticos y explicaciones
se atribuyen a `EvidenceBasedReasoner`; cuando corresponde, enlazan
independientemente heurísticas, procedimientos o patrones contextuales. No
dependen de condiciones causales creadas para el caso.

Las variantes de diseño anteriores del caso 1 fueron retiradas. Para este caso
solo deben utilizarse los archivos HTML, PNG y SVG ubicados en `caso-1/`.
