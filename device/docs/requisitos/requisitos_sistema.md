# Requisitos iniciales del sistema

**Proyecto:** Sistema de adquisición y análisis de datos basado en gemelos digitales para el proceso de preparación de café espresso  
**Estado documental:** Histórico. Requisitos iniciales del anteproyecto (2026-09-09). Los requisitos vigentes del sistema son los de la Tabla 4.4 de la monografía; el modelo de aprendizaje quedó fuera del alcance.
**Fecha:** 2026-09-09
**Fuente principal:** Contexto del anteproyecto y `Anteproyecto.pdf`
**Alcance:** funcionamiento mínimo necesario para construir y evaluar el prototipo

## Propósito

Este documento define las capacidades indispensables del sistema. Rige la
arquitectura, la implementación y las pruebas. Los documentos de arquitectura
detallan cómo se cumplen estos requisitos; los ADR pueden fijar decisiones de
diseño, pero no crear nuevas funciones sin actualizar esta línea base.

El sistema debe adquirir datos de una preparación espresso, regular las
variables críticas durante la operación, conservar la evidencia, comparar el
proceso físico con una representación virtual y relacionar los resultados
técnicos con la evaluación sensorial del barista.

## Alcance mínimo

El prototipo debe:

- adquirir temperatura, presión y tiempo;
- permitir el inicio físico de una preparación por el barista;
- regular temperatura y presión durante la preparación;
- detener la preparación por solicitud del barista, tiempo máximo o seguridad;
- registrar las mediciones y eventos;
- aceptar TDS, dosis y masa de bebida medidos externamente;
- calcular rendimiento y métricas de estabilidad;
- conservar los datos localmente y sincronizarlos cuando exista un servicio externo;
- mantener una representación virtual y comparar sus resultados con los datos reales;
- registrar evaluación sensorial;
- registrar las características disponibles del café;
- consultar y comparar preparaciones anteriores;
- aprender relaciones contextuales entre características del café, condiciones de extracción y evaluación sensorial;
- producir diagnósticos o recomendaciones trazables para revisión humana.

## Fuera del alcance mínimo

No son necesarios para aprobar el funcionamiento básico del prototipo:

- inicio de la extracción desde la web, API, temporizador o modelo;
- automatización del molino o de la preparación completa;
- parada por TDS, rendimiento, evaluación sensorial o recomendación;
- medición embebida de masa o TDS;
- evaluación sensorial automática;
- aprendizaje automático ejecutado dentro de la ESP32-S3 o dentro del lazo de control;
- operación dependiente de Internet;
- modelado virtual de vapor y purga dentro del gemelo digital;
- función de vapor o espumado en la máquina (retirada el 2026-10-06; ver más abajo);
- operación comercial o certificación industrial.

Los filtros históricos complejos, las comparaciones múltiples, las
visualizaciones avanzadas, la exportación en múltiples formatos y el modelado
del vapor o la purga pueden incorporarse como extensiones después de validar
esta línea base. El funcionamiento físico local de la purga puede
implementarse como apoyo operativo, pero no forma parte del modelo virtual de la
extracción inicial.

**Revisión del 2026-10-06 — el prototipo no tiene función de vapor.** Se retira
para simplificarlo: el gemelo modela solo temperatura y presión de extracción, y
el vapor exigía una referencia térmica propia, un límite duro calculado sobre
ella y un retorno térmico que nada de lo evaluado necesita. La purga se conserva
como apoyo operativo. Los dos pulsadores que el mando dedicaba al vapor quedan
sin función hasta que se decida otra (ADR-006). El aprendizaje automático sí pertenece a la evaluación final,
aunque su entrenamiento ocurra después de reunir datos suficientes.

## Actores y autoridad

| Actor | Responsabilidad |
|---|---|
| Barista | Verifica precondiciones, inicia y detiene físicamente la preparación, aporta mediciones externas, evalúa la bebida y decide sobre una recomendación. |
| Investigador | Define el protocolo, consulta los datos y evalúa el sistema, el gemelo y los indicadores. |
| Controlador local | Adquiere, regula, supervisa seguridad, registra y mantiene la operación sin depender de servicios externos. |
| Servicios externos | Conservan el historial completo, ejecutan análisis, semántica y recomendaciones fuera del lazo de control. |

Solo el barista puede iniciar una preparación. Ningún servicio externo, modelo,
interfaz o recomendación puede actuar directamente sobre la cafetera.

## Requisitos funcionales

Los siguientes 29 requisitos constituyen el catálogo funcional mínimo.

| ID | Requisito | Prioridad | Verificación |
|---|---|---|---|
| **RF-001** | El sistema debe conservar una configuración efectiva con referencia de temperatura, referencia de presión y tiempo máximo obligatorio. | Obligatorio | Prueba e inspección |
| **RF-002** | El sistema debe validar las unidades, el formato, la coherencia y los límites de una configuración antes de activarla. | Obligatorio | Prueba |
| **RF-003** | El sistema debe iniciar una preparación cuando el barista realice una acción física deliberada sobre el mando de la cafetera. | Obligatorio | Prueba |
| **RF-004** | Durante una preparación activa, el sistema debe permitir que el barista solicite una parada normal mediante el mando físico de la cafetera y, al recibirla, debe detener la bomba. | Obligatorio | Prueba |
| **RF-005** | Durante una preparación activa, el sistema debe detener automáticamente la bomba cuando el tiempo transcurrido alcance el tiempo máximo configurado. | Obligatorio | Prueba |
| **RF-006** | Al finalizar una preparación, el sistema debe cerrar su registro e identificar la causa de finalización —parada normal, tiempo máximo o protección—, el origen de la solicitud cuando corresponda y el instante de finalización. | Obligatorio | Prueba e inspección |
| **RF-007** | El sistema debe adquirir temperatura, presión y tiempo con una base temporal común, unidad y estado de calidad para cada medición. | Obligatorio | Prueba e inspección |
| **RF-008** | El sistema debe validar, filtrar y marcar como no aptas las señales desconectadas, saturadas, imposibles o desactualizadas antes de utilizarlas para control o análisis. | Obligatorio | Prueba |
| **RF-009** | Durante calentamiento y preparación, el sistema debe regular la temperatura alrededor de la referencia efectiva autorizada. | Obligatorio | Prueba experimental |
| **RF-010** | Después de un inicio físico válido, el sistema debe regular la presión alrededor de la referencia efectiva hasta la finalización de la preparación o la actuación de una protección. | Obligatorio | Prueba experimental |
| **RF-011** | El sistema debe informar al barista cuando detecte sobretemperatura, sensor crítico inválido, tarea crítica detenida o condición insegura. | Obligatorio | Prueba e inspección |
| **RF-012** | Ante una condición insegura, el sistema debe inhibir las salidas peligrosas y registrar el evento. | Obligatorio | Prueba e inspección |
| **RF-013** | El sistema debe ofrecer una aplicación web operativa en la plataforma externa local para consultar estado, alarmas, tiempo, mediciones y referencias. | Obligatorio | Demostración |
| **RF-014** | El sistema debe crear un registro único por preparación que relacione configuración, mediciones, eventos, causa de finalización, versiones relevantes y responsable. | Obligatorio | Prueba e inspección |
| **RF-015** | El sistema debe conservar localmente la configuración, calibraciones, estado de recuperación y una cola limitada de registros pendientes, y debe recuperar los registros confirmados después de un reinicio. | Obligatorio | Prueba |
| **RF-016** | Cuando exista conectividad, el sistema debe sincronizar los registros pendientes con el servicio externo sin duplicarlos. | Obligatorio | Prueba |
| **RF-017** | El sistema debe permitir registrar dosis, masa final de bebida y TDS medidos externamente. | Obligatorio | Prueba e inspección |
| **RF-018** | El sistema debe calcular el rendimiento de extracción cuando estén disponibles la dosis, la masa final de bebida y el TDS. | Obligatorio | Análisis y prueba |
| **RF-019** | El sistema debe calcular indicadores reproducibles de estabilidad térmica y estabilidad de presión. | Obligatorio | Análisis y prueba |
| **RF-020** | El sistema debe comparar los valores medidos con los valores objetivo y simulados, identificar las desviaciones y conservar sus valores, tolerancias, instantes y procedencia. | Obligatorio | Análisis e inspección |
| **RF-021** | El sistema debe mantener una representación virtual de la preparación a partir de las condiciones y mediciones válidas. | Obligatorio | Demostración y análisis |
| **RF-022** | El sistema debe relacionar preparación, mediciones, indicadores, evaluación y evidencia en el componente semántico. | Obligatorio | Demostración e inspección |
| **RF-023** | El sistema debe producir análisis, hipótesis o recomendaciones trazables que distingan una desviación observada de una causa demostrada. | Obligatorio | Demostración e inspección |
| **RF-024** | El sistema debe registrar una evaluación sensorial del barista vinculada con la preparación, identificando evaluador, escala, atributos, valores, fecha y observaciones. | Obligatorio | Prueba e inspección |
| **RF-025** | El sistema debe utilizar un modelo de aprendizaje externo para estudiar relaciones entre características del café, condiciones de extracción, desviaciones técnicas y evaluación sensorial. El modelo debe producir predicciones o asociaciones contextualizadas, declarar incertidumbre y abstenerse cuando no existan datos comparables suficientes. | Obligatorio para la evaluación final | Análisis experimental |
| **RF-026** | El sistema debe permitir registrar las características disponibles del café asociado a una preparación, como variedad, proceso poscosecha, nivel de tueste, lote, fecha de tueste y origen. Debe conservar explícitamente los datos que no estén disponibles. | Obligatorio | Prueba e inspección |
| **RF-027** | El sistema debe permitir consultar y comparar preparaciones anteriores mediante sus mediciones, referencias, indicadores, características del café y evaluaciones sensoriales disponibles. | Obligatorio | Demostración |
| **RF-028** | El sistema debe actualizar y ejecutar la representación virtual con las condiciones y mediciones válidas de una preparación, y conservar los resultados simulados vinculados con sus entradas y versión. | Obligatorio | Demostración y análisis |
| **RF-029** | El sistema debe registrar la decisión del barista sobre cada recomendación: aceptada, rechazada o modificada, junto con la justificación cuando corresponda. | Obligatorio | Prueba e inspección |
| **RF-030** | El sistema debe sostener el objetivo de temperatura activado por el barista subiendo la referencia de la caldera antes del primer uso tras un reposo, según una regla versionada fijada por los autores con lo medido y acotada por el máximo del acta de temperatura, sin modificar el objetivo ni cambiar lo pedido durante un uso; debe respetar la exclusión del barista y la suspensión del protocolo, y registrar cada cambio con el reposo, la configuración que regía, la versión de la regla y su motivo (ADR-018 §§ 12 y 13, revisado el 2026-10-22). | Obligatorio | Prueba e inspección |

## Requisitos no funcionales

| ID | Requisito | Verificación |
|---|---|---|
| **RNF-001** | La adquisición, la regulación y la seguridad deben seguir funcionando aunque fallen la red, la interfaz o los servicios externos. | Prueba de carga |
| **RNF-002** | La bomba y el calentador deben quedar apagados en arranques, reinicios, actualizaciones, alarmas y fallos críticos. | Prueba |
| **RNF-003** | Antes de abrir o intervenir eléctricamente la cafetera, el operador debe retirar el enchufe. El botón del panel no desconecta la red. | Inspección del procedimiento |
| **RNF-004** | Cada medición debe indicar qué mide, su unidad, cuándo se tomó, qué calibración usa y si es confiable. | Inspección del esquema |
| **RNF-005** | La estabilidad de temperatura y presión debe evaluarse con tolerancias definidas y datos reales. | Análisis experimental |
| **RNF-006** | Una interrupción no debe dañar los registros confirmados; los registros incompletos deben quedar identificados. | Prueba |
| **RNF-007** | Cada resultado calculado debe indicar los datos, calibraciones y versiones de modelo que lo generaron. | Inspección |
| **RNF-008** | El sistema debe distinguir datos medidos, cálculos, diagnósticos, recomendaciones y decisiones del barista, sin presentar una asociación como causa comprobada. | Inspección y análisis |
| **RNF-009** | El modelo de aprendizaje debe registrar su versión, datos de entrenamiento, métricas, limitaciones y tipos de café en los que puede utilizarse. | Inspección y análisis |

El botón de encendido y apagado del panel es una función lógica y no desconecta
la red eléctrica. La desconexión para intervención se realiza retirando
manualmente el enchufe de la cafetera.

## Restricciones de frontera

| ID | Restricción |
|---|---|
| **RD-001** | La operación crítica debe ejecutarse localmente en la ESP32-S3 y no puede depender de Internet ni de un servidor externo. |
| **RD-002** | La aplicación web puede consultar datos y proponer configuraciones válidas, pero no iniciar extracciones ni escribir directamente sobre los actuadores. La API, los temporizadores, las recomendaciones, los servicios externos y la recuperación después de un reinicio tampoco pueden iniciar una preparación. La parada normal se solicita mediante el mando físico. |
| **RD-003** | La masa y el TDS se obtienen inicialmente mediante instrumentos externos y se ingresan manualmente. |
| **RD-004** | La ontología, el razonamiento, el gemelo digital y las recomendaciones permanecen fuera del lazo de control. |
| **RD-005** | Las series de alta frecuencia se conservan como datos de series; el grafo semántico almacena hechos, relaciones, resúmenes y procedencia, no cada muestra como un individuo. |

## Indicadores mínimos de evaluación

La evaluación del prototipo debe utilizar, como mínimo:

- disponibilidad y calidad de las mediciones de temperatura y presión;
- error y estabilidad de temperatura;
- error y estabilidad de presión;
- continuidad y recuperación de los registros;
- diferencia entre estado físico y estado virtual;
- TDS y rendimiento de extracción cuando se disponga de las mediciones;
- evaluación sensorial del barista.

Los valores numéricos, ventanas de cálculo, límites de seguridad y protocolo
sensorial se fijan en el protocolo experimental y no se duplican aquí.

## Trazabilidad con el anteproyecto

| Objetivo del anteproyecto | Requisitos |
|---|---|
| Modelar parámetros y variables del proceso con apoyo del barista | RF-001, RF-007, RF-017, RF-022, RF-024–RF-026 y RF-029 |
| Proponer la arquitectura de adquisición y análisis del gemelo digital | RF-003–RF-023 y RF-028, RD-001–RD-005 |
| Evaluar el desempeño y su efecto sobre la bebida | RF-017–RF-029 y RNF-004–RNF-009 |

## Orden de implementación

1. seguridad, máquina de estados y desconexión física;
2. adquisición, calibración y calidad de datos;
3. almacenamiento local e interfaz operativa;
4. regulación de temperatura y presión;
5. registro, sincronización e indicadores;
6. gemelo digital y comparación físico-virtual;
7. evaluación sensorial, semántica e historial;
8. construcción, validación y uso del modelo de aprendizaje;
9. registro de decisiones y evaluación de recomendaciones.

## Extensiones no normativas

Estas funciones pueden documentarse y planificarse aparte, pero no deben
contarse como requisitos del núcleo:

- perfiles de múltiples fases y transiciones avanzadas;
- modelado virtual de la purga;
- báscula integrada y caudalímetro;
- filtros históricos y comparaciones avanzadas;
- exportaciones adicionales;
- administración multiusuario y operación remota.

Una extensión solo pasa a la línea base cuando exista una justificación en el
anteproyecto o una decisión aprobada, junto con su criterio de verificación.

## Criterio de aprobación

Los identificadores de la línea base anterior de 48 RF, 17 RNF y 10 RD quedan
obsoletos. Las referencias que aún aparezcan en arquitecturas, ADR o planes
anteriores deben migrarse a esta línea base antes de declarar esos documentos
completamente alineados.

La especificación se considera aprobada cuando el prototipo puede ejecutar una
preparación iniciada físicamente por el barista, regular temperatura y presión,
detenerse de forma segura, conservar los datos, calcular los indicadores
mínimos, comparar el estado físico y virtual, registrar la evaluación sensorial
y producir una salida semántica o recomendación trazable sin actuación
autónoma, consultar el historial y conservar la decisión del barista. Para la
evaluación final, el modelo de aprendizaje debe haber sido evaluado con los datos
disponibles o declarar explícitamente que debe abstenerse por falta de evidencia
comparable.
