# Escalera de la bomba con café · puesta en servicio 6.5 y 6.6 · 2026-10-11

Dos pasadas de la escalera de la bomba (`pump-ladder-v1`: escalones de 2 s de
reposo, 6 s de demanda y 4 s de descarga, de 1 a 75 %) con café de verdad en
lugar de la canastilla vacía de la etapa 6
(escalera del 2026-10-10).

## Condiciones

- **Café:** 14 g molidos a 38 clics (molino del operador; más clics, más fino),
  una pastilla nueva por pasada, en la **canastilla presurizada de dos tazas**.
- **Caldera cerrada** con el tornillo del orificio de vapor, sello rehecho tras
  la fuga de esa misma noche. Calentador regulando con el acta v2, a 92 °C.
- **Firmware:** `ident-escalera-v2`, la escalera hidráulica compilada con el
  código de f7efe56. Cada escalón exige pulsar café con la máquina en «lista».
- Entre escalones la caldera bajaba un poco y salía de «lista»; café solo se
  pulsaba de vuelta en «lista».

## Resultados

Presión máxima en cada escalón (bar):

| Bomba | 1 % | 2 % | 3 % | 5 % | 10 % | 20 % | 30 % | 50 % | 75 % |
|---|---|---|---|---|---|---|---|---|---|
| Pasada 1 | 4,9 | 5,7 | 5,8 | 6,0 | 6,2 | 6,4 | 6,7 | 8,3 | 10,2 |
| Pasada 2 | 5,4 | 5,6 | 5,8 | 5,9 | 6,1 | 6,4 | 6,7 | 8,6 | 10,3 |

Las dos pasadas coinciden dentro de 0,3 bar salvo el 1 %, que es el más sensible
a la presión de reposo de partida (2,0 bar en la primera, 2,4 en la segunda).

**6.5 · zona sin respuesta: no hay.** La presión sube en todos los escalones
desde el 1 %, en las dos pasadas. Con café no hace falta compensar un umbral.

**6.6 · tiempos.** Cálculo: `tiempos_66.py`.

| Bomba | Subida 10→90 % del salto | Descarga a la mitad tras cortar |
|---|---|---|
| 1 % | 3,4 s | 2,0 s |
| 2 % | 1,8 s | 1,1 s |
| 3 % | 1,0 s | 0,7 s |
| 5 % | 0,8 s | 0,4 s |
| 10–30 % | 0,2–0,4 s | 0,5–0,7 s |
| 50 % | sin meseta en 6 s | 0,3 s |
| 75 % | sin meseta en 6 s; 9 bar a unos 3,4 s del arranque | 0,6 s |

(Primera pasada; la segunda da lo mismo con una dispersión de unas décimas.)

**La canastilla presurizada abre hacia 8 bar.** Hasta el 30 % (6,7 bar) no sale
nada; en el 30 % gotea solo al cortar, y desde el 50 % (8,3 bar) sale chorro.
Por eso al 50 y al 75 % la presión no se estabiliza en 6 s: al abrir, el caudal
crece y la presión sigue subiendo hasta el corte.

**Presión de reposo de unos 3 bar.** Con la caldera cerrada la presión no vuelve
a cero entre escalones. Al terminar, con el calentador encendido, se quedó en
2,9 bar y no bajaba; al apagarlo bajó a 0,8 bar en 10 min. Al quitar el
portafiltro con 0,85 bar **la lectura no cambió**: esa presión está del lado de
la caldera, donde lee el sensor, y no detrás de la pastilla. Entre la caldera y
la ducha del grupo hay algo que la retiene.

## Qué significa para la etapa 7

- **La planta con café no es la de la 6.3.** Aquella se midió con la canastilla
  vacía, que nunca pasa de 6,7 bar. Con café la presión sube muy rápido a
  densidades bajas, se aplana entre 5 y 7 bar, y vuelve a subir cuando abre la
  canastilla. La sintonía de la 7.1 necesita un modelo ajustado a estos datos.
- **6 s de bomba no bastan para ver el régimen a partir del 50 %.** Para
  identificar la planta en la zona de 9 bar hacen falta escalones más largos.

## Registros

En `6.5-6.6-cafe-20261011/`: el registro de la placa
de las 01:20 a las 02:36 Z, con las dos pasadas, y `tiempos_66.py`. La bitácora
de la jornada está en `7.9-prealimentacion-20261011/notas.txt`.
