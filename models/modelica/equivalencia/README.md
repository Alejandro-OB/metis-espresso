# Equivalencia entre arquitecturas

La FMU del modelo se exporta en x86_64 y se recompila para la Raspberry Pi 5
(aarch64). Esta carpeta guarda las series de referencia generadas en x86_64 y la
comparación muestra a muestra que acredita que el artefacto recompilado ejecuta
el mismo modelo.

## Contenido

| Archivo | Contenido |
|---|---|
| `referencia-x86_64.json` | Serie de referencia del modelo `espresso-detailed` 1.0.0 |
| `referencia-v2-x86_64.json` | Serie de referencia de la versión 2.0.0 |
| `referencia-v21-x86_64.json` | Serie de referencia de la versión 2.1.0, la canónica del registro |
| `FmuAdapterV2-aarch64.procedencia.json`, `FmuAdapterV21-aarch64.procedencia.json` | Con qué se recompiló cada artefacto aarch64: compilador, CMake, SUNDIALS y sumas SHA-256 de origen y resultado |
| `equivalencia-v2-aarch64.json`, `equivalencia-v21-aarch64.json` | Resultado de la comparación |
| `comprobar-equivalencia.py` | Repite la ejecución con un artefacto y la compara con una referencia |

## Condiciones de la referencia

Las tres referencias se ejecutaron igual:

| Dato | Valor |
|---|---|
| Duración simulada | 30,0 s |
| Paso de comunicación | 0,1 s |
| Muestras | 300 |
| Perfil | objetivo 93,0 °C y 9,0 bar; tolerancias 0,5 °C y 0,5 bar |
| Controlador | `profile-tracking-v1` |
| Condiciones iniciales | ninguna declarada (arranque en frío del modelo) |

La ejecución es determinista: dos corridas en el mismo equipo dan series
idénticas muestra a muestra. En las referencias de las versiones 2.0.0 y 2.1.0
la bomba no llega a actuar en esos 30 s partiendo en frío, así que la presión
solo se compara en reposo.

## Resultado

| Versión | Artefacto x86_64 | Artefacto aarch64 | Temperatura | Presión | Veredicto |
|---|---|---|---|---|---|
| 2.0.0 | `dist/FmuAdapterV2.fmu` | `dist/FmuAdapterV2-aarch64.fmu` | rmse 3·10⁻¹⁴ °C | sin diferencia | equivalente |
| 2.1.0 | `dist/FmuAdapterV21.fmu` | `dist/FmuAdapterV21-aarch64.fmu` | sin diferencia | sin diferencia | equivalente |

Los artefactos aarch64 se compilaron con gcc 14.2.0, CMake 3.31.6 y SUNDIALS
7.8.0, con `models/modelica/entorno-construccion/recompilar-artefacto.sh`.
Ese guion solo compila el C que la FMU trae dentro; no reexporta el modelo. La
variable `SUNDIALS_PREFIX` permite usarlo sobre el sistema, fuera del
contenedor.

## Umbrales

`comprobar-equivalencia.py` exige, por canal:

| Canal | rmse máximo | Diferencia máxima |
|---|---|---|
| `temperatureC` | 0,025 °C | 0,05 °C |
| `pressureBar` | 0,01 bar | 0,02 bar |

## Repetir la comprobación

Desde la raíz del repositorio, con un entorno que tenga FMPy (por ejemplo el del
backend):

```bash
PYTHONPATH=packages python models/modelica/equivalencia/comprobar-equivalencia.py \
  models/modelica/dist/FmuAdapterV21-aarch64.fmu \
  models/modelica/equivalencia/referencia-v21-x86_64.json \
  [salida.json]
```

El artefacto aarch64 solo se ejecuta en aarch64. Termina con código 0 si el
artefacto es equivalente y 1 si no.
