# Plantas identificadas

Contiene la planta identificada de cada lazo de control, medida sobre la
cafetera:

| Archivo | Lazo |
|---|---|
| `pressure_plant.h` | Presión (bomba) |
| `temperature_plant.h` | Temperatura (calentador) |

Cada archivo define, con el prefijo `COMMISSIONED_PRESSURE_` o
`COMMISSIONED_TEMPERATURE_`, el identificador de la identificación, la ganancia,
la constante de tiempo y el retardo del modelo de primer orden, el λ de la
sintonía, el filtro del lazo, la velocidad del limitador de referencia, el techo
de la salida y el límite duro. El de temperatura añade el arranque rápido, la
prealimentación, la recta caldera → agua y el máximo de la compensación del
primer uso; el de presión, la preinfusión. La lista completa de campos y lo que
pasa cuando falta cada uno está en [`../commissioned_plant.h`](../commissioned_plant.h).

**Que un archivo exista es lo que pone en servicio su lazo.** `commissioned_plant.h`
lo detecta con `__has_include`; sin el archivo, el lazo queda sin poner en
servicio y el guardián de actuación inhibe su salida. No hay opción de Kconfig
para esto a propósito: una opción podría activarse sin medición, un archivo
generado no.

Los archivos son salida de las herramientas de identificación y no se editan a
mano. Un archivo de planta escrito a mano pondría en marcha la actuación con
números que nadie midió, que es justo lo que el guardián existe para impedir.

Con estos dos archivos presentes el firmware no compila con `CAFE_SIMULATION` ni
con `CAFE_CONTROL_BENCH_LOOP` activos: un binario capaz de energizar la máquina
no puede regular contra una planta simulada.
