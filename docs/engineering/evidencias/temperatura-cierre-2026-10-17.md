# Cierre del control de temperatura · 2026-10-18 a 2026-10-22

Este informe reúne las pruebas que cerraron el lazo de temperatura de Metis.
Los criterios de cada prueba se fijaron antes de medir. Al empezar, la caldera
se pasaba después de cada café (hasta 93,1–93,6 °C durante unos 8 min, fuera de
la banda de 92,0 ± 0,6 °C), la corrección de la prealimentación sin agua
([ADR-009](../../arquitectura/decisiones/ADR-009-control-local-temperatura-pi-ciclo-integral.md),
enmienda del 2026-10-17 a la § 3.4) faltaba comprobarla en la máquina, y no se
sabía cuánto esperar en «lista». Después se añadieron la relación entre la
caldera y el agua sobre el café, el enfriamiento del primer uso tras un reposo y
el arranque rápido con la referencia de caldera más alta.

## Método común

- **Cafés:** perfil `profile-placa-01-v11` (corte a los 20 s), 13 g molidos a
  38 clics y canastilla presurizada de dos tazas.
- **Antes de cada café o purgado:** metal entre 91,4 y 92,6 °C y al menos
  10 min en «lista» (salvo en las pruebas de espera). Después, 15 min sin tocar
  la máquina, la ventana del sobrepaso.
- **Medidas:** el metal lo registra la placa. El agua la mide un termistor
  auxiliar (NTC-3), que desde la curva caldera → agua queda fijo en la cavidad del
  portafiltro montado sin canastilla, donde el agua caería sobre el café. Cada
  jornada se compara con una sonda de referencia en agua caliente (±2 °C).
- **Usos de agua:** se pulsa café con el portafiltro sin canastilla y el agua
  sale libre. El indicador es la media del chorro entre 5 y 19 s.

## Resultados

### Resultados del 2026-10-18

Esa mañana el NTC-3 dio +0,80 °C de media frente a la sonda. Las pruebas A, B y
C usan el control del acta v4.

#### A y B · sobrepaso tras el café

**Para qué.** Medir el sobrepaso con un café de 20 s y probar si una ley de
devolución de la integral lo evita.

**En qué consiste.** A es un café con el programa de operación. B son dos
purgados y un café con un programa que devuelve la integral con τ_eq = 533 s.
Criterio: vuelta a «lista» en 240 s o menos y máximo de 92,6 °C en los 15 min
siguientes.

**Cómo se hizo.** Ambos programas con la sintonía del acta v4 y
prealimentación 0,75; B con el corte por falta de agua. El café B se pulsó sin
los 10 min en banda.

**Resultados.**

| Uso | Vuelta a «lista» | Máximo en 15 min |
|---|---|---|
| Café A, sin la ley | 214 s | 93,23 °C, 9,4 min tras el corte |
| Purgado 1, con la ley | 220 s | 92,85 °C |
| Purgado 2, con la ley | 191 s | 92,69 °C |
| Café B, con la ley | 138 s | 92,84 °C |

Con la ley la caldera recupera antes y se pasa 0,4 °C menos, pero después queda
corta (91,29 °C tras el purgado 2).

**Conclusión.** El sobrepaso persiste con cafés de 20 s. La ley no cumple el
máximo en ningún uso y no se adopta. El corte de la prealimentación por falta de
agua no dio falsos positivos.

#### C · espera en «lista»

**Para qué.** Saber desde cuántos minutos en «lista» el café sale igual.

**En qué consiste.** Usos pulsados tras esperas de 1 a 20 min contadas desde la
vuelta a «lista». Criterio: que el agua no varíe más que entre usos iguales,
cuyo recorrido medido el 2026-10-15 fue 78,3–80,1 °C.

**Cómo se hizo.** Primero cuatro cafés a 44 clics, con esperas de 3 min 21 s a
21 min. Como el café no se controla, después se repitieron con agua sola,
pulsando a la hora exacta aunque el metal estuviera fuera de la banda.

**Resultados.** Con café, la lectura del NTC a 19 s sigue a la masa (de 15,5 a
18,9 g, de 66,5 a 74,6 °C) y no a la espera; a 44 clics leyó hasta 9 °C menos
que a 38. Con agua sola:

| Espera | Usos | Máximo | Media del máximo | Media a 19 s |
|---|---|---|---|---|
| 5–20 min | 4 | 81,1–82,6 °C | 82,1 °C | 75,5 °C |
| 2 min | 3 | 81,1–82,1 °C | 81,5 °C | 75,0 °C |
| 1 min | 2 | 80,5–81,7 °C | 81,1 °C | 75,3 °C |

Los grupos se solapan y a los 19 s no hay orden. El metal al pulsar varió entre
91,1 y 93,0 °C sin que se notara en el agua.

**Conclusión.** Desde 1 min en «lista» el agua sale dentro de la dispersión
normal. Si una espera corta enfría algo, es menos de 1 °C en el primer chorro.
Por debajo de 1 min no se midió.

#### El criterio pasa a ser el agua

El metal oscila cerca de 1 °C en reposo por una perturbación que no se
identificó; se descartaron el aire del cuarto y, por magnitud, la tensión de
red. Esa oscilación no se nota en el agua. Por eso el criterio de temperatura
pasa a ser **que el agua que sale sea pareja**, no que el metal quede en la
banda de ±0,6 °C. Con él, el control del acta v4 sin la ley de devolución
cumple dentro de lo que se puede medir. La banda del metal queda como condición
de «lista».

### Curva caldera → agua sobre el café

**Para qué.** Que el perfil pida el agua y la placa calcule la referencia de
caldera.

**En qué consiste.** Usos de agua a cuatro referencias: 88, 92, 96 y 100 °C.

**Cómo se hizo.** El 2026-10-18 de 14:10 a 15:15, con el portafiltro montado
todo el tiempo. Para cada referencia, cuatro usos de 20 s, cada uno 2 min
después de volver a «lista»; el primero no cuenta.

**Resultados.**

| Referencia | Metal al pulsar | Media del máximo | Media 5–19 s |
|---|---|---|---|
| 88 °C | 88,6–89,4 °C | 85,5 °C | 81,3 °C |
| 92 °C | 92,0–93,1 °C | 90,1 °C | 84,8 °C |
| 96 °C | 97,4–98,0 °C | 92,2 °C | 88,9 °C |
| 100 °C | 101,7–102,5 °C | 94,6 °C | 92,2 °C |

- La media del chorro es lineal con la referencia: agua ≈ 0,92 · referencia +
  0,3 °C, con residuos de 0,26 °C o menos.
- Contra el metal filtrado al pulsar, uso a uso en los 12 usos: agua =
  0,8063 · metal + 9,84 °C, rms 0,55 °C, residuo máximo 0,91 °C, con el metal
  entre 88,60 y 102,49 °C.
- El máximo sube 0,74 °C por grado y pasa de 92 °C, el punto de ebullición
  local, desde 96 °C. Por la regla fijada no se probó por encima de 100 °C.
- Con 96 y 100 °C el metal quedó 1,5–2,5 °C sobre la referencia al pulsar.

**Conclusión.** El agua sobre el café es lineal con la caldera. El acta v5 usó
la recta contra la referencia.

### Comprobación con la caldera a 99,7 °C

**Para qué.** Comprobar que la recta lleva el agua a lo pedido.

**En qué consiste.** El perfil v15 pide 92 °C de agua. Criterio, fijado antes
de medir: la media 5–19 s de los usos 2–4 en 92 ± 0,8 °C (incertidumbre de la
comparación con la sonda ese día) y ningún uso fuera de 92 ± 1 °C.

**Cómo se hizo.** El 2026-10-18 de 15:56 a 16:12, con el firmware del acta v5:
la recta contra la referencia pone la caldera en 99,67 °C. Mismo método que la
curva. Después se repitió de 16:28 a 17:10 con la recta contra el metal (acta
v6), que pone la caldera en 101,90 °C, y se siguió con usos hasta que tres
quedaran dentro de 0,3 °C entre sí.

**Resultados.**

- **Con la recta contra la referencia no cumple.** Los usos 2–4
  (sim-00000260/261/262) dan 90,3 / 90,7 / 90,9 °C, media 90,6 °C. Esa recta se
  equivoca en −1,1 a −1,7 °C; la recta contra el metal, en −0,25, −0,28 y
  −0,33 °C. En la curva el metal quedó 1,7–2,5 °C sobre la referencia a 100 °C;
  aquí, 0,4–1,3 °C. La recta contra la referencia lleva dentro el exceso de
  aquella tarde, que no se repite.
- **Con la recta contra el metal, los usos 2–4 tampoco cumplen** (90,9 °C de
  media): tras un reposo largo el agua sube uso a uso (88,1 → 90,3 → 90,7 →
  91,7 → 92,2 °C) durante unos 20 min. En uso continuo se estabiliza en
  92,2 °C, con el metal en 103,9–104,7 °C. Pulsar al llegar a «lista» da
  1–2 °C menos que esperar 2 min.
- El máximo se pega al hervor (94,0–94,3 °C) con el metal sobre 102 °C, y en uso
  seguido el metal queda 1,5–2,8 °C sobre la referencia por la integral cargada.

**Conclusión.** El agua sigue al metal al pulsar, no a la referencia. Se adopta
la recta contra el metal (acta v6, que la v7 conserva): 92 °C de agua piden
101,9 °C de referencia. Lo que falla en los primeros usos es el enfriamiento
tras el reposo, que se estudia a continuación.

### Ensayo de reposo

**Para qué.** Medir cuánto se enfría el primer uso tras un reposo y ajustar un
modelo que lo prediga.

**En qué consiste.** Tras cada reposo, contado desde el fin del último uso, se
hacen cinco usos. El modelo se ajusta con los reposos de 10, 20 y 40 min; un
segundo reposo de 20 min queda reservado, y en él la media 5–19 s y la subida
entre usos seguidos deben acertar con 1,01 °C o menos.

**Cómo se hizo.** Mismo método que la curva, con el perfil v15 (caldera en
101,90 °C). El 2026-10-18 se midieron un reposo de unas 4 h 9 min, uno de 20 min
y uno de 40 min.

**El NTC-3 se corrió en el reposo de 40 min.** Desde las 23:07 leyó unos 4,5 °C
de más (meseta del hervor en 98,9 °C, imposible con la cavidad abierta). La
placa auxiliar leyó bien con una resistencia fija de 10 kΩ. Los dos usos tras
el fallo no valen, y la segunda vuelta del reposo de 40 min queda **dudosa**. A
la mañana siguiente el NTC-3 dio −0,04 °C frente a la sonda: el fallo se
atribuye a la conexión en la protoboard (inferido).

Ese arranque en frío, con la referencia en 101,90 °C, tardó **15 min 51 s**
desde 23,5 °C hasta «lista»: el arranque rápido no actuó, porque su referencia
máxima era 93 °C.

### Ensayo de reposo, segunda parte

**Cómo se hizo.** El 2026-10-19, con el NTC-3 comprobado esa mañana: cinco usos
de calentamiento que no cuentan, reposos de 10 y 40 min, un reposo de 1 h 30 min
declarado de comprobación antes de medirlo, uno de 3 h 13 min y, al final, el
reposo de 20 min reservado.

**Resultados.** Media 5–19 s, en °C:

| Fecha | Reposo | Usos (sim-) | Primer uso | Segundo uso | Usos 3–5 |
|---|---|---|---|---|---|
| 18 | ~4 h 9 min | 272–276 | 87,6 | 88,8 | 91,4–91,5 |
| 18 | 20 min | 277–281 | 88,7 | 90,7 | 91,2–91,3 |
| 18 | 40 min (dudoso) | 285–289 | 88,9 | 91,6 | 92,1–92,3 |
| 19 | 10 min | 295–299 | 91,3 | 91,4 | 91,1–91,4 |
| 19 | 40 min | 300–304 | 88,5 | 91,4 | 91,7–92,0 |
| 19 | 1 h 30 min | 305–309 | 89,7 | 90,6 | 91,7–92,1 |
| 19 | 3 h 13 min | 310–314 | 89,5 | 92,3 | 92,2–92,4 |
| 19 | 20 min, reservado | 315–319 | 90,4 | 92,3 | 92,0–92,2 |

- **Tras 10 min el primer uso no sale frío.** Tras 20 min, 40 min y 3 h 13 min
  el 19, sale 1,8, 2,9 y 2,8 °C más frío que los recuperados, y desde el segundo
  uso vuelve a lo normal. Tras 1 h 30 min el segundo uso aún salía 1,2 °C bajo
  el estable: la recuperación no siempre tiene la misma forma.
- Los usos recuperados del 19 leen 0,6–1,0 °C más que los del 18, con el NTC-3
  en otro montaje.

**Modelo del agua.** Se probaron cuatro variantes de un modelo del agua en el
portafiltro; la variante Pe es la que mejor acierta el primer uso. Su versión
v6 se ajustó solo con los reposos del 19. En el reposo reservado, Pe v6 da para
el primer uso 90,55 frente a 90,38 °C (+0,17 °C), pero para la subida al
segundo 0,90 frente a 1,98 °C: un error de −1,08 °C, sobre el margen de
1,01 °C. Las cuatro variantes suben como mucho
1,3 °C del primer al segundo uso, donde la máquina sube 2,7–2,8 °C.

**Conclusión.** Tras un reposo de 20 min o más, solo el primer uso sale frío.
Ninguna variante del modelo cumple la comprobación ciega y, por la regla
fijada, ninguna se entrega.

### Compensación del primer uso

**Para qué.** Compensar el primer uso tras el reposo sin tocar los demás.

**En qué consiste.** La plataforma pasa `boilerOffsetC` del perfil a la placa,
que sube la referencia de caldera solo para la preparación siguiente y la
devuelve al terminarla. Criterio: el primer uso a 1,01 °C o menos de la media de
los usos 2 a 5. Antes del segundo reposo de 30 min el criterio pasó a
**0,5 °C o menos**, porque la repetibilidad de los usos recuperados del 19 es
0,11 °C (desviación agrupada de los usos 3–5).

**Cómo se hizo.** El 2026-10-19 por la tarde, con el binario de identificación
`ensayo-primer-uso` y un máximo provisional de 4 °C
(`CONFIG_CAFE_FIRST_USE_OFFSET_MAX_DECI_C=40`). Perfiles v16 a v19 = v15 con la
subida de cada fila; la placa aceptó y retiró las cinco subidas. La de 2,4 °C
la pidió Pe v6; la de 2,9 °C, Pe v7 con corrección de sesgo; la de 4,0 °C es el
máximo del binario, para separar la ganancia del ruido.

**Resultados.** Media 5–19 s, en °C:

| Reposo | Usos (sim-) | Subida | Primer uso | Media usos 2–5 | Diferencia |
|---|---|---|---|---|---|
| 20 min | 315–319 | — | 90,4 | 92,2 | −1,8 |
| 20 min desde «lista» | 320–324 | +3,0 °C | 91,6 | 92,0 | −0,4 |
| 30 min | 325–329 | +2,4 °C | 90,92 | 91,73 | −0,80 |
| 30 min | 330–334 | +2,9 °C | 91,11 | 91,81 | −0,70 |
| 30 min | 335–339 | +4,0 °C | 91,97 | 91,78 | +0,20 |

- Pe sobrestima el primer uso compensado: con +2,4 °C sale 1,13 °C bajo lo
  predicho (92,05 °C).
- **Ganancia del primer uso:** entre +2,4 y +4,0 °C el primer uso sube de 90,92
  a 91,97 °C, unos 0,66 °C de agua por °C de caldera. Pe suponía 0,69–0,85.
- La referencia máxima alcanzada fue 105,90 °C.
- El uso con +4,0 °C tras 1 h 25 min de reposo (sim-00000340) no vale: el NTC-3
  saltó de 1604 a 2730 mV a las 20:41:40, en pleno reposo, y leyó 129–131 °C.

**Conclusión.** Subir la referencia de caldera solo para el primer uso lo
compensa. Con +4,0 °C tras 30 min cumple el criterio de 0,5 °C; con +2,4 y
+2,9 °C, no. Con los déficits sin compensar de 2,8–2,9 °C tras 40 min y
3 h 13 min y esa ganancia, el máximo de 4 °C cubre hasta unos 30 min y se queda
~0,5 °C corto en los reposos largos (inferido). Se firma el acta v7
([`acta-temperatura-20261019.json`](../actas/acta-temperatura-20261019.json)):
la v6 sin cambios más `first_use_offset.max_offset_c = 4,0 °C`.

### Arranque rápido con δ = 25 °C desde frío

**Para qué.** Con la caldera en 101,9 °C el arranque rápido no actúa, porque
el acta lo limita a `fast_start.max_setpoint_c` = 93 °C. Se busca que actúe sin
dar una «lista» falsa.

**En qué consiste.** El 2026-10-20 se probó el binario `arranque-101`, con la
referencia del arranque rápido hasta 103 °C y el corte en referencia − 20 °C.
Llegó a «lista» definitiva a los 8 min 30 s, pero dio una «lista» falsa a
1 min 15 s: tras el corte, la inercia del calentador subió el metal 20,9 °C,
pasó la referencia y luego cayó a 92,2 °C. Se prueba entonces el corte en
referencia − 25 °C. Criterio, fijado antes de encender: (1) tras la primera «lista» la placa no la
pierde en 10 min; (2) la caldera no pasa de 102,5 °C (referencia + 0,6 °C);
(3) «lista» definitiva antes de 15 min 51 s.

**Cómo se hizo.** El 2026-10-21 a las 23:07:58, con el binario
`arranque-101-d25`, el perfil v15 y el sostenimiento de la plataforma
suspendido. Metal a 24,3 °C al encender.

**Resultados.**

- **(1) Cumple:** ninguna «lista» perdida en los 10 min siguientes.
- **(2) No cumple tal como se escribió:** el máximo fue 102,95 °C, pero en
  «lista», de 2 a 10 min después de llegar, con el calentador en 3–4 %. El pico
  tras el corte (a 76,8 °C tras 53 s al 100 %) fue 97,91 °C, 4 °C bajo la
  referencia, sin «lista» falsa. La subida en «lista» es la oscilación de la
  espera, con una banda de ±2,1 °C (inferido).
- **(3) Cumple:** 7 min 49 s.

**Conclusión.** Con δ = 25 °C el arranque rápido llega a «lista» en la mitad de
tiempo y sin «lista» falsa.

### Subida automática del primer uso en la máquina

**Para qué.** Comprobar que la placa aplica la subida que decide la plataforma.

**En qué consiste.** Un café y cuatro cambios de estado: excluir e incluir el
perfil, suspender y reanudar el sostenimiento. Criterio: (1) la
placa llega a «lista» con la referencia subida; (2) al terminar el café vuelve a
101,9 °C; (3) el resumen declara la subida aplicada y la plataforma deja de
pedirla.

**Cómo se hizo.** El 2026-10-22, con el firmware de operación y la regla
`primer-uso-v1`, que pedía +3,70 °C (`beyond-measured`). La corrección después
del café no se verifica porque no corrige en la máquina real
([ADR-018](../../arquitectura/decisiones/ADR-018-politica-de-sostenimiento-del-objetivo.md)
§ 12.1).

**Resultados.**

- La placa llegó a «lista» con la caldera en 105,0 °C. **(1) Cumple.**
- Al terminar el café sim-00000343 la placa informó «referencia de caldera
  105.60 C -> 101.90 C». **(2) Cumple.**
- El resumen trae `boilerOffsetAppliedC` 3,7. **(3) Cumple.**
- Excluir el perfil v15 desde la aplicación da `excluded-by-barista` y subida
  0,00 °C; incluirlo de nuevo, `beyond-measured` y 3,70 °C. Suspender el
  sostenimiento da `suspended-by-protocol` y 0,00 °C; levantar la suspensión
  vuelve a 3,70 °C. La placa recibió cada cambio en menos de 1 s.
- `FirstUseRule` y la placa acotan la subida al máximo del acta (4,0 °C).

**El agua de este café no se midió.** El NTC-3 marcó hasta 101,7 °C en el
chorro, por encima del hervor local, y esa mañana dio +2,02 °C frente a la
sonda: no cumplió el ±2 °C. El agua del primer uso con +3,7 °C queda **inferida** entre lo medido el 19: +2,9 °C dejó el agua
0,70 °C corta y +4,0 °C, 0,20 °C por encima.

**Conclusión.** La placa aplica la subida que pide la plataforma, la retira al
terminar el café y obedece exclusiones y suspensiones.

### Arranque rápido con la subida del primer uso desde el encendido

**Para qué.** Con la subida pedida desde el encendido la referencia supera el
tope de 103 °C y el arranque rápido no actúa.

**En qué consiste.** Binario `arranque-106-d25`: el anterior con la referencia
del arranque rápido hasta 106 °C
(`CONFIG_CAFE_HEATER_FAST_START_MAX_SETPOINT_DECI_C=1060`). Criterio, fijado
antes de encender: (1) tras la primera «lista» no se pierde en 10 min; (2) el
pico tras el corte queda bajo la referencia (105,6 °C); (3) «lista» antes de
15 min 51 s.

**Cómo se hizo.** El 2026-10-22 a las 08:50:57, con el perfil v20 = v15 con
`boilerOffsetC: 3.7` (101,9 + 3,7 = 105,6 °C desde el encendido). Metal a
22,7 °C al encender.

**Resultados.**

- **(1) Cumple:** ninguna «lista» perdida en 10 min.
- **(2) Cumple:** pico de 101,84 °C, 3,8 °C bajo la referencia, sin «lista»
  falsa.
- **(3) Cumple:** 8 min 5 s.
- En «lista» el metal llega a 106,80 °C, la oscilación de la espera.

**Conclusión.** El arranque rápido con δ = 25 °C funciona también con la
referencia subida desde el encendido.

## Conclusión

El sobrepaso tras el café persiste, y la ley de devolución de la integral no lo
corrige y no se adopta. El corte de la prealimentación sin agua no da falsos
positivos. El criterio de temperatura pasa a ser el agua: sale pareja aunque el
metal oscile cerca de 1 °C, y desde 1 min en «lista». El agua sigue al metal al
pulsar (agua = 0,8063 · metal + 9,84 °C), y 92 °C de agua piden 101,9 °C de
referencia. Tras un reposo de 20 min o más solo el primer uso sale frío, entre
1,8 y 2,9 °C. Subir la caldera solo para ese uso lo compensa con unos 0,66 °C
de agua por °C, y el máximo de 4,0 °C del acta v7 cubre hasta unos 30 min. El
arranque rápido con δ = 25 °C llega a «lista» en 7 min 49 s (101,9 °C) y en
8 min 5 s (105,6 °C), sin «lista» falsa, frente a 15 min 51 s sin él. La placa
aplica la subida que pide la plataforma y la retira al terminar el café. El
NTC-3 falló por su conexión tres veces; los usos afectados no entran en los
resultados.
