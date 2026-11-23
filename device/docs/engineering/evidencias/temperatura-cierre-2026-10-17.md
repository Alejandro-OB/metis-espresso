# Cierre de la temperatura · guion de las pruebas con la máquina

Escrito el 2026-10-17, antes de medir. Guion de puesta en servicio, 7.6
(sobrepaso tras el café) y 7.9 (temperatura del café). Lo que aquí se fija
—orden, condiciones y criterios— no se cambia después de ver los datos; si se
cambia, se dice cuándo y por qué, como en la partición de presión.

## Para qué sirven

La temperatura tiene hoy dos problemas medidos y una pregunta abierta:

1. **La caldera se pasa después de cada café.** Vuelve a «lista» y sube hasta
   93,1–93,6 °C durante unos 8 min, fuera de la banda de 92,0 ± 0,6 °C
   (evidencia del 16). La causa está medida:
   la integral se carga mientras el agua nueva se calienta y después sobra.
2. **La prealimentación calienta aunque no entre agua.** Ya está corregido en
   el código (ADR-009, enmienda del 2026-10-17 a la § 3.4), pero no está en la
   placa.
3. **No se sabe cuánto hay que esperar en «lista».** Todos los cafés medidos
   esperaron 11–26 min, y en ese tramo la espera no cambia el café. Por debajo
   de 10 min no hay datos. La observación del director (el agua y el metal
   tardan en igualarse) predice café más frío con esperas cortas.

**Objetivo esperado al terminar:** que la caldera se quede en la banda después
de cada café, que la placa no caliente sin agua, y saber con datos desde
cuántos minutos en «lista» el café sale igual. Con eso, el control de
temperatura de la puesta en servicio queda cerrado. Solo falta la catación, que
va en la sesión con el barista.

## Condiciones comunes

- **Receta:** perfil `profile-placa-01-v11` (corte a los 20 s, relación 1:2),
  13 g a 38 clics molidos justo antes, canastilla presurizada de dos tazas,
  misma bolsa en todas. Antes de cada café se comprueba en el registro que la
  placa arranca la extracción con ese perfil.
- **Verificación B antes de cada café o purgado:** metal entre 91,4 y
  92,6 °C, al menos 10 min seguidos en «lista» (salvo en la prueba C, donde la
  espera es la variable), presión por el acta, depósito lleno y su temperatura
  anotada, tara justo antes de pulsar.
- **Después de cada café o purgado, 15 min sin tocar la máquina.** Es la
  ventana en que aparece el sobrepaso.
- **Una persona basta.** El metal lo registra la placa. En los tres cafés
  (A, el de B y los de C) se sostiene además el NTC-3 bajo el pico, en el
  mismo sitio de siempre, y se lee a los 10, 13, 16 y 19 s. Con el corte a los
  20 s ya no hay lectura a los 25 s. En los purgados no hace falta.
- **A1 del NTC-3** en agua hirviendo antes del primer café, como cada día de
  medidas de café (ntc-aux-2026-10-14.md).

## A · Café de referencia (programa de operación actual)

**Qué se hace:** un café con el programa que está cargado
(`operacion-purga-perfil`, sin ley del director) y el perfil v11.

**Qué se obtiene:**
- Para presión (9.4): el identificador de la extracción, el perfil con que
  arrancó, el instante del corte y la masa asentada 5–15 s después.
- Para la temperatura: **cuánto se pasa la caldera con un café de 20 s**. El
  sobrepaso medido el 16 fue con cafés de 27 s. Si con 20 s el metal no sale de
  la banda, la ley del director no tiene nada que corregir con esta receta, y
  se dice así.

**Lo que se anota:** máximo del metal en los 15 min, tiempo fuera de la banda y
tiempo de vuelta a «lista».

## B · Ley del director (programa `devolucion-integral`)

**Qué se carga:** con la cafetera desconectada, `devolucion-integral`:
sintonía del acta v4, prealimentación 0,75, devolución de la integral con
τ_eq = 533 s y el corte por falta de agua. La placa debe declarar al arrancar
«Devolución de la integral térmica tras el café: τ_eq=533 s».

**Qué se hace:** dos purgados de 10 s con la canastilla de dos tazas vacía y
después un café como el de A. Cada uno tras la verificación B y seguido de sus
15 min.

**Criterio, fijado antes de medir** (ADR-009, segunda enmienda del 2026-10-16):
- el metal no pasa de 92,6 °C en los 15 min que siguen a cada uno;
- la vuelta a «lista» no tarda más de 240 s, lo más lento medido el 16 sin la
  ley. Se compara además con la de A, que es la misma receta.

**Qué se obtiene:**
- Si cumple, τ_eq = 533 s pasa a un acta y la ley rige en operación.
- Si no cumple, se sabe en cuál de los dos lados falla (se pasa o tarda) y el
  modelo de tres nodos dice hacia dónde mover τ_eq. Antes de otro ensayo se
  vuelve a simular.
- **Comprobación del corte por falta de agua, sin bombear en seco:** en los dos
  purgados y el café no debe aparecer «Prealimentación cortada». Si aparece, el
  corte da falsos positivos y se revisa antes de seguir.

## C · Esperas en «lista»

**Con qué programa:** con la ley si B cumple; si no, con el de operación. Se
declara cuál.

**Qué se hace:** cuatro cafés, cada uno tras una espera distinta en «lista»
contada desde que la placa vuelve a «lista». Orden: 20, 2, 5 y 10 min. El de
20 min va primero porque es la condición de todos los cafés medidos hasta hoy y
sirve de referencia del día.

**Criterio, fijado antes de medir:** una espera alcanza si su café a los 19 s
queda dentro del recorrido de los cafés iguales del mismo día: el de A, el de B
y el de 20 min de C, los tres con al menos 10 min en «lista». El número sale de
esos tres cafés, no se fija antes. Por qué no se usa uno anterior: el 16, a los
25 s, tres cafés iguales variaron 1,5 °C, pero a los 19 s los mismos variaron
4,7 °C (71,7, 74,4 y 76,4 °C), porque el NTC todavía sube. Con la receta de
20 s solo queda la lectura de los 19 s. Si el recorrido vuelve a ser de unos
5 °C, con un café por espera solo se ve un efecto mayor que ese, y se declara
así. Un café que se aparte por la pastilla (casos de 4–12 °C medidos) se
repite en la misma posición, con la causa anotada.

**Qué se obtiene:** el tiempo mínimo en «lista» antes de pulsar, sacado de los
datos. Pasa a la plataforma como el criterio de «máquina lista» que ve el
barista. Con un solo café por espera, el resultado vale para esta máquina y
esta receta, y se declara así.

## Resultado final del modelo

Con A, B y C, el modelo de temperatura queda así:

1. **Caldera:** `CalderaTermicaEstratificada` (Modelica, 10.2), con la ley del
   director y su τ_eq medido si B cumple. Sirve para simular el sostenimiento
   entre cafés.
2. **Café, como curva y no como un número de una receta.** El barista elige
   el tiempo (operador, 2026-10-17), así que el modelo da la temperatura del
   café segundo a segundo durante la extracción, según la caldera al pulsar y
   la bolsa, y se lee en el tiempo elegido. Se comprueba con los cafés de hoy:
   hasta los 19 s, los de 20 s deben seguir la curva de los de 27 s del 15 y el
   16. Si la siguen, el tiempo solo corta la curva, y el modelo vale para
   cualquier tiempo hasta 27 s; más allá no hay datos. **La pendiente con la
   caldera no se puede medir aquí**, porque todos los cafés salen a 92 °C: se
   usa la de `cafe-v2` (0,82), declarada como heredada. La molienda no entra:
   E3 midió que casi no cambia el caudal con esta canastilla (1–2 g entre 32 y
   38 clics; el tiempo da unos 2,1 g/s), así que no se espera que mueva la
   temperatura (inferido; en E3 el NTC no estaba en el chorro).
3. **Espera mínima en «lista»:** el resultado de C, o una corrección por espera
   si C muestra que el café depende de ella.

Con eso, y con el modelo de presión de la 10.4 para la masa, la plataforma
puede decirle al barista, para el tiempo y la dosis que elija, qué ratio va a
salir y a qué temperatura; o, si elige el ratio, cuánto tiempo poner (aprobado
por el operador el 2026-10-17). También desde cuándo puede pulsar, y puede
marcar un café que se aparte de lo esperado como posible problema de pastilla.
La receta de las pruebas (20 s, 38 clics, 1:2) no limita al barista.

## Lo que no entra aquí

- Que «lista» revise también el límite de arriba (el 17 marcó «lista» con el
  metal en 112 °C). Es un cambio de código aparte.
- Cafés a otras referencias para medir la pendiente con la receta nueva.
- Cafés de más de 27 s, fuera de lo medido.
- La catación, en la sesión con el barista.

## Resultados del 2026-10-18 (medido)

Registradores relanzados a las 08:53 tras un reinicio del computador; abrir el
puerto reinició la placa. A1 del NTC-3 a las 09:13–09:20: +0,80 °C de media
frente a la sonda (a1-ntc3-20261018.txt);
el NTC se había soltado de la protoboard de 09:06:32 a 09:13:43 y el método solo
usa datos desde ahí. La sonda no estaba en el depósito: su temperatura no consta.

### A · café de referencia (`operacion-sin-agua`, perfil v11)

| Extracción | Metal al pulsar | NTC a 10/13/16/19 s | Masa al corte / asentada | Presión máx. | Metal mín. |
|---|---|---|---|---|---|
| sim-00000228, 09:22:54 | 92,03 °C | 32,7 / 53,1 / 68,6 / 75,6 °C | 23,03 / 27,56 g | 8,90 bar | 81,82 °C |

Corte a los 19,994 s. Vuelta a «lista» a los 214 s. Sobre la banda de 09:30:55
a 09:37:28, máximo 93,23 °C a las 09:32:39 (9,4 min después del corte). Con un
café de 20 s el sobrepaso sigue.

### B · ley del director (`devolucion-integral`, τ_eq = 533 s)

Cargado a las 09:39 con la red desconectada; «lista» a las 09:49:19.

| Uso | Antes | Vuelta a «lista» | Máximo en 15 min | Sobre la banda |
|---|---|---|---|---|
| Purgado 1, 10:02:33–10:02:43 | 10 min en banda; integral 2,18 % | 220 s | 92,85 °C, 10:15:40 | 10:13:11–10:16:57 |
| Purgado 2, 10:29:49–10:29:59 | 10 min en banda | 191 s | 92,69 °C, 10:38:27 | toques sueltos, 10:38:27–10:40:13; otro a las 10:47:43 |
| Café sim-00000229, 11:00:00 | **no** cumplió 10 min: bajo 91,4 °C hasta 10:57:00 (mín. 91,29 °C) | 138 s | 92,84 °C, 11:08:57 | 11:07:15–11:12:32 |

Café B: metal al pulsar 92,15 °C, mínimo 82,37 °C, presión máx. 8,92 bar, masa
22,94 / 27,67 g, NTC 53,5 / 63,3 / 73,3 / 79,1 °C. El NTC no es comparable con
el A: este café siguió a dos purgados que calentaron el grupo y el portafiltro
(inferido). Ningún uso mostró «Prealimentación cortada».

**Decisión del operador (10:59):** pulsar el café B sin esperar otros 10 min en
la banda, porque con la ley la caldera oscila alrededor de ella. Desvía el
criterio fijado antes de medir y se declara.

**Resultado frente al criterio:** la vuelta a «lista» cumple en los tres usos
(≤ 240 s). El máximo **no cumple** en ninguno (92,85, 92,69 y 92,84 °C frente a
92,6 °C). Frente al café A, la ley recupera antes (138 frente a 214 s) y se pasa
0,4 °C menos y durante menos tiempo, pero deja la caldera corta después de
volver (91,29 °C tras el purgado 2). El purgado 1 se hizo con la placa recién
reiniciada y la integral todavía subiendo, que es el valor al que la ley
devuelve (inferido). La ley no se adopta; τ_eq se revisa con el modelo antes de
otro ensayo.

### C · esperas con café, a 44 clics (medido)

Molido de antemano, cuatro dosis de 13 g de la misma molienda. Espera contada
desde la vuelta a «lista»; la real queda en la tabla.

| Extracción | Espera real | Metal al pulsar | Masa al corte / asentada | NTC a 10/13/16/19 s | Presión máx. |
|---|---|---|---|---|---|
| sim-00000230, 11:53:14 | 21 min en banda | 92,16 °C | 15,53 / 20,35 g | 28,2 / 36,4 / 55,5 / 66,5 °C | 8,76 bar |
| sim-00000231, 12:00:54 | 3 min 21 s | 91,52 °C | 17,11 / 21,49 g | 27,3 / 41,9 / 60,2 / 68,1 °C | 8,81 bar |
| sim-00000232, 12:08:46 | 5 min 28 s | 92,09 °C | 18,85 / 22,70 g | 28,6 / 56,9 / 69,0 / 74,6 °C | **9,15 bar** |
| sim-00000233, 12:23:11 | 12 min 22 s | 92,52 °C | 18,69 / 23,62 g | 29,1 / 52,4 / 63,7 / 72,0 °C | 8,88 bar |

Las esperas de 2 y 10 min salieron en 3 min 21 s y 12 min 22 s porque el
operador llegó después de la hora; en los cafés 3 y 4 se esperó a que el metal
bajara a la banda (92,65 y 92,97 °C a la hora fijada). A 44 clics pasa bastante
menos café que a 38 (15,5–18,9 g frente a 23,0 g al corte), y el NTC a los 19 s
sale 1–9 °C más frío que el café A.

**Resultado:** no se ve que una espera corta enfríe el café. La lectura del NTC
sigue a la masa (de 15,5 a 18,9 g, de 66,5 a 74,6 °C) y no a la espera; el café
de 20 min fue el más frío. La masa creció café a café, por el molido al aire o
por el portafiltro cada vez más caliente (inferido, no comprobado). Con café, la
espera queda tapada por el café.

### C bis · esperas con agua sola (método fijado antes de medir, 2026-10-18 12:25)

Propuesta del operador: el café no se controla, así que las esperas se miden con
agua sola, para que lo único que cambie sea la caldera.

- Portafiltro **sin canastilla**, enjuagado bajo la llave, secado y montado
  **1 min** antes de pulsar, siempre igual.
- Se pulsa **café** (perfil v11, 20 s). Sin canastilla la bomba queda al tope y
  el agua sale libre (E1: 6,8 bar y 3,3 g/s al 75 %); la presión no se regula,
  igual en todos.
- NTC bajo el pico, en el sitio de siempre, leído a los 10, 13, 16 y 19 s; masa
  en la báscula para comprobar que salió la misma agua.
- Esperas de 20, 2, 5 y 10 min desde la vuelta a «lista», en ese orden.
- **Cambios del operador antes del primer uso (12:30):** se pulsa a la hora
  exacta con la placa en «lista», aunque el metal esté fuera de la banda por la
  oscilación de reposo, y se anota el metal al pulsar; esperar a la banda haría
  desigual la espera. La tara la hace el operador por la placa unos 15 s antes, y
  el operador marca el montaje del portafiltro 60 s antes de pulsar, para que no
  varíe el tiempo de montaje.
- **Criterio:** una espera alcanza si el agua a los 19 s queda dentro del
  recorrido entre usos iguales. El 15, con el portafiltro sin canastilla, el
  agua por el pico varió 1,8 °C (78,3–80,1 °C); se toma como referencia y se
  revisa con lo que dé el de 20 min.

### C bis · resultados (medido)

Portafiltro sin canastilla, enjuagado y montado 60 s antes (marcado por
el operador), tara por la placa 15 s antes, pulsado a la hora fijada.

| Extracción | Espera real | Metal al pulsar | Agua en 20 s | NTC máximo | NTC a 19 s |
|---|---|---|---|---|---|
| sim-00000234, 12:44:16 | 20 min | 92,49 °C | 57,7 g | 81,1 °C a 6 s | 75,3 °C |
| sim-00000235, 12:47:27 | 2 min 10 s | 91,07 °C | 64,5 g | 81,1 °C a 6 s | 74,7 °C |
| sim-00000236, 12:55:48 | 5 min 15 s | 92,48 °C | 58,2 g | 82,6 °C a 7 s | 76,6 °C |
| sim-00000237, 13:07:18 | 10 min 13 s | 93,00 °C | 65,6 g | 82,6 °C a 5 s | 75,5 °C |

Sin café, el agua por el pico sube a su máximo en 5–7 s y luego baja mientras
la caldera reemplaza el agua caliente con fría. Presión máxima 7,46–7,55 bar
(sin canastilla no se regula). La vuelta a «lista» tras cada uso fue de 41 a
48 s, más rápida que con café.

**Resultado:** entre 2 y 20 min de espera, el agua no cambia más que entre usos
iguales: el máximo varió 1,5 °C (81,1–82,6 °C) y la lectura a 19 s, 1,9 °C
(74,7–76,6 °C), sin orden con la espera; el uso de 2 min dio el mismo máximo
que el de 20. Cumple el criterio fijado (recorrido del 15, 1,8 °C). **Con 2 min
en «lista» basta**; por debajo de 2 min no se midió. Tampoco se nota el metal
al pulsar entre 91,1 y 93,0 °C, dentro de esta dispersión.

## Decisión del operador (2026-10-18, 13:16): el criterio es el agua

El criterio de la temperatura pasa a ser **que el agua que sale sea pareja**, no
que el metal quede dentro de la banda de ±0,6 °C. Razón: el metal oscila cerca
de 1 °C en reposo por una perturbación térmica que no se identificó
(oscilación en reposo);
se descartaron el aire del cuarto (la sonda al aire varió 0,38 °C, sin
correlación con el metal) y, por magnitud, la tensión de red. Esa oscilación no
se nota en el agua: entre 2 y 20 min de espera, y con el metal entre 91,1 y
93,0 °C al pulsar, el agua varió 1,5 °C, lo mismo que entre usos iguales.

Con este criterio, el control actual (acta v4, sin ley del director) cumple
dentro de lo que se puede medir. La banda del metal queda como herramienta de
medida y como condición de «lista»; la ley del director, la recalibración del
lazo y el redondeo de la potencia al 1 % quedan como trabajo futuro, con su
análisis en ley-tau.

### C bis · repeticiones de 2 min y usos de 1 min (pedidas por el operador, medido)

| Extracción | Espera real | Metal al pulsar | Agua en 20 s | NTC máximo | NTC a 19 s |
|---|---|---|---|---|---|
| sim-00000238, 13:25:19 (arranque) | ~17 min | 92,58 °C | 66,7 g | 82,2 °C | 74,7 °C |
| sim-00000239, 13:30:47 | 2 min 09 s | 92,08 °C | 65,2 g | 81,2 °C | 74,8 °C |
| sim-00000240, 13:35:56 | 2 min 15 s | 92,53 °C | 65,5 g | 82,1 °C | 75,5 °C |
| sim-00000241, 13:38:46 | 1 min 06 s | 91,83 °C | ~70 g (tara rechazada; por diferencia) | 80,5 °C | 74,7 °C |
| sim-00000242, 13:42:36 | 1 min 11 s | 93,18 °C | 69,8 g | 81,7 °C | 75,9 °C |

En los usos de 1 min el portafiltro se montó al volver a «lista»; en el
sim-00000241 llevaba 44 s montado y no 60.

**Por grupo, con todos los usos con agua del día:** esperas largas (5–20 min,
4 usos) máximo 81,1–82,6 °C, media 82,1 °C, y 75,5 °C de media a 19 s; 2 min
(3 usos) 81,1–82,1 °C, media 81,5 °C, y 75,0 °C; 1 min (2 usos) 80,5–81,7 °C,
media 81,1 °C, y 75,3 °C. Los grupos se solapan y a los 19 s no hay orden. En el
máximo, las medias bajan unos 0,6 °C con 2 min y 1,0 °C con 1 min frente a las
esperas largas: puede haber un efecto pequeño de la espera sobre el agua que
estaba en la caldera, del tamaño de la dispersión entre usos iguales, que con
estos usos no se separa de ella. **Conclusión:** desde 1 min en «lista» el agua
sale dentro de la dispersión normal; si la espera corta enfría algo, es menos
de 1 °C en el primer chorro y no se nota a los 19 s.

## Curva caldera → agua sobre el café (2026-10-18, 14:10–15:15, medido)

Método propuesto por el operador: NTC fijo dentro de la cavidad del portafiltro
sin canastilla, sin tocar el metal; portafiltro montado todo el tiempo, como lo
dejan los baristas, de modo que mide el agua donde caería sobre el café, con el
portafiltro caliente. Montado en reposo se estabilizó en 71–72 °C en unos 8 min.
Por referencia, cuatro usos con agua (pulsar café, perfil de 20 s) cada uno
2 min después de la vuelta a «lista»; el primero no cuenta (parte con el
portafiltro en reposo). Perfiles v12 (88), v11 (92), v13 (96) y v14 (100),
copias del vigente con solo la temperatura cambiada, aprobados por el operador.
**Regla fijada antes de medir 100 (operador, 14:54):** si a 100 °C la media del
chorro entre 5 y 19 s llega a 92 °C, no se prueba por encima de 100.

| Referencia | Usos (sim-) | Metal al pulsar | Máximo | Media 5–19 s | A 19 s |
|---|---|---|---|---|---|
| 88 °C | 248, 249, 250 | 88,6–89,4 °C | 85,2 / 86,1 / 85,1 (85,5) | 82,0 / 81,1 / 80,8 (**81,3**) | 78,3 |
| 92 °C | 244, 245, 246 | 92,0–93,1 °C | 89,7 / 90,7 / 89,8 (90,1) | 84,8 / 84,6 / 85,1 (**84,8**) | 80,2 |
| 96 °C | 252, 253, 254 | 97,4–98,0 °C | 92,3 / 92,2 / 92,1 (92,2) | 89,2 / 89,1 / 88,4 (**88,9**) | 85,7 |
| 100 °C | 256, 257, 258 | 101,7–102,5 °C | 94,5 / 94,6 / 94,6 (94,6) | 91,9 / 92,2 / 92,3 (**92,2**) | 88,9 |

- **La media del chorro es lineal con la referencia:** agua ≈ 0,92 · referencia
  + 0,3 °C, con residuos de las medias de la tabla de 0,26 °C o menos
  (corregido el mismo día: la primera redacción decía + 0,6 °C, calculado con
  las medias sin redondear y la pendiente sin redondear; con los valores de la
  tabla, + 0,6 deja residuos de hasta 0,44 °C).
  Contra el metal filtrado al pulsar, uso a uso: 0,8063 · metal + 9,84 °C,
  rms 0,55 °C, residuo máximo 0,91 °C, metal entre 88,60 y 102,49 °C
  (redondeado antes como 0,81 · metal + 9,7).
- **El máximo sube menos** (0,74 °C por grado) y pasa de 92 °C, el punto de
  ebullición local, desde 96 °C; la media llega a 92,2 °C con 100 °C. Por la
  regla, **no se prueba por encima de 100**.
- Con 96 y 100 °C el metal quedó 1,5–2,5 °C sobre la referencia al pulsar; con
  88 y 92, dentro de la banda. No se analizó la causa.
- Taras rechazadas por el goteo del portafiltro montado: la masa de esos usos
  se tomó por diferencia; el sim-00000248 da ~44 g, dudoso. El NTC se soltó de la
  protoboard de 14:10:52 a 14:13:55 (uso de descarte sim-00000243, sin lectura).

## Comprobación con la caldera a 99,7 °C (2026-10-18, 15:56–16:12, medido)

Firmware con el acta v5 (`acta-temperatura-20261018-v5`): el perfil v15 pide
92 °C de agua y la placa pone la referencia en 99,67 °C con la recta contra la
referencia. Mismo método que la curva: NTC fijo en el portafiltro montado,
cuatro usos de agua de 20 s, cada uno 2 min tras volver a «lista», el primero
de descarte. **Criterio, fijado antes de medir:** media 5–19 s de los usos 2–4
en 92 ± 0,8 °C (incertidumbre del A1 del día) y ningún uso fuera de 92 ± 1 °C.

| Uso | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Recta contra la referencia | Recta contra el metal |
|---|---|---|---|---|---|---|
| sim-00000259 (descarte) | 99,73 °C | 92,7 | 88,1 | 82,8 | 92,0 | — |
| sim-00000260 | 100,10 °C | 93,3 | 90,3 | 86,9 | 92,0 | 90,55 |
| sim-00000261 | 100,63 °C | 94,1 | 90,7 | 87,3 | 92,0 | 90,98 |
| sim-00000262 | 100,94 °C | 94,1 | 90,9 | 87,0 | 92,0 | 91,23 |

- **No cumple:** media de los usos 2–4 en 90,6 °C, 1,4 °C bajo 92; los tres
  quedan bajo 91.
- **El agua sigue al metal al pulsar, no a la referencia.** La recta contra la
  referencia se equivoca en −1,1 a −1,7 °C; la recta contra el metal, en −0,25
  a −0,33 °C. En la curva, a 100 °C de referencia el metal quedó 1,7–2,5 °C
  por encima al pulsar; aquí, a 99,67 °C, 0,4–1,3 °C. La recta contra la
  referencia lleva dentro el exceso de aquella tarde, que no se repite.
- **Decisión del operador (16:14):** cambiar la conversión a la recta contra el
  metal (acta v6, `acta-temperatura-20261018b`) y repetir esta comprobación.
  Con ella, 92 °C de agua piden 101,9 °C de referencia.

## Comprobación con la recta contra el metal (2026-10-18, 16:28–17:10, medido)

Firmware con el acta v6 (`acta-temperatura-20261018b-v6`): el perfil v15 pide
92 °C de agua y la placa pone la referencia en 101,90 °C. Mismo método y
criterio que la comprobación anterior. Después, a petición del operador, un uso
justo al llegar a «lista» y, por recomendación propia, usos seguidos hasta que
tres quedaran dentro de 0,3 °C entre sí (regla fijada antes de seguir; tope en
el uso 12).

| Uso | Espera en «lista» | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa |
|---|---|---|---|---|---|---|
| sim-00000263 (descarte) | 2 min | 101,78 °C | 93,1 | 88,1 | 82,8 | 66,3 g |
| sim-00000264 | 2 min | 102,24 °C | 94,0 | 90,3 | 86,8 | — |
| sim-00000265 | 2 min | 102,39 °C | 94,1 | 90,7 | 87,0 | 59,8 g |
| sim-00000266 | 2 min 30 s | 103,38 °C | 94,2 | 91,7 | 88,4 | 68,4 g |
| sim-00000267 | al llegar | 101,41 °C | 92,8 | 89,7 | 86,3 | sin tara |
| sim-00000268 | 57 s | 103,21 °C | 94,1 | 91,4 | 88,0 | sin tara |
| sim-00000269 | 2 min | 104,22 °C | 94,3 | 92,2 | 89,1 | 58,4 g |
| sim-00000270 | 4 min | 104,65 °C | 94,3 | 92,4 | 89,5 | 63,3 g |
| sim-00000271 | 2 min | 103,93 °C | 94,3 | 92,1 | 88,8 | 72,5 g |

- **Los usos 2–4 no cumplen** (90,9 °C de media). La recta contra el metal
  se equivoca en −1,6 a −2,0 °C en ellos; con la v5 había acertado en −0,3.
- **En uso continuo el agua se estabiliza en 92,2 °C** (usos 7–9: 92,2 / 92,4
  / 92,1, dentro de 92 ± 0,8), con el metal en 103,9–104,7 °C.
- **Calentamiento inicial:** tras un reposo largo el agua sube uso a uso
  (88,1 → 90,3 → 90,7 → 91,7 → 92,2); hacen falta unos 5 usos, unos 20 min.
  Pulsar al llegar a «lista» da 1–2 °C menos que esperar 2 min.
- **El máximo se pega al hervor**, 94,0–94,3 °C, en todos los usos con el metal
  sobre 102 °C: una media de unos 92 °C es casi lo más que da este método.
- **En uso seguido el metal queda 1,5–2,8 °C sobre la referencia.** En reposo
  tras el uso 7, a las 17:03, el calentador seguía al 3,4 % con la integral en
  5,6 % y el error en −3 °C: es la integral cargada durante los usos, que se
  descarga despacio (el sobrepaso de la segunda enmienda del 2026-10-16 de
  ADR-009). Llegó a 105,0 °C y ya bajaba.
- **Inferido:** la curva a 100 °C se midió después de una hora de usos seguidos,
  con el grupo caliente y el metal sobre la referencia; por eso dio 92,2 °C con
  una referencia más baja.

## Ensayo de reposo (2026-10-18, noche, desde las 21:18, medido)

Protocolo del hilo de la plataforma, aprobado por el operador: mismo método
que la curva (NTC-3 fijo en la cavidad del portafiltro sin canastilla, montado
toda la sesión, agua sola, usos de 20 s), perfil v15 (92 °C de agua →
referencia 101,90 °C con el acta v6). Cinco usos tras cada reposo, cada uno
2 min después de volver a «lista»; el reposo se cuenta desde el fin del último
uso. **Fijado antes de medir:** se ajusta con los reposos de 20, 40 y 10 min y
el último de 20 min queda reservado; en cada uso reservado, la media 5–19 s y
la subida entre usos seguidos deben acertar con 1,01 °C o menos. La máquina
llevaba ~4 h 9 min en «lista» desde el sim-00000271 (17:08:56), con el NTC
grabando: ese reposo cuenta como un punto adicional de reposo largo y
reemplaza los 5 usos de calentamiento del protocolo.

| Reposo | Uso | Pulsado | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|
| ~4 h 9 min | sim-00000272 | 21:18:13 | 101,87 °C | 92,8 | 87,6 | 81,9 | 68,3 g |
| | sim-00000273 | 21:24:41 | 102,21 °C | 94,2 | 88,8 | 83,4 | 67,5 g |
| | sim-00000274 | 21:30:13 | 102,99 °C | 94,3 | 91,4 | 87,7 | 62,6 g |
| | sim-00000275 | 21:35:07 | 103,52 °C | 94,3 | 91,5 | 87,8 | 74,5 g |
| | sim-00000276 | 21:39:31 | 103,66 °C | 94,3 | 91,5 | 87,7 | 66,3 g |
| 20 min | sim-00000277 | 22:00:05 | 102,44 °C | 93,2 | 88,7 | 83,6 | 66,1 g |
| | sim-00000278 | 22:06:24 | 102,69 °C | 94,2 | 90,7 | 87,2 | 66,2 g |
| | sim-00000279 | 22:11:28 | 102,84 °C | 94,4 | 91,2 | 87,6 | 59,8 g |
| | sim-00000280 | 22:16:13 | 103,35 °C | 94,3 | 91,2 | 87,4 | 65,1 g |
| | sim-00000281 | 22:20:40 | 103,57 °C | 94,4 | 91,3 | 87,5 | 69,2 g |

El uso 3 tras el reposo largo se pulsó con 2 min 33 s de espera (YA tardío).
| 40 min (1.ª vuelta) | sim-00000282 | 23:01:28 | 101,59 °C | 92,8 | 89,0 | 83,8 | 62,4 g |
| | sim-00000283 (no vale) | 23:07:54 | 102,71 °C | 98,9 | 95,4 | 91,4 | 63,0 g |
| | sim-00000284 (no vale) | 23:13:11 | 102,77 °C | 98,9 | 95,8 | 92,2 | 35,0 g |
| 40 min (2.ª vuelta, dudosa) | sim-00000285 | 23:53:46 | 101,92 °C | 94,0 | 88,9 | 83,5 | 64,6 g |
| | sim-00000286 | 00:00:37 | 102,72 °C | 94,9 | 91,6 | 87,8 | 67,1 g |
| | sim-00000287 | 00:05:47 | 102,89 °C | 94,9 | 92,3 | 89,1 | 75,3 g |
| | sim-00000288 | 00:10:31 | 102,75 °C | 95,1 | 92,3 | 88,8 | — |
| | sim-00000289 | 00:15:00 | 103,66 °C | 95,1 | 92,1 | 88,6 | 70,0 g |

**El NTC-3 se corrió en el reposo de 40 min.** Desde las 23:07 leyó unos
4,5 °C de más (meseta del hervor en 98,9 °C, imposible con la cavidad abierta)
y en el 284 la masa salió a la mitad. Con la resistencia fija de 10 kΩ en su
lugar la placa leyó 1720–1725 mV (1703–1706 el 14 y el 16): la placa está bien.
Reconectado, en reposo quedó 0,5–0,8 °C sobre los reposos anteriores a la
misma altura, y en los usos la meseta del hervor pasó de 94,2–94,4 °C a
94,9–95,1 °C, creciendo despacio: corrimiento del sensor o de su conexión
(inferido; la prueba de la taza lo confirmaría). **Control fijado antes de la
2.ª vuelta:** máximo del uso 1 ≤ 93,7 °C y de los usos 3 a 5 ≤ 94,6 °C;
fallaron los dos, así que la 2.ª vuelta del reposo de 40 min queda **dudosa**
(no entra en el ajuste sin esa marca) y no se repitió el mismo día, por
decisión del operador. Los reposos de 10 min y el segundo de 20 min
(reservado) se harán otro día: calentar y 5 usos sin contar, reposo de 10 y 5
usos, reposo de 20 y 5 usos, con el sensor comprobado antes y la temperatura
del cuarto y del depósito anotadas.

## 2026-10-19, mañana: arranque en frío y NTC-3 (medido)

- **Arranque en frío con la referencia en 101,90 °C** (acta v6, perfil v15 de
  92 °C de agua): encendida a las 07:29:49 desde 23,5 °C, «lista» a las
  07:45:40, **15 min 51 s**. El arranque rápido no actuó, porque su referencia
  máxima es 93 °C (acta v4 en adelante); calentó el PI con su techo del 10 %.
  Tras «lista», el metal llegó a 103,13 °C (1,2 °C sobre la referencia). Con
  92 °C de referencia, el arranque rápido llegaba en ~8 min y el PI solo en
  ~17 min (ADR-009, enmienda del 2026-10-11).
- **A1 del NTC-3 en la taza** (07:46–08:08, método del 11, `a1-ntc3-20261019.txt`):
  613 puntos entre 61,5 y 77,8 °C, NTC − sonda de −0,04 °C de media (de −0,52
  a +0,36): **cumple**. El corrimiento de +0,5–0,7 °C de la noche del 18 ya no
  está; al aire, antes de la taza, quedó en +0,3 °C. Lo más probable es que
  fuera la conexión en la protoboard (inferido).

## Ensayo de reposo, segunda parte (2026-10-19, desde las 08:15, medido)

Mismo método y partición que la primera parte, con el NTC-3 comprobado en la
taza esa mañana y montado de nuevo en la cavidad del portafiltro; sonda R al
aire, como el 18 (24,4 °C a las 09:09). Firmware con la telemetría durante el
café y el minuto previo a pulsar (e102ab5, d8a8b66): en el primer uso la placa
entregó 19 muestras durante el café y no perdió ninguna. Orden: calentamiento
de 5 usos sin contar, reposo de 10 min, reposo de 40 min y, al final, el
segundo reposo de 20 min (reservado).

| Reposo | Uso | Pulsado | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|
| calentamiento (no cuenta) | sim-00000290 | 08:15:27 | 101,66 °C | 90,2 | 85,2 | 78,9 | 68,8 g |
| | sim-00000291 | 08:23:07 | 102,90 °C | 93,5 | 88,1 | 82,2 | 68,2 g |
| | sim-00000292 | 08:28:45 | 103,83 °C | 94,2 | 91,0 | 87,1 | 64,0 g |
| | sim-00000293 | 08:33:34 | 103,58 °C | 94,1 | 91,2 | 86,9 | 71,2 g |
| | sim-00000294 | 08:38:11 | 103,85 °C | 94,2 | 91,4 | 87,9 | 80,0 g |
| 10 min | sim-00000295 | 08:48:51 | 104,27 °C | 93,9 | 91,3 | 87,9 | 67,0 g |
| | sim-00000296 | 08:54:24 | 102,91 °C | 94,2 | 91,4 | 87,7 | 67,9 g |
| | sim-00000297 | 08:59:15 | 103,62 °C | 94,3 | 91,1 | 87,6 | 65,0 g |
| | sim-00000298 | 09:03:49 | 103,25 °C | 94,3 | 91,3 | 87,5 | 58,6 g |
| | sim-00000299 | 09:08:20 | 104,05 °C | 94,2 | 91,4 | 88,2 | 58,1 g |

- **Tras 10 min de reposo el primer uso no sale frío** (91,3 °C, igual que el
  uso continuo); tras 20 min salía 2,6 °C más frío. El metal al pulsar el
  primer uso estaba en 104,3 °C, más alto que tras los otros reposos.
- La meseta del hervor quedó en 94,1–94,3 °C en todos los usos calientes, la
  de los usos válidos del 18: el NTC-3 lee como entonces.
- El sim-00000296 se pulsó 40 s después de la hora fijada.

| Reposo | Uso | Pulsado | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|
| 40 min | sim-00000300 | 09:48:57 | 101,96 °C | 92,3 | 88,5 | 83,5 | 78,8 g |
| | sim-00000301 | 09:55:24 | 102,39 °C | 94,1 | 91,4 | 88,2 | 63,7 g |
| | sim-00000302 | 10:00:48 | 102,97 °C | 94,3 | 91,7 | 88,7 | 65,6 g |
| | sim-00000303 | 10:05:34 | 103,45 °C | 94,2 | 92,0 | 88,4 | 64,8 g |
| | sim-00000304 | 10:10:09 | 103,24 °C | 94,3 | 92,0 | 88,6 | 56,0 g |
| 1 h 30 min (comprobación) | sim-00000305 | 11:41:11 | 102,10 °C | 92,9 | 89,7 | 85,7 | 49,1 g* |
| | sim-00000306 | 11:48:08 | 102,49 °C | 94,2 | 90,6 | 85,8 | 58,6 g |
| | sim-00000307 | 11:53:33 | 102,60 °C | 94,4 | 91,8 | 88,6 | 61,5 g |
| | sim-00000308 | 11:58:37 | 103,45 °C | 94,3 | 91,7 | 88,6 | 74,3 g |
| | sim-00000309 | 12:03:14 | 103,89 °C | 94,3 | 92,1 | 88,6 | 73,4 g |

\* Tara hecha ~20 s antes de que el operador moviera el recipiente; masa dudosa.

- **Reposo de 40 min:** solo el primer uso sale frío (88,5 °C, 2,9 °C bajo el
  estable); desde el segundo, 91,4–92,0 °C.
- **Cambio de plan (operador, 10:11):** el reposo de 20 min reservado se
  aplazó; en su lugar quedó un reposo de 1 h 30 min (desde el fin de 304,
  10:10:29), declarado **de comprobación** antes de medirlo. Después, otro
  reposo largo (desde el fin de 309, 12:03:34) y el de 20 min reservado.
- **Cola de la placa (08:59–09:21):** la plataforma del PC no respondía; la
  cola de 32 huecos se llenó con el sim-00000296 (7 de 18 tramos) y las series
  de 297–299 se perdieron; sus resúmenes se entregaron al cerrar el 300. Los
  datos de la placa están completos en el registro del PC. Corregido en el
  firmware (925c240, sin cargar aún).

## Ensayo de reposo, tercera parte (2026-10-19, tarde, medido)

Mismo método: perfil v15, cinco usos de agua de 20 s tras cada reposo, cada uso
2 min después de volver a «lista», reposo contado desde el fin del último uso.
Reposo largo desde el fin de 309 (12:03:34) y, a continuación, el segundo reposo
de 20 min (el reservado de la v4).

| Reposo | Uso | Pulsado | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|
| 3 h 13 min | sim-00000310 | 15:18:26 | 101,90 °C | 93,0 | 89,5 | 85,2 | 52,6 g |
| | sim-00000311 | 15:25:23 | 102,53 °C | 94,1 | 92,3 | 89,3 | 57,4 g |
| | sim-00000312 | 15:30:27 | 102,52 °C | 94,3 | 92,4 | 89,4 | 61,6 g |
| | sim-00000313 | 15:35:28 | 103,56 °C | 94,2 | 92,2 | 89,5 | 55,1 g |
| | sim-00000314 | 15:40:18 | 103,48 °C | 94,3 | 92,3 | 89,5 | 68,5 g |
| 20 min (reservado) | sim-00000315 | 16:00:50 | 102,03 °C | 93,4 | 90,4 | 86,6 | 57,0 g |
| | sim-00000316 | 16:07:33 | 102,23 °C | 94,2 | 92,3 | 89,7 | 60,6 g |
| | sim-00000317 | 16:12:41 | 102,72 °C | 94,2 | 92,2 | 89,3 | 59,5 g |
| | sim-00000318 | 16:17:16 | 102,94 °C | 94,1 | 92,1 | 89,0 | 58,6 g |
| | sim-00000319 | 16:21:34 | 103,03 °C | 94,2 | 92,0 | 88,6 | 63,1 g |

- **Tras 3 h 13 min y tras 20 min, solo el primer uso sale frío** (2,8 y 1,8 °C
  bajo los recuperados); desde el segundo, 92,0–92,4 °C. Tras 1 h 30 min el
  segundo uso aún salía 1,2 °C bajo el estable (90,6 °C): la recuperación no
  tiene siempre la misma forma.
- Los usos recuperados de la tarde leen 0,6–1,0 °C más que los del 18
  (91,0–91,7 °C), con el NTC-3 en otro montaje.

### Modelo del agua frente a los reposos del 19 (medido y simulado)

Detalle en `models/modelica/agua_portafiltro/` (`datos.py`, `ajustar_reposo.py`,
`comprobacion-reposo-*.txt`).

- **v5, comprobación ciega en el reposo de 3 h 13 min** (`comprobacion-reposo-largo2.txt`):
  ninguna variante cumple. Pe acierta el primer uso (88,9 frente a 89,2 °C) y
  falla el segundo (90,3 frente a 92,0 °C).
- **v6** (cb777da, fijada antes de ajustar y de medir el reposo de 20 min): el
  18 sale del ajuste por el otro montaje del NTC-3; ajustan los reposos de 10 min,
  40 min, 1 h 30 min y 3 h 13 min del 19. Las cuatro variantes siguen subiendo
  como mucho 1,3 °C del primer al segundo uso donde la máquina sube 2,7–2,8 °C.
- **Variante «Est»**, agua estancada que el primer uso renueva (3030bb5, fijada
  antes de medir el reposo de 20 min): el volumen cae a su cota inferior (1 ml);
  los datos no la piden.
- **v6, comprobación ciega en el reposo de 20 min** (`comprobacion-reposo-v6-20b.txt`):
  ninguna variante cumple todo. Pe: primer uso 90,55 frente a 90,38 °C
  (+0,17); subida al segundo 0,90 frente a 1,98 °C (error −1,08, sobre el
  margen de 1,01); usos 3–5 dentro. Por la regla fijada, ninguna se entrega.

## Compensación del primer uso (2026-10-19, tarde, medido)

Firmware de identificación `ensayo-primer-uso` (8f9aa9e; máximo provisional
4 °C, `CONFIG_CAFE_FIRST_USE_OFFSET_MAX_DECI_C=40`), cargado a las 16:28 con la
máquina conectada; la placa arrancó apagada y se encendió a las ~16:40. La
plataforma del PC pasa `boilerOffsetC` del perfil a la placa (2fbc282). Perfil
v16 = v15 con `boilerOffsetC: 3.0`, activado a las 17:01:52; la placa lo toma
en esa sincronización («primer uso +3.00 C … referencia 104.90 C») y vuelve a
«lista» a las 17:03:37. Al terminar el primer uso la placa retira la subida
sola («desplazamiento a 0, referencia de caldera 104.90 C -> 101.90 C»); v15 se
reactiva a las 17:06:41 y la placa lo toma a las 17:06:42.

| Reposo | Uso | Pulsado | Subida | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|---|
| 20 min desde «lista» (16:44:52) | sim-00000320 | 17:06:02 | +3,0 °C | 104,53 °C | 93,7 | 91,6 | 87,9 | 42,7 g* |
| | sim-00000321 | 17:11:56 | — | 102,63 °C | 94,1 | 92,1 | 89,5 | 56,0 g |
| | sim-00000322 | 17:16:43 | — | 102,35 °C | 94,2 | 91,9 | 89,0 | 58,1 g |
| | sim-00000323 | 17:21:17 | — | 102,79 °C | 94,3 | 92,0 | 89,0 | 72,6 g† |
| | sim-00000324 | 17:25:41 | — | 103,44 °C | 94,3 | 92,0 | 89,3 | 61,3 g |

\* La taza rebosó (operador): la báscula no vio toda el agua. Para el modelo se
toma el caudal medio de 319 y 321 (2,95 g/s).
† Masa alta con presión y tiempo normales; sin causa observada.

- **El primer uso compensado queda a 0,4 °C de los recuperados** (91,6 frente a
  92,0 °C de media de 321–324); tras 20 min sin subida, la diferencia era de
  1,8 °C (315 frente a 316–319). El reposo previo no es igual: este empieza en
  «lista» tras un apagado de unos 12 min, no tras cinco usos.
- **Pe v6 sin reajustar** predice 91,72 °C para el 320 con lo medido (error
  −0,11 °C). Con el metal y el agua de la caldera 3 °C más bajos desde la
  activación hasta el fin del uso, 89,16 °C: los 3 °C de caldera dan unos
  2,5 °C de agua (0,85 °C por °C, la recta del acta v6 da 0,81). Ese
  contrafactual es una aproximación: resta los 3 °C al metal medido en lugar
  de simular el lazo.

### Reposo de 30 min con la subida que predice el modelo (medido)

Predicción y criterio fijados antes de medir (`prediccion-subida-30min.txt`,
96133c8): Pe v6 sin reajustar, con un futuro sintético de la caldera, predice
90,17 °C para el primer uso sin subida y 92,07 °C para los usos recuperados;
pide 2,4 °C de caldera, con primer uso predicho de 92,05 °C. Criterio: el
primer uso a ≤ 1,01 °C de la media de los usos 2 a 5. Perfil v17 = v15 con
`boilerOffsetC: 2.4`, activado a las 17:51:51 (la placa lo toma a las 17:51:52,
referencia 104,30 °C; «lista» a las 17:53:19). Reposo desde el fin de 324
(17:26:01). La placa retira la subida al terminar el primer uso; v15 se
reactiva a las 17:56:54.

| Uso | Pulsado | Subida | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|
| sim-00000325 | 17:56:15 | +2,4 °C | 104,45 °C | 93,4 | 90,9 | 88,1 | 61,0 g |
| sim-00000326 | 18:02:11 | — | 102,60 °C | 94,1 | 91,6 | 88,7 | 59,3 g |
| sim-00000327 | 18:07:36 | — | 102,83 °C | 94,3 | 91,7 | 88,4 | 55,9 g |
| sim-00000328 | 18:12:15 | — | 103,25 °C | 94,3 | 91,9 | 88,4 | 61,1 g |
| sim-00000329 | 18:16:44 | — | 103,26 °C | 94,1 | 91,9 | 88,3 | 61,2 g |

- **La compensación cumple el criterio.** Con las medias del extracto
  (`extracto.csv.xz`, las del guion del modelo): primer uso 90,92 °C; usos
  2–5 91,72 / 91,62 / 91,81 / 91,75 °C, media 91,73 °C; diferencia 0,80 °C.
- **La cifra pedida se queda corta.** El primer uso sale 1,13 °C bajo lo
  predicho (92,05). Con el metal medido en lugar del futuro sintético, Pe da
  91,69 °C (error del modelo +0,77 °C); el resto (+0,36 °C) viene del futuro
  sintético de la caldera. Con la ganancia de 0,76–0,85 °C de agua por °C de
  caldera, cerrar los 0,8 °C restantes habría pedido ~1 °C más.
- Los usos recuperados de este reposo leen 0,3–0,6 °C menos que los de las
  15:00–17:30 (91,6–91,9 frente a 92,0–92,4 °C); la sonda R al aire baja de
  27,1–27,3 °C (16:00–17:05) a 26,4 °C (18:08). La relación es inferida.

| Reposo | Subida | Primer uso | Usos 2–5 | Diferencia |
|---|---|---|---|---|
| 20 min (315–319) | — | 90,4 | 92,2 | 1,8 |
| 20 min desde «lista» (320–324) | +3,0 °C, fija | 91,6 | 92,0 | 0,4 |
| 30 min (325–329) | +2,4 °C, del modelo | 90,9 | 91,7 | 0,8 |

Guion de la comprobación con lo medido y del contrafactual del 320:
`models/modelica/agua_portafiltro/comprobar_subida.py`.

### Segundo reposo de 30 min, con el modelo reajustado (medido)

Pedido por el operador (18:19), que no queda conforme con el anterior. Antes de
medir: partición v7 (650d269, ajustan todos los usos del 19 desde el 295, con los
reposos con subida); predicción con la plantilla del reposo de 30 min anterior
(`prediccion-subida-30min-b.txt`, 8b3299b). Pe v7 pide 2,5 °C; en los primeros
usos de los siete reposos del 19 el modelo sale +0,45 °C sobre lo medido frente a
+0,15 °C en los usos 3–5, y con esa corrección se piden **2,9 °C**. Criterio
cambiado por el operador antes de medir (9daa385): **≤ 0,5 °C**, porque la
repetibilidad de los usos recuperados del 19 es 0,11 °C (desviación agrupada de
los usos 3–5), no los 0,50 °C de la curva del 18 de la que salía el 1,01.
Perfil v18 = v15 con `boilerOffsetC: 2.9`, activado a las 18:42:54 (referencia
104,80 °C; «lista» a las 18:44:57); v15 de nuevo a las 18:47:52. Reposo desde el
fin del 329 (18:17:04).

| Uso | Pulsado | Subida | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|
| sim-00000330 | 18:47:13 | +2,9 °C | 104,91 °C | 93,0 | 91,3 | 87,9 | 63,7 g al corte* |
| sim-00000331 | 18:52:56 | — | 102,24 °C | 94,1 | 91,7 | 88,9 | 56,5 g |
| sim-00000332 | 18:58:02 | — | 102,66 °C | 94,3 | 91,8 | 88,9 | 62,0 g |
| sim-00000333 | 19:02:31 | — | 103,14 °C | 94,3 | 92,0 | 88,5 | 62,2 g |
| sim-00000334 | 19:06:49 | — | 103,48 °C | 94,3 | 91,8 | 88,7 | 62,5 g |

\* La báscula cae a −124 g tras el corte: el recipiente se movió; el caudal del
modelo sale de la pendiente antes del corte (3,20 g/s).

- **No cumple el criterio de 0,5 °C.** Medias del extracto: primer uso
  91,11 °C; usos 2–5 91,72 / 91,87 / 91,83 / 91,83 °C, media 91,81 °C;
  diferencia **0,70 °C** (con +2,4 °C fue 0,80 °C).
- Medio grado más de caldera mejora el primer uso en 0,1 °C; el modelo esperaba
  ~0,35 °C. Con dos subidas tan cercanas y una variación entre reposos de
  0,1–0,2 °C no se separa la ganancia del ruido.
- En los dos reposos de 30 min, Pe predice el primer uso compensado casi 1 °C
  sobre lo medido (325: +0,77 con lo medido; 330: +0,94 con Pe v6). Nunca se
  midió un reposo de 30 min sin subida: el déficit sin compensar a 30 min solo
  lo da el modelo.
- Corrección a la predicción del primer reposo de 30 min (96133c8): el guion
  sustituía el caudal del 320 después de simular, así que aquella predicción
  corrió con el caudal leído de la báscula rebosada (1,91 g/s). En la v7 la
  sustitución está en `datos.py` y entra en la simulación.

### Tercer reposo de 30 min, con la subida máxima del binario de ensayo (medido)

Pedido por el operador tras la prueba anterior (19:10), para separar la ganancia
del ruido con una subida claramente distinta. Perfil v19 = v15 con
`boilerOffsetC: 4.0`, el máximo provisional del binario; la placa lo acepta a
las 19:33:00 (referencia 105,90 °C; «lista» a las 19:35:31) y retira la subida al
terminar el primer uso; v15 de nuevo a las 19:37:52. Reposo desde el fin del 334
(19:07:09). Criterio, el de la prueba anterior: ≤ 0,5 °C.

| Uso | Pulsado | Subida | Metal al pulsar | Máximo | Media 5–19 s | A 19 s | Masa asentada |
|---|---|---|---|---|---|---|---|
| sim-00000335 | 19:37:31 | +4,0 °C | 106,05 °C | 93,7 | 92,0 | 89,4 | 58,7 g |
| sim-00000336 | 19:42:50 | — | 101,95 °C | 94,3 | 91,8 | 88,9 | 55,2 g |
| sim-00000337 | 19:47:53 | — | 102,88 °C | 94,3 | 92,0 | 88,2 | 63,6 g |
| sim-00000338 | 19:52:41 | — | 103,33 °C | 94,3 | 91,7 | 88,5 | 57,3 g |
| sim-00000339 | 19:57:21 | — | 103,68 °C | 94,3 | 91,9 | 88,8 | 57,2 g† |

† La báscula marca 7,9 g tras la tara del 339; la masa asentada es la diferencia
desde ese valor.

- **Cumple el criterio.** Medias del extracto: primer uso 91,97 °C; usos 2–5
  91,85 / 91,61 / 91,73 / 91,91 °C, media 91,78 °C; el primer uso queda
  **0,20 °C por encima** de los recuperados.

| Reposo de 30 min | Subida | Primer uso − media de los usos 2–5 |
|---|---|---|
| 325–329 | +2,4 °C (Pe v6) | −0,80 °C |
| 330–334 | +2,9 °C (Pe v7 con corrección de sesgo) | −0,70 °C |
| 335–339 | +4,0 °C (máximo del binario) | +0,20 °C |

- Ganancia del primer uso con la subida, de las tres pruebas: el primer uso
  sube de 90,92 a 91,97 °C entre +2,4 y +4,0 °C, unos 0,66 °C de agua por °C de
  caldera (pendiente de dos puntos extremos; los recuperados de los tres
  reposos están en 91,73–91,81 °C). Pe suponía 0,69–0,85.
- Con los déficits sin compensar medidos el 19 (1,8 °C tras 20 min; 2,8–2,9 °C
  tras 40 min y tras 3 h 13 min) y esa ganancia, el máximo de 4 °C cubre hasta
  unos 30 min y se queda ~0,5 °C corto en los reposos largos (inferido).

### Reposo largo con la subida máxima (medido; uso no válido)

Reposo desde el fin del 339 (19:57:41). La activación prevista a las 20:53:31 se
canceló a pedido del operador; v19 (+4,0 °C) se activó a las 21:18:45 (referencia
105,90 °C, «lista» a las 21:21:28) y el sim-00000340 se pulsó a las 21:23:3x,
tras 1 h 25 min de reposo; la placa retiró la subida al terminar y v15 se
reactivó a las 21:24:01.

- **El NTC-3 no vale desde las 20:41:40:** la lectura salta de 1604 a 2730 mV en
  una muestra, en pleno reposo y sin acción en la máquina, y queda en
  2430–2640 mV (antes, 1600–1760 mV en «lista»). En el 340 lee 129–131 °C, por
  encima de la ebullición: el uso no vale para temperatura. Mismo tipo de falla
  de conexión que la noche del 18. No se siguió con los usos 2–5. El salto no se
  avisó al momento: el reposo no tenía vigilante del sensor.
- Del 340 vale lo de la placa: metal al pulsar 106,01 °C, presión máx. 7,64 bar,
  masa asentada 37,8 g.
- Pendiente para el 20: revisar la conexión del NTC-3 con la prueba de la
  resistencia (1720–1725 mV, sección «2026-10-19, mañana») y repetir el reposo
  largo con la subida máxima.

## Acta v7 y firmware de operación (2026-10-19, noche)

Acta `acta-temperatura-20261019` (v7, c086c0d, etiqueta firmada): la v6 sin
cambios más `first_use_offset.max_offset_c = 4,0 °C`, con lo medido y lo no
probado declarados. Planta generada (8a667b8) y `operacion-previas` compilado con
925c240, 8f9aa9e y el acta v7, sin máximo provisional; cargado a las 21:32. La
placa arranca con «Primer uso: máximo de 4.00 C (acta-temperatura-20261019-v7)».

## Arranque rápido con la referencia de caldera de 101,9 °C (2026-10-20, medido)

El arranque rápido no actuaba desde el 18-oct: el acta limita su referencia a
`fast_start.max_setpoint_c` = 93 °C (las corridas de su ajuste fueron con
referencia 92 °C) y, desde que el perfil pide el agua, la caldera va a 101,9 °C.
Pedido por el operador (08:43): que actúe con la configuración nueva.

Binario de identificación `arranque-101` (HEAD 4…, acta v7) con el arranque rápido
provisional y los números del acta salvo la referencia máxima: δ = 20 °C, banda
42 °C, 70 s, **referencia hasta 103 °C**, espera hasta caer 0,2 °C; máximo de la
subida del primer uso, el del acta (4,0 °C). Máquina apagada desde las 21:32 del
19; metal a 22,97 °C al cargar. Perfil v15 (agua 92 °C → caldera 101,9 °C).

| Hora | Evento | Metal |
|---|---|---|
| 08:52:32 | encendido; el arranque rápido arma (100 %) | 23,0 °C |
| 08:53:28 | corte del 100 % (56 s) | 81,7 °C |
| 08:53:47 | «lista» (la placa la declara) | 101,9 °C |
| 08:53:56 | máximo, a 0 % | 102,63 °C |
| 08:54:14 | «lista» perdida (`coffee-temperature-lost`) | — |
| 08:55:58 | mínimo, con el PI | 92,17 °C |
| 09:01:02 | «lista» de nuevo | 101,9 °C |

- **«Lista» definitiva a los 8 min 30 s del encendido**, frente a 15 min 51 s
  del arranque en frío del 19 sin arranque rápido.
- **La primera «lista», a los 1 min 15 s, es falsa:** tras el corte el metal
  sube 20,9 °C por la inercia del calentador (12–17 °C con referencia 92 °C,
  enmienda de ADR-009 del 2026-10-13), toca la referencia y la pasa 0,7 °C; el
  agua de la caldera, fría, se lleva ese calor y el metal cae a 92,2 °C. Un café
  pulsado en esa ventana de 27 s saldría frío. No se firma así.
- Decisión del operador (09:27): probar δ = 25 °C (corte en ~76,9 °C) desde frío.
  Con el arrastre medido, el máximo quedaría hacia 98 °C, bajo la banda de
  «lista» (inferido). Binario `arranque-101-d25` compilado para el 21.
- El NTC-3 lee 328 mV con el portafiltro frío tras el reinicio del computador y
  de la placa auxiliar (07:55) y sube con el calentamiento (1185 mV a las 09:00):
  vuelve a dar lecturas plausibles. La prueba de la resistencia sigue pendiente.

## Arranque rápido con δ = 25 °C desde frío (2026-10-21, noche, medido)

Binario de identificación `arranque-101-d25`: el de `arranque-101` con el corte en
referencia − 25 °C (resto igual: banda 42 °C, 70 s, referencia hasta 103 °C,
espera hasta caer 0,2 °C). Perfil v15 (agua 92 °C → caldera 101,9 °C). En la
plataforma, el sostenimiento suspendido durante la prueba
(`suspension-627e6a3b0dc6`) para que la subida del primer uso no cambie la
referencia. Máquina apagada al menos desde las 21:45 según la telemetría;
metal a 24,3 °C al encender. Registro: `registros/arrastre-20261011/maquina.csv`.

**Criterio, fijado antes de encender:** (1) tras la primera «lista» la placa no
la pierde en 10 min; (2) la caldera no pasa de 102,5 °C (referencia + 0,6 °C);
(3) «lista» definitiva antes de 15 min 51 s (arranque en frío sin arranque rápido
del 19).

| Hora (local) | Evento | Metal |
|---|---|---|
| 23:07:58 | encendido | 24,6 °C |
| +6 s | arranque rápido al 100 % | — |
| +59 s | corte del 100 % (53 s) | 76,8 °C |
| +88 s | pico tras el corte | 97,91 °C |
| +2 min 49 s | mínimo, con el PI | 91,31 °C |
| 23:15:47 (+7 min 49 s) | «lista» | ~101,3 °C |
| +10 min 25 s | sube sobre la referencia en «lista» | 102,66 °C |
| hasta +17 min 49 s | máximo, 102,95 °C; «lista» sin perder | 102,6–102,9 °C |

- **(1) Cumple:** ninguna «lista» perdida en los 10 min siguientes.
- **(2) No cumple tal como se escribió:** el máximo fue 102,95 °C (referencia +
  1,05 °C), pero en «lista», de 2 a 10 min después de llegar, con el
  calentador en 3–4 %. Tras el corte del arranque rápido el pico fue 97,91 °C,
  4 °C por debajo de la referencia: el arrastre ya no produce «lista» falsa. La
  subida en «lista» es la oscilación de la espera que dio la banda de ±2,1 °C
  (`evidencias/estabilidad-espera-20261022`), no del arranque rápido (inferido).
- **(3) Cumple:** 7 min 49 s, frente a 15 min 51 s sin arranque rápido y 8 min
  30 s con δ = 20 °C el 20 (que además dio la «lista» falsa a 1 min 15 s).

## Subida automática del primer uso en la máquina (2026-10-22, madrugada, medido; guion 11.6)

Firmware de operación con el acta v8; plataforma del PC con `primer-uso-v1`
(205972b). Último uso anterior: sim-00000342, el 20; la plataforma pide 3,7 °C
(`beyond-measured`) desde que se reanudó el sostenimiento (23:45:49).

**Criterio, fijado antes de medir:** (1) la placa llega a «lista» con la
referencia subida; (2) al terminar el café quita la subida y vuelve a 101,9 °C;
(3) el resumen dice la subida aplicada y la plataforma deja de pedirla.

- 23:45:49: «primer uso +3,70 °C (vuelve a 0 al terminar la próxima preparación)
  -> referencia 105,59 °C».
- Encendido hacia las 00:00; el arranque rápido no actúa (referencia sobre el
  tope de 103 °C del acta v8) y calienta con el PI.
- 00:11:12: «lista» con la caldera en 105,0 °C. **(1) Cumple.**
- 00:11:45: café (sim-00000343, 19,9 s). 00:12:06: «Primer uso: la preparación
  terminó; desplazamiento a 0, referencia de caldera 105.60 C -> 101.90 C»; la
  plataforma decide «reposo corto» (00:12:07) y la placa acepta «desplazamiento
  de caldera pedido 0.00 C» (00:12:08). **(2) Cumple.**
- Resumen de sim-00000343: `boilerOffsetAppliedC` 3,7. **(3) Cumple.**
- NTC-3 en el portafiltro: 87,2 °C antes de pulsar y hasta 101,7 °C en el chorro,
  por encima del hervor local (94,2 °C) con la cavidad abierta: lee de más, como el
  18. No se usa como medida.

## Arranque rápido con la subida del primer uso desde el encendido (2026-10-22, mañana, medido)

Binario `arranque-106-d25`: el `arranque-101-d25` con la referencia del
arranque rápido hasta 106 °C (`CONFIG_CAFE_HEATER_FAST_START_MAX_SETPOINT_DECI_C=1060`);
δ = 25 °C, banda 42 °C, 70 s, espera hasta caer 0,2 °C. Perfil v20 = v15 con
`boilerOffsetC: 3.7` (agua 92 °C → caldera 101,9 + 3,7 = 105,6 °C desde el
encendido). Máquina apagada desde la madrugada; metal a 22,7 °C al encender.

**Criterio, fijado antes de encender:** (1) tras la primera «lista» no se pierde
en 10 min; (2) el pico tras el corte del arranque rápido queda bajo la referencia
(105,6 °C); (3) «lista» antes de 15 min 51 s (arranque en frío del 19 sin
arranque rápido). El criterio 3 se escribió primero contra «11 min 12 s» y se
corrigió antes de encender: esa cifra era la de la noche anterior, con la
máquina no fría (9 min 16 s medidos al revisarla), y no servía de comparación.

| Tiempo desde el encendido (08:50:57) | Evento | Metal |
|---|---|---|
| 0 s | arranque rápido al 100 % | 22,7 °C |
| ~58 s | corte (referencia − 25 °C) | ~80,6 °C |
| ~90 s | pico tras el corte | 101,84 °C |
| ~3 min | mínimo, con el PI | 95,4 °C |
| 8 min 5 s | «lista» | 105,2 °C |
| hasta 18 min 5 s | «lista» sin perder; máximo 106,80 °C | 105,3–106,8 °C |

- **(1) Cumple:** ninguna «lista» perdida en 10 min.
- **(2) Cumple:** pico 101,84 °C, 3,8 °C bajo la referencia; sin «lista» falsa.
- **(3) Cumple:** 8 min 5 s, frente a 15 min 51 s sin arranque rápido.
- En «lista» el metal sube hasta 1,2 °C sobre la referencia, la oscilación de la
  espera (banda de ±2,1 °C), como con 101,9 °C.

## Sensor del portafiltro antes del café de la 11.5 (2026-10-22, mañana, medido)

Procedimiento del 18 y el 19 (secciones de esos días):

- **Placa auxiliar:** con la resistencia fija de 10 kΩ en lugar del NTC-3 lee
  1714–1721 mV, estable (1703–1725 los días anteriores). La placa está bien.
- **A1 en la taza** (10:55–11:21, método del 11,
  `a1-ntc3-20261022.txt`): 662 puntos
  entre 59,5 y 79,4 °C, NTC − sonda de **+2,02 °C** de media (de +1,37 a +2,63):
  **no cumple** el ±2 °C. El 19 daba −0,04 °C. Cuadra con el chorro de la noche
  del 21 sobre el hervor.
- Decisión del operador: no recalibrarlo (se ha corrido entre días al moverlo o
  reconectarlo) y cerrar la 11.5 sin el café con sonda. El agua del primer uso
  con +3,7 °C queda **inferida** entre lo medido el 19: +2,9 °C dejó el agua
  0,70 °C corta y +4,0 °C, 0,20 °C por encima.

## Verificaciones de la 11.5 sobre la subida del primer uso (2026-10-22, mañana, medido)

Hechas sobre la subida del primer uso, que es la corrección que actúa en la
máquina; la corrección después del café no corrige en la máquina real (ADR-018
§ 12.1). Placa con el firmware de operación v9 y el perfil v15, en reposo y sin
café; la plataforma pedía +3,70 °C (`beyond-measured`). Horas locales; las
decisiones son documentos `first-use-decision` y las líneas de la placa, de
`registros/arrastre-20261011/maquina.csv`.

| Hora | Acto | Decisión de la plataforma | La placa |
|---|---|---|---|
| 11:23:20 | el barista excluye el v15 desde la Bitácora | `excluded-by-barista`, sin subida | v15 aceptada, subida pedida 0,00 °C |
| 11:23:54 | el barista lo vuelve a incluir | `beyond-measured`, 3,7 °C | subida pedida 3,70 °C |
| 11:24:14 | la administración suspende el sostenimiento (`suspension-ca56d81d0cd1`) | `suspended-by-protocol`, sin subida | subida pedida 0,00 °C |
| 11:25:08 | la administración levanta la suspensión | `beyond-measured`, 3,7 °C | subida pedida 3,70 °C |

En cada caso la placa recibió el cambio en la sincronización siguiente, en
menos de 1 s. Las otras verificaciones, sobre la subida:

- **Rige en el café siguiente, no cambia durante él y se quita al terminar:**
  sim-00000343 de la 11.6 (`boilerOffsetAppliedC` 3,7; la placa la devolvió a 0
  al terminar y la plataforma dejó de pedirla).
- **Declara de dónde sale:** cada `first-use-decision` guarda el reposo, el
  último uso visto, la configuración que regía, la regla con su versión y el
  motivo.
- **No pasa del máximo:** `FirstUseRule` rechaza una regla que pida más que el
  máximo del acta (4,0 °C), y la placa acota con el mismo máximo.
- **No cambia el tiempo máximo:** la subida viaja en `boilerOffsetC` junto a la
  configuración que rige, sin versión nueva del perfil.
- **Sin objetivo activo no pide nada:** pruebas de la API; en la máquina siempre
  hay un perfil vigente.
