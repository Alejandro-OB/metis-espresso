# Escalera de la bomba con café · 2026-10-11

Pasos 6.5 y 6.6 de la puesta en servicio.

## Para qué es la prueba

La prueba caracteriza la respuesta de la presión a la demanda de la bomba con
una pastilla de café en la canastilla. Responde dos preguntas: si existe una
zona de demanda baja sin respuesta de presión (6.5) y cuánto tarda la presión
en subir y en descargarse en cada escalón (6.6). Con esos datos se ajusta la
planta del lazo de presión.

## En qué consiste

Se recorre una escalera de demandas de la bomba, del 1 al 75 %, con café en la
canastilla, y se registra la presión en cada escalón. La escalera ya se había
recorrido con la canastilla vacía el 2026-10-10.

## Cómo se hizo

- **Secuencia:** `pump-ladder-v1`, con escalones de 2 s de reposo, 6 s de
  demanda y 4 s de descarga, de 1 a 75 %. Se hicieron dos pasadas.
- **Café:** 14 g molidos a 38 clics (en ese molino, más clics dan molido más
  fino), una pastilla nueva por pasada, en la canastilla presurizada de dos
  tazas.
- **Caldera:** cerrada con el tornillo del orificio de vapor. Calentador
  regulando con el acta v2, a 92 °C.
- **Firmware:** `ident-escalera-v2`. Cada escalón exige pulsar café con la
  máquina en «lista». Entre escalones la caldera bajaba un poco y salía de
  «lista»; el siguiente escalón se pulsaba de vuelta en «lista».

## Resultados

Presión máxima en cada escalón (bar):

| Bomba | 1 % | 2 % | 3 % | 5 % | 10 % | 20 % | 30 % | 50 % | 75 % |
|---|---|---|---|---|---|---|---|---|---|
| Pasada 1 | 4,9 | 5,7 | 5,8 | 6,0 | 6,2 | 6,4 | 6,7 | 8,3 | 10,2 |
| Pasada 2 | 5,4 | 5,6 | 5,8 | 5,9 | 6,1 | 6,4 | 6,7 | 8,6 | 10,3 |

Las dos pasadas coinciden dentro de 0,3 bar, salvo en el 1 %. Ese escalón es
el más sensible a la presión de reposo de partida, que fue de 2,0 bar en la
primera pasada y de 2,4 bar en la segunda.

**Zona sin respuesta (6.5).** No hay. La presión sube en todos los escalones
desde el 1 %, en las dos pasadas.

**Tiempos (6.6)**, de la primera pasada; la segunda da lo mismo con una
dispersión de unas décimas:

| Bomba | Subida 10→90 % del salto | Descarga a la mitad tras cortar |
|---|---|---|
| 1 % | 3,4 s | 2,0 s |
| 2 % | 1,8 s | 1,1 s |
| 3 % | 1,0 s | 0,7 s |
| 5 % | 0,8 s | 0,4 s |
| 10–30 % | 0,2–0,4 s | 0,5–0,7 s |
| 50 % | sin meseta en 6 s | 0,3 s |
| 75 % | sin meseta en 6 s; 9 bar a unos 3,4 s del arranque | 0,6 s |

**Apertura de la canastilla presurizada.** Hasta el 30 % (6,7 bar) no sale
nada; en el 30 % gotea solo al cortar, y desde el 50 % (8,3 bar) sale chorro.
La canastilla abre hacia 8 bar. Por eso, al 50 y al 75 % la presión no se
estabiliza en 6 s: al abrir, el caudal crece y la presión sigue subiendo hasta
el corte.

**Presión de reposo.** Con la caldera cerrada, la presión no vuelve a cero
entre escalones y queda en unos 3 bar. Al terminar, con el calentador
encendido, se quedó en 2,9 bar sin bajar; al apagarlo bajó a 0,8 bar en 10 min.
Al quitar el portafiltro con 0,85 bar, la lectura no cambió. Esa presión está
del lado de la caldera, donde lee el sensor, y no detrás de la pastilla: entre
la caldera y la ducha del grupo hay algo que la retiene.

## Conclusión

La planta de presión con café no es la medida con la canastilla vacía, que
nunca pasa de 6,7 bar. Con café, la presión sube muy rápido con demandas bajas,
se aplana entre 5 y 7 bar y vuelve a subir cuando la canastilla abre, hacia
8 bar. No hace falta compensar un umbral de demanda. La sintonía del lazo de
presión necesita un modelo ajustado a estos datos. Para identificar la planta
en la zona de 9 bar hacen falta escalones de más de 6 s, porque desde el 50 %
la presión no alcanza su régimen en ese tiempo.
