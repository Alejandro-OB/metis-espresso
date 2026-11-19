# ADR-011 — Ejecución del modelo de simulación en la plataforma externa

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Ámbito:** generación, registro y equivalencia numérica del modelo virtual cuando se ejecuta en una arquitectura distinta a la del equipo de desarrollo

## 1. Contexto

ADR-001 asigna a la plataforma externa la ejecución de Modelica y del modelo
virtual, fuera del lazo de control. La plataforma externa es candidata a
Raspberry Pi, de arquitectura ARM de 64 bits, mientras que el desarrollo y la
exportación del modelo ocurren en un equipo x86_64.

La auditoría de preparación ya había señalado que el artefacto del modelo
contiene únicamente una biblioteca para Linux x86_64. Esta decisión resuelve
qué hacer con esa incompatibilidad sin perder la trazabilidad que exige RNF-007,
según la cual cada resultado calculado debe declarar los datos, calibraciones y
versiones de modelo que lo generaron.

### 1.1 Evidencia medida

Comprobaciones realizadas el 2026-09-15 sobre el equipo de desarrollo (x86_64),
con el modelo canónico `espresso-detailed` v1.0.0:

| Comprobación | Resultado |
|---|---|
| Versión de la interfaz | FMI 2.0 |
| Bibliotecas incluidas | solo `binaries/linux64`, compilada para x86_64 |
| Fuentes incluidas en el artefacto | **263 archivos** de código C, con sus archivos de construcción |
| Ejecución de 30 s simulados con paso de 0,1 s | 300 muestras en 0,15 s y 0,03 s en dos corridas consecutivas, entre 195 y 869 veces más rápido que el tiempo real |
| Repetibilidad en el mismo equipo | series **idénticas muestra a muestra** |

Dos restricciones técnicas condicionan la decisión:

**El ejecutor resuelve la biblioteca en una única ruta.** Para FMI 2.0, FMPy
busca la biblioteca nativa en `binaries/linux64` con independencia de la
arquitectura del equipo: la denominación específica de ARM solo se emplea en
FMI 3. Por tanto un mismo artefacto no puede contener las dos versiones, y la
variante para la plataforma externa será **un archivo distinto, con otra suma de
verificación**.

**El registro admite un único artefacto por modelo.** El esquema
`model-registry.schema.json` declara `artifact` como objeto único y prohíbe
propiedades adicionales, y el registro comprueba la suma antes de simular. Un
artefacto recompilado haría fallar esa comprobación.

**El tiempo de ejecución tiene un límite operativo.** El trabajador de
simulaciones interrumpe cualquier ejecución que supere 120 s, mientras que la
duración simulada admitida llega a 900 s. En el equipo de desarrollo el margen
es amplio; en una plataforma más lenta ese límite puede convertirse en la
restricción efectiva.

## 2. Problema de decisión

Se debe definir cómo se produce el artefacto del modelo para la arquitectura de
la plataforma externa, cómo se registra sin romper la identidad del modelo ni la
procedencia de los resultados, y con qué criterio se acepta que el resultado
obtenido allí es el del mismo modelo.

## 3. Alternativas consideradas

### 3.1 Generación del artefacto

| Alternativa | Evaluación |
|---|---|
| Compilación cruzada desde el equipo de desarrollo | Evita depender de la plataforma externa, pero exige montar y mantener una cadena cruzada para producir un solo archivo. |
| Compilación en la plataforma externa, sobre el sistema operativo | Directa, pero deja sin registrar con qué compilador y opciones se generó el binario que produce los resultados. |
| Compilación en la plataforma externa, dentro de un entorno declarado en el repositorio | Repetible por terceros, con procedencia registrada, y no altera el sistema de la plataforma. **Seleccionada.** |
| Reexportar el modelo con OpenModelica en la plataforma externa | Innecesario: el artefacto ya incluye las fuentes, y exigiría instalar la herramienta completa de modelado en el equipo de operación. |

### 3.2 Registro del artefacto

| Alternativa | Evaluación |
|---|---|
| Registrar la variante como un modelo distinto | Rompe la identidad del modelo: cada simulación declara qué modelo la produjo, de modo que el historial quedaría dividido y comparar dos preparaciones simuladas en equipos distintos dejaría de ser directo. |
| Mantener un registro distinto por instalación | Evita tocar el contrato, pero convierte la procedencia en información local y no verificable desde el repositorio. |
| Declarar varios artefactos por modelo, uno por arquitectura | Conserva identificador, versión y contrato del modelo, y hace explícita la existencia de dos binarios equivalentes. Exige versionar el contrato. **Seleccionada.** |

### 3.3 Criterio de equivalencia

| Alternativa | Evaluación |
|---|---|
| Exigir resultados idénticos bit a bit | No es sostenible entre compiladores y arquitecturas distintas, y un incumplimiento no indicaría un problema real. |
| Aceptar la tolerancia del modelo (2,5 °C) | Insuficiente: una diferencia de ese orden permitiría que el veredicto de conformidad dependiera del equipo donde se simuló. |
| Exigir una diferencia dos órdenes de magnitud menor que la tolerancia del modelo | Verificable, y garantiza que la elección del equipo no altera ninguna conclusión. **Seleccionada.** |

## 4. Decisión

### 4.1 Generación

El artefacto para la plataforma externa se generará **recompilando las fuentes
incluidas en el propio artefacto**, dentro de un entorno de construcción
declarado en el repositorio y ejecutado en la plataforma externa. No se
instalará la herramienta de modelado en el equipo de operación.

El procedimiento debe registrar la versión del compilador y las opciones
empleadas, que pasan a formar parte de la procedencia del artefacto.

### 4.2 Registro

El contrato del registro de modelos se ampliará para que un modelo declare
**un artefacto por arquitectura**, conservando su identificador, su versión, su
contrato y su fuente. El registro seleccionará el artefacto correspondiente a la
arquitectura de ejecución y comprobará su suma antes de simular.

Cada resultado de simulación seguirá declarando el modelo, su versión y la suma
del artefacto **efectivamente utilizado**, de manera que la arquitectura de
ejecución quede registrada en la procedencia del resultado.

### 4.3 Equivalencia numérica

Se considerará que el artefacto recompilado ejecuta el mismo modelo cuando, al
repetir la ejecución de referencia conservada en
`models/modelica/equivalencia/`, la comparación muestra a muestra cumpla:

| Canal | Error cuadrático medio | Diferencia máxima absoluta |
|---|---|---|
| Temperatura | ≤ 0,025 °C | ≤ 0,05 °C |
| Presión | ≤ 0,01 bar | ≤ 0,02 bar |

Los umbrales de temperatura equivalen al 1 % y al 2 % de la tolerancia de modelo
de 2,5 °C que el sistema utiliza para comparar el proceso físico con el virtual.

Si la comparación no los cumple, el artefacto no se declarará equivalente y la
diferencia deberá explicarse antes de utilizarlo para producir evidencia.

## 5. Justificación

1. **La repetibilidad ya está comprobada.** Dos ejecuciones consecutivas en el
   mismo equipo producen series idénticas, de modo que cualquier diferencia
   observada en otra arquitectura es atribuible al binario y no a la ejecución.
2. **La identidad del modelo es parte de la evidencia.** RNF-007 exige que cada
   resultado declare la versión del modelo que lo generó; dividir el modelo en
   dos identificadores distintos degradaría el historial y las comparaciones.
3. **El umbral protege el veredicto.** Con diferencias cien veces menores que la
   tolerancia del modelo, ninguna conclusión sobre la conformidad de una
   preparación puede depender del equipo donde se ejecutó la simulación.
4. **La compilación declarada es reproducible.** El binario que produce la
   evidencia queda asociado a un procedimiento que un tercero puede repetir.
5. **No se traslada la herramienta de modelado al equipo de operación.** El
   artefacto ya contiene sus fuentes, de modo que la plataforma externa solo
   necesita un compilador de C.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- El modelo virtual puede ejecutarse en la plataforma externa conservando su
  identidad, su versión y su contrato.
- La procedencia de cada resultado indica en qué arquitectura se produjo.
- La equivalencia entre arquitecturas deja de ser una suposición y pasa a ser
  una comprobación con umbrales declarados.

### 6.2 Consecuencias desfavorables

- Se introduce un cambio de contrato en el registro de modelos, con su migración
  y la actualización del verificador de contratos.
- El proyecto mantiene dos artefactos por modelo, que deben regenerarse juntos
  cuando el modelo cambie.
- La comprobación de equivalencia añade un paso obligatorio antes de aceptar un
  artefacto nuevo.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Diferencias numéricas por optimizaciones del compilador | Registrar las opciones de compilación y comparar contra la serie de referencia antes de aceptar el artefacto. |
| Artefactos de arquitecturas distintas generados a partir de fuentes distintas | Regenerar ambos a partir del mismo artefacto de origen y conservar la suma de la fuente en el registro. |
| Ejecución más lenta que el límite de 120 s del trabajador | Medir la duración de las simulaciones largas en la plataforma externa y ajustar el límite de forma explícita si procede. |
| Pérdida de la serie de referencia | Conservada en el repositorio con su suma de verificación. |
| Confusión entre resultado simulado y medición física | Sin cambios: las observaciones simuladas conservan su origen declarado y no habilitan actuadores. |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| ~~Entorno de construcción del artefacto~~ | **Cerrado el 2026-09-15.** Declarado en `models/modelica/entorno-construccion/` y ejecutado en la plataforma: los 263 archivos de C compilan en menos de 15 s y producen un binario `aarch64` con su hoja de procedencia. El contenedor declarado no pudo emplearse allí por un fallo de red del motor de contenedores; la compilación se hizo con el mismo procedimiento sobre el sistema, con idénticas versiones de compilador y CMake. |
| Cambio del contrato del registro | Versionar el esquema, adaptar el lector del registro y el verificador de contratos. |
| ~~Comprobación de equivalencia~~ | **Cerrada el 2026-09-15.** Diferencia **exactamente cero** en ambos canales frente a la referencia, sobre 300 muestras. Descartado el falso positivo con un control: el artefacto de x86_64 no carga en la plataforma, de modo que las series comparadas las produjo el binario ARM. Evidencia en `device/tests/capacidad-plataforma-externa/`. |
| Límite de tiempo del trabajador | Medir simulaciones largas en la plataforma externa y decidir si 120 s sigue siendo adecuado. |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. generación del artefacto para la arquitectura de la plataforma externa a
   partir de las fuentes incluidas;
2. comprobación de la suma del artefacto seleccionado antes de simular;
3. ejecución de la serie de referencia en la plataforma externa y comparación
   muestra a muestra dentro de los umbrales de §4.3;
4. declaración de la arquitectura y de la suma del artefacto en la procedencia
   del resultado;
5. duración de una simulación larga frente al límite del trabajador;
6. ejecución del verificador de contratos con el registro ampliado.

## 10. Trazabilidad

- [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md): ejecución del
  modelo virtual en la plataforma externa.
- [ADR-010](ADR-010-residencia-y-memoria-del-almacen-rdf.md): decisión paralela
  sobre el almacén RDF en la misma plataforma.
- Alcance de producción de la plataforma externa.
- Plan de comparación físico-virtual:
  origen de la tolerancia de modelo de 2,5 °C.
- [Serie de referencia](../../../../models/modelica/equivalencia/README.md).
- Requisitos: RF-020, RF-028 y RNF-007.

## 11. Criterios de reconsideración

La decisión deberá revisarse si el modelo se exporta en una versión de la
interfaz que permita alojar varias arquitecturas en un mismo artefacto, si la
plataforma externa no alcanza los umbrales de equivalencia, o si el tiempo de
ejecución en esa plataforma obliga a replantear dónde se ejecuta la simulación.
