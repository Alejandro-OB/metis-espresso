# Planta reducida y contrato FMU

Modelo de prueba de integración, no el modelo físico de la cafetera (ese está en
[`models/modelica/`](../../../models/modelica/)).

`EkoCoffeeMachine.mo` es una planta de primer orden para temperatura y presión.
Sus parámetros sirven para probar la integración; **no se identificaron sobre la
cafetera** y no deben leerse como parámetros reales, límites de seguridad ni
ganancias de control. En el registro de modelos del servidor
(`models/modelica/model-registry-v1.json`) figura como `espresso-reduced-fixture`,
con papel de *fixture*.

## Contenido

| Archivo | Función |
|---|---|
| `EkoCoffeeMachine.mo` | Modelo Modelica reducido |
| `fmu-contract.json` | Interfaz: entradas `heaterCommand` y `pumpCommand` (0 a 1), salidas `temperatureC` y `pressureBar`, paso de comunicación de 0,1 s, autoridad `simulation-only` |
| `export_fmu.mos`, `export_fmu.sh` | Exportación a FMU de co-simulación FMI 2.0 |
| `dist/EkoCoffeeMachine.fmu` | FMU exportada |
| `fmu_adapter.py` | Reexporta `FmuPlantAdapter` del paquete `espresso_simulation` |
| `reference_model.py` | Las mismas ecuaciones integradas por Euler, para comparar con la FMU |
| `fmu_hil_bridge.py` | Puente entre la FMU y el canal de prueba del firmware por el puerto serie |
| `requirements-fmu.txt` | FMPy y pySerial |
| `tests/test_modelica_contract.py` | Pruebas del contrato |

Toda salida se etiqueta como simulación. La FMU nunca escribe GPIO ni controla
la cafetera.

## Exportar la FMU

Con OpenModelica (`omc`) instalado:

```bash
./export_fmu.sh
```

Escribe `dist/EkoCoffeeMachine.fmu`. Si la exportación nativa falla, el guion
intenta con una imagen local de OpenModelica llamada `openmodelica-mcp:local`,
que no forma parte del repositorio.

## Probar

Desde `device/`, con un entorno que tenga `requirements-fmu.txt` instalado:

```bash
PYTHONPATH=../packages python -m unittest software.modelica.tests.test_modelica_contract -v
```

`PYTHONPATH` hace falta porque el adaptador viene de `packages/espresso_simulation`.

## Puente con la tarjeta de control

Desde `device/`, con la tarjeta de control conectada por USB:

```bash
PYTHONPATH=../packages python -m software.modelica.fmu_hil_bridge --port /dev/ttyACM0 --steps 100
```

Avanza la FMU, envía a la tarjeta de control tramas `I,…` y escribe la respuesta `O,…` en
`hil_fmu_run.csv`. El puente abre el puerto a 115200 baudios y la consola del
firmware va a 921600: hay que igualar la velocidad antes de usarlo. Es un modo de
prueba; las salidas de potencia deben estar desconectadas.
