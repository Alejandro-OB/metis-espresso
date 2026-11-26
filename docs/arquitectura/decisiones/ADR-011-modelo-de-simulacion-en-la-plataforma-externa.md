# ADR-011 — Ejecución del modelo de simulación en la plataforma externa

**Estado:** Aceptada, 2026-09-21

## 1. Contexto

La plataforma externa, una Raspberry Pi ARM de 64 bits, ejecuta el modelo
virtual ([ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md)). El
modelo `espresso-detailed` v1.0.0 se exporta como FMU (FMI 2.0) en un equipo
x86_64 y trae solo `binaries/linux64` para x86_64, junto con sus 263 archivos de
código C. En FMI 2.0, FMPy busca la biblioteca en `binaries/linux64` en
cualquier arquitectura, así que la variante ARM es otro archivo con otra suma de
verificación. Cada resultado debe declarar la versión del modelo que
lo generó. Dos corridas en el mismo equipo dan series idénticas muestra a
muestra.

## 3. Alternativas consideradas

### 3.1 Generación del artefacto

| Alternativa | Evaluación |
|---|---|
| Compilación cruzada | Exige mantener una cadena cruzada para un solo archivo. Descartada. |
| Compilación en la plataforma, sobre el sistema | No registra compilador ni opciones. Descartada. |
| Reexportar con OpenModelica en la plataforma | Instalaría la herramienta de modelado en el equipo de operación sin necesidad. Descartada. |
| Compilación en la plataforma, en un entorno declarado | **Seleccionada.** |

### 3.2 Registro del artefacto

| Alternativa | Evaluación |
|---|---|
| La variante como modelo distinto | Divide el historial y complica comparar simulaciones. Descartada. |
| Un registro por instalación | La procedencia no sería verificable desde el repositorio. Descartada. |
| Un artefacto por arquitectura en el mismo modelo | **Seleccionada.** |

### 3.3 Criterio de equivalencia

| Alternativa | Evaluación |
|---|---|
| Identidad bit a bit | Insostenible entre compiladores y arquitecturas. Descartada. |
| La tolerancia del modelo, 2,5 °C | El veredicto de conformidad podría depender del equipo. Descartada. |
| Cien veces menos que la tolerancia | **Seleccionada.** |

## 4. Decisión

### 4.1 Generación

El artefacto ARM se genera recompilando las fuentes del propio artefacto en el
entorno declarado en
[`model/entorno-construccion/`](../../../model/entorno-construccion/), ejecutado
en la plataforma. El compilador y sus opciones forman parte de la procedencia.

### 4.2 Registro

El registro de modelos admite un artefacto por arquitectura
(`artifactsByArchitecture` en `model-registry.schema.json`) con el mismo
identificador, versión y contrato. Comprueba la suma del artefacto elegido antes
de simular, y cada resultado declara la suma del artefacto usado.

### 4.3 Equivalencia numérica

El artefacto recompilado ejecuta el mismo modelo si, al repetir la referencia de
[`model/equivalencia/`](../../../model/equivalencia/README.md), la comparación
muestra a muestra cumple:

| Canal | Error cuadrático medio | Diferencia máxima absoluta |
|---|---|---|
| Temperatura | ≤ 0,025 °C | ≤ 0,05 °C |
| Presión | ≤ 0,01 bar | ≤ 0,02 bar |

Los umbrales de temperatura son el 1 % y el 2 % de la tolerancia de 2,5 °C. Si no
se cumplen, el artefacto no se usa para producir evidencia.

## 5. Justificación

1. Como la ejecución es repetible, una diferencia en otra arquitectura se debe al
   binario.
2. Un solo identificador conserva el historial y las comparaciones.
3. Con ese umbral, ninguna conclusión de conformidad depende del equipo.
4. El binario queda asociado a un procedimiento que un tercero puede repetir.
5. El artefacto trae sus fuentes, así que la plataforma solo necesita un
   compilador de C.

## 6. Consecuencias

Cada modelo tiene dos artefactos, que se regeneran juntos desde el mismo origen,
y aceptar uno nuevo exige pasar la comprobación de equivalencia.

## 8. Estado de implementación

En la plataforma, el código C compila en menos de 15 s. Un fallo de red del motor
de contenedores obligó a compilar con el mismo procedimiento sobre el sistema,
con las mismas versiones de compilador y CMake. La diferencia frente a la
referencia fue cero en ambos canales sobre 300 muestras; el artefacto x86_64 no
carga en la plataforma, así que las series las produjo el binario ARM. Falta
comprobar si el límite de 120 s del trabajador de simulaciones basta para
duraciones simuladas de hasta 900 s.

## 11. Criterios de reconsideración

Revisar si una versión de FMI admite varias arquitecturas en un artefacto, si la
plataforma no alcanza los umbrales o si su tiempo de ejecución obliga a simular
en otro lugar.
