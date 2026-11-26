# ADR-006 — Construcción modular en tres placas y medición externa de masa

**Estado:** Aceptada, revisada
**Fecha de decisión:** 2026-08-29  
**Última revisión:** 2026-09-16
**Ámbito:** integración física, instrumentación, conversión de señales e ingreso de mediciones externas

## 1. Contexto

La arquitectura ya establece que una única ESP32-S3 debe adquirir temperatura, presión y tiempo y ejecutar la regulación local. Todavía era necesario decidir si la primera implementación utilizaría una PCB integral diseñada específicamente para la cafetera o un conjunto de tarjetas y módulos disponibles comercialmente. Después de seleccionar la construcción modular se definió además que los módulos no quedarían unidos mediante cableado libre: se distribuirían en tres placas con responsabilidades separadas de mando, control/adquisición y potencia.

El objetivo inmediato es disponer de un prototipo funcional en el menor tiempo razonable, facilitar el reemplazo de componentes durante la caracterización y evitar que el diseño, fabricación y depuración de una PCB integral retrasen las pruebas del sistema.

También debía definirse cómo registrar la masa de la bebida. La inspección de la bandeja de 13,5 × 9,2 cm y 2 cm de profundidad muestra restricciones de altura, agua, limpieza y paso de cable. Como los baristas ya utilizan una gramera y el prototipo no necesita una parada automática por masa, se adopta medición externa e ingreso manual.

## 2. Problemas de decisión

1. ¿Debe construirse primero una PCB integral o una arquitectura física modular?
2. ¿Cómo deben agruparse físicamente el mando, el procesamiento/sensado y la potencia?
3. ¿Debe el prototipo medir la masa de forma embebida o registrar manualmente el valor obtenido con la gramera del barista?

## 3. Alternativas consideradas

### 3.1 Integración electrónica

| Alternativa | Ventajas | Desventajas | Evaluación |
|---|---|---|---|
| PCB integral propia | Menos cableado y una integración final más compacta. | Exige cerrar componentes, huellas, protección, diseño, fabricación y depuración antes de validar las cadenas de medida y actuación. | Se pospone. |
| Módulos comerciales interconectados | Permite sustituir, medir y depurar cada función por separado y reduce el tiempo hasta las primeras pruebas. | Aumenta conectores, cableado, volumen y posibles puntos de conexión defectuosa. | **Seleccionada para el prototipo.** |
| Arquitectura mixta con tres placas portadoras funcionales | Ordena el cableado, conserva módulos reemplazables y separa físicamente el panel, la lógica aislada y la red. | Exige diseñar tres placas y dos interfaces entre ellas. | **Seleccionada como forma de materializar el prototipo modular.** |

### 3.2 Montaje de la medición de masa

| Alternativa | Ventajas | Desventajas | Evaluación |
|---|---|---|---|
| Gramera comercial externa e ingreso manual | Utiliza la práctica habitual del barista, evita modificar la bandeja y permite registrar dosis y masa final para calcular el rendimiento. | No proporciona una serie de masa ni permite que el firmware detenga automáticamente la bomba por peso. | **Seleccionada para el prototipo inicial.** |

## 4. Decisión

### 4.1 Construcción modular

La primera versión física se construirá mediante módulos reemplazables, conectados a una única tarjeta de desarrollo ESP32-S3. No se diseñará una PCB integral que reúna procesamiento, adquisición y potencia. Los módulos y las interconexiones se distribuirán en tres placas funcionales:

| Placa | Responsabilidad | Dominio eléctrico |
|---|---|---|
| `PCB-MANDO` | Ocho pulsadores táctiles organizados como cuatro pares en paralelo, cuatro indicadores y conector hacia control. | Solo 3,3 V aislados. |
| `PCB-CONTROL` | Tarjeta ESP32-S3, módulos de temperatura y presión, acondicionamiento de señales, entradas de mando y señales lógicas hacia potencia. | 5 V aislados de entrada y lógica de 3,3 V; no admite red. |
| `PCB-POTENCIA` | Entrada y protección de red, `HLK-10M05`, SSR, dimmer, conexiones del calentador y de la bomba y barreras de aislamiento. | Contiene una zona de 120 VAC y una isla SELV aislada, separadas por una barrera explícita. |

La autoridad del mando y el procedimiento de desconexión eléctrica se rigen
por ADR-005. ADR-008 define las protecciones de `PCB-POTENCIA`; por ello el
prototipo no añade un contactor `K_SAFE`, un relé intermedio ni un pulsador
dedicado de parada de emergencia, y el mando de encendido/apagado continúa
siendo lógico.

Los cuatro pares de la placa de mando conservan la disposición mecánica observada en la tarjeta original `CM8501B-D-11`: cada acción física acciona dos pulsadores conectados en paralelo. Por tanto, se utilizan cuatro entradas lógicas, no ocho. En la versión nueva cada par cerrará una entrada activa en bajo contra `GND_ISO`, con *pull-up* externo y filtrado en `PCB-CONTROL`.

Las acciones físicas previstas para la placa de mando son:

1. encendido/apagado lógico;
2. inicio/parada normal de la extracción, donde una segunda pulsación durante la preparación es una parada anticipada y no una pausa reanudable;
3. entrada y salida del modo de espumado, como extensión operativa;
4. purga manual del grupo mientras el mando se mantenga pulsado, como extensión operativa.

Las dos primeras acciones pertenecen a la operación mínima del prototipo. El
espumado y la purga pueden implementarse como apoyo físico después de validar
sus condiciones de seguridad, pero no forman parte de los requisitos mínimos,
del modelo virtual inicial ni de la evaluación principal del gemelo digital.

El encendido y apagado son lógicos: no sustituyen el seccionamiento de red. El modo de espumado se acepta únicamente desde un estado seguro, es excluyente con extracción y purga, utiliza una referencia térmica propia y conserva la perilla mecánica como medio utilizado por el barista para liberar y graduar el vapor. Al cancelarlo, la cafetera debe regresar al intervalo térmico autorizado para café antes de admitir otra extracción. La entrada al modo no activa por sí sola la bomba; la primera implementación la mantiene apagada durante vapor hasta que una prueba hidráulica demuestre que la máquina necesita una excitación controlada y se aprueben sus límites.

La cuarta acción puede asignarse a la **purga manual del grupo** como extensión
operativa. Solo se permitirá en un estado preparado y térmicamente apto,
accionará la bomba mientras el barista mantenga pulsado el mando y se detendrá
al soltarlo, alcanzar el tiempo máximo o presentarse un fallo. La purga no
ejecutará un perfil de extracción ni creará una preparación de café; se
registrará como evento operativo. Los valores de referencia de vapor y los
tiempos máximos de vapor y purga se fijarán mediante caracterización antes de
habilitar esas extensiones.

> **Revisión del 2026-10-06 — se retira el modo de espumado.** El prototipo se
> simplifica y no tendrá función de vapor: la tercera acción deja de existir, y
> con ella su referencia térmica, su exclusión con extracción y purga, y el
> retorno al intervalo de café. La purga, cuarta acción, se conserva tal como
> está descrita. Los dos pulsadores de `BTN_STEAM_N` siguen montados en
> `PCB-MANDO`, cableados y acondicionados en `PCB-CONTROL`, y no se rehace
> ninguna placa.
>
> **Pasan a ser la tara de la báscula de ensayo.** Pulsarlos pone a cero la
> lectura de la galga, para descontar la taza antes de cada extracción sin ir a
> la aplicación. No acciona ninguna salida de potencia ni cambia el estado de la
> máquina, de modo que no entra en el guardián ni en la exclusión entre modos.
> Se descartó darles además una segunda función con pulsación mantenida —marcar
> en el registro un evento como la primera gota—: aportaba un dato, pero
> mezclaba en un botón dos usos que el barista tendría que distinguir por el
> tiempo de pulsación.
>
> Aplicado en el firmware el mismo día: la máquina de estados pierde los tres
> estados y los tres eventos del vapor, y el pulsador pide la tara a la báscula
> en todo estado que no autorice la bomba (`operational_fsm_tare_allowed`).
> Durante una extracción o una purga la rechaza, porque pondría a cero una masa
> que se está midiendo.
>
> **Revisión del 2026-10-07 — la tara pasa al final del panel.** En el panel
> montado los cuatro pares quedan, de izquierda a derecha, encendido, café,
> tara —el antiguo vapor— y purga. El usuario prefirió la tara en el extremo, de modo que la
> función se cruza en el firmware: la tara lee la red `BTN_PURGE_N`
> (`SW7 ∥ SW8`) y la purga la red `BTN_STEAM_N` (`SW5 ∥ SW6`). Las redes y la
> serigrafía conservan su nombre, porque cambiarlo exigiría rehacer las placas;
> la correspondencia vive en `panel_runtime.c`.
>
> **Revisión del 2026-10-17 — un color por estado, despejar la alarma y
> reiniciar desde el mando** (pedidas por el usuario tras reparar el mando).
>
> - **Colores.** Cada estado enciende un solo LED: amarillo parpadeando al
>   calentar, verde fijo en «lista» con el metal en la banda de la verificación
>   B (±0,6 °C) y parpadeando fuera de ella, azul fijo con café o purgado en marcha,
>   rojo fijo en alarma y rojo parpadeando ante una avería o un pulsador
>   retenido; en reposo, todo apagado. La tara hecha destella el verde (antes
>   azul y amarillo). Se descartó el código acumulativo anterior (verde siempre
>   encendido y los demás sumados encima), porque obligaba a leer una
>   combinación. Vive en `indicator_panel`.
> - **Despejar la alarma.** Encendido sostenido 3 s en alarma pide despejarla.
>   El supervisor solo publica `OP_EVENT_SAFETY_CLEARED` si la causa ya no
>   está (`process_supervisor_clear_allowed`), y la máquina vuelve a reposo,
>   nunca a lo que hacía. Hasta hoy ningún componente publicaba ese evento y
>   la alarma solo se dejaba reiniciando la placa desde el computador.
> - **Reiniciar.** Encendido y tara sostenidos juntos 3 s reinician la placa,
>   salvo con la bomba en uso. Se descartó encendido con café, porque café
>   solo, en «lista», arranca la bomba antes de que el gesto se complete. Las
>   salidas arrancan apagadas (RNF-002).
> - **Ningún gesto con un pulsador retenido**, para que un contacto en
>   cortocircuito no despeje alarmas ni reinicie en bucle. Los 3 s quedan por
>   debajo de los 10 s en que un pulsador se declara retenido. Los gestos viven
>   en `panel_gesture`.
> - **Sin red, encendido no enciende.** El detector de cruce por cero sabe si
>   hay red: con ella marca 120 cruces por segundo. Sin red, encendido deja la
>   máquina en reposo, destella el rojo como una tara rechazada y lo anota. Con
>   red pero todavía sin medidas aptas (la presión tarda unos 2 s más que el
>   cruce por cero tras conectar), también se niega. Antes
>   se encendía igual, y arrancada la placa sin red el supervisor, sin presión
>   apta (el transductor se alimenta de la red), la llevaba a alarma por algo
>   que no es una falla. Apagar se puede siempre.
> - **Cualquier botón negado destella el rojo** (usuario, 2026-10-17): café o
>   purgado sin estar lista, encendido en alarma o sin red, una tara negada.
>   Soltar el purgado y los eventos del supervisor no cuentan.
> - **Tara corrida: doble destello azul, repetido cada 15 s** hasta la siguiente
>   tara (el usuario no quiso un azul parpadeando sin parar), si la báscula
>   se aleja más de 1 g de cero (provisional: tres veces su desviación en
>   reposo, 0,29 g) entre la tara y el siguiente café o purgado. Vive en
>   `tare_watch`.
> - **Sin red no se avisa de averías del guardián.** La presión no apta sin red
>   no es una avería, y sin red nada puede accionarse. Un pulsador retenido y
>   una alarma declarada se siguen viendo. Si la red falta en mitad de un café,
>   ~~el supervisor sigue disparando por presión no apta: con la bomba en uso esa
>   regla no se relaja.~~ Cambiado el mismo día a pedido del usuario: ver abajo.
> - **Si la red falta 1 s seguido, la máquina vuelve a reposo** y los LED se
>   apagan, también con café o purgado en marcha; el café se cierra como
>   pérdida de energía. No es alarma. Sin red la presión no apta ya no dispara
>   con la bomba en uso, porque la bomba se dispara en cada cruce y no puede
>   moverse. El segundo es provisional (`PROCESS_MAINS_LOSS_MS`): son 120
>   periodos, más que cualquier intermitencia del detector.

La partición física base será:

| Función | Implementación modular inicial |
|---|---|
| Procesamiento | Tarjeta disponible marcada como `ESP32-S3-N16R8`, con 16 MB de flash y 8 MB de PSRAM confirmados mediante pruebas físicas. |
| Temperatura | Sonda original especificada para CM8502-UL como AMPRON `MF58-104X3950YA`, conservando montaje y conector de dos vías, con divisor/filtro y un canal del ADS1115 a 3,3 V. El informe aporta 100 kΩ a 25 °C y beta 3950 K; su calibración metrológica se ejecutará posteriormente. |
| Presión | HK1100C G1/4 de 0–12 bar, alimentado a 5 V y conectado mediante una derivación en T; salida 0,5–4,5 V reducida por divisor 2/3 hacia un ADS1115 a 3,3 V. Lectura inicial cada 10 ms. |
| Masa, dosis y TDS | Instrumentos externos del barista e ingreso manual posterior; no forman parte de `PCB-CONTROL`. |
| Calentador | Módulo de conmutación de estado sólido aislado, dimensionado y montado térmicamente para la carga real. |
| Bomba | Módulo o subconjunto separado de control de fase, con aislamiento, cruce por cero y protección dimensionados para la bomba real. |
| Alimentación y seguridad | Fuente aislada, distribución y protecciones de rama, fusibles térmicos existentes y terminales apropiados como subconjuntos separados. La desconexión para intervenir se realiza retirando manualmente el enchufe. |

Las tres placas son portadoras funcionales para módulos y conexiones; no convierten el prototipo en una PCB integral. `PCB-POTENCIA` no podrá trazarse ni energizarse sin una revisión eléctrica específica de protecciones, separación, distancias, conductores, disipación y cubiertas.

La inspección fotográfica de la tarjeta que ya posee el proyecto confirma la marcación `ESP32-S3-N16R8`. La prueba física del 2026-09-01 confirmó 16 MB de flash y 8 MB de PSRAM, con inicialización y prueba de memoria correctas. La tabla vigente reserva 256 KiB para NVS y 3 MiB para la aplicación; la cola persistente verificada conserva 32 sobres de hasta 1536 bytes.

La marcación visible no identifica una referencia oficial completa de la placa
de desarrollo, por lo que se conserva la denominación genérica. La inspección
posterior cerró lo necesario para la portadora: 57 × 28 mm, dos filas hembra de
22 contactos, paso nominal 2,54 mm, separación nominal de 25,4 mm, doble USB-C,
regulador visible de la familia `AMS1117` y pinout coincidente con la
serigrafía. No se aplica a esta placa el estado EOL documentado para
`ESP32-S3-WROOM-2-N16R8V`, porque es una referencia distinta. Si la placa
disponible no supera la verificación, `ESP32-S3-DevKitC-1-N32R16V` podrá
evaluarse como reemplazo, no como compra inicial.

El uso de módulos no autoriza conexiones provisionales inseguras. La red, el calentador y la bomba no se montarán en protoboard sin soldadura; permanecerán encerrados, protegidos y separados del dominio lógico. Cada módulo se aceptará por sus características verificadas y no solo por el nombre comercial del integrado que anuncia.

### 4.2 Medición externa e ingreso manual

El prototipo no incorpora ningún sistema de pesaje integrado ni una báscula
conectada. El barista medirá con instrumentos externos la dosis de café, la
masa final de bebida y el TDS. Después de la extracción introducirá los valores
manualmente mediante la aplicación web externa, con unidad y origen explícitos.
El sistema validará formato y rango y calculará el rendimiento de extracción
cuando disponga de los tres datos.

Una masa esperada puede permanecer en el perfil como referencia para comparar
el resultado posterior, pero no es una consigna de actuación. El firmware no
detiene la bomba por masa: el barista solicita la parada normal al observar su
gramera o el tiempo máximo la ejecuta automáticamente.

La medición de masa permanecerá manual dentro del alcance de esta versión. Una
eventual medición automática requeriría una nueva decisión mecánica,
metrológica y de control.

## 5. Fundamento y procedencia de la decisión

| Evidencia | Hallazgo respaldado | Uso en esta decisión |
|---|---|---|
| Código Gaggiuino, `pressure_sensor.cpp`, revisión `bddc57a` | La configuración no integrada utiliza ADS1115 y la configuración `SINGLE_BOARD` utiliza ADS1015. | Sirve como antecedente para seleccionar ADS1115 en la implementación modular inicial. |
| Ficha HK1100C de 0–1,2 MPa | Declara alimentación de 5 V, salida 0,5–4,5 V, rango de 0–12 bar y rosca G1/4. | Cierra el transductor y permite dimensionar el divisor y el conector de tres vías. |
| Ficha técnica Kalley K-EX | Confirma bandeja extraíble, dimensiones generales de 218 × 273 × 290 mm, masa de 2,7 kg y alimentación de 120 V/60 Hz. | Delimita la máquina, pero no proporciona dimensiones suficientes para copiar un soporte bajo bandeja. |
| Caracterización local de la Kalley K-EX | Documenta las placas originales, las cargas de red, el boiler `CM8502`, la bomba y la ausencia de una interfaz aislada reutilizable. | Justifica sustituir la electrónica original y mantener separados el dominio lógico y la potencia. |
| Inspección y mediciones del NTC original, 2026-08-30 | Sonda de dos conductores y marca parcial `C8`; 94,7 kΩ cerca de 22 °C, disminución al calentar, recuperación al enfriar y `OL` entre conductores y boiler. | Confirma funcionamiento monótono e aislamiento básico; no sustituye la calibración de la unidad montada. |
| Informe constructivo CM8502, ítems 51 y 51a | El conjunto NTC aplica al CM8502-UL y especifica como NTC principal AMPRON `MF58-104X3950YA`, 100 kΩ a 25 °C, beta 3950 K y temperatura máxima de operación de 250 °C. | Identifica la referencia base y sus parámetros; la calibración posterior determina los coeficientes efectivos de la sonda instalada y su montaje. |
| Inspección del conector NTC, 2026-08-31 | Cabezal rojo de dos vías, cuerpo 6 × 8 mm, paso cercano a 3,96 mm, pines de 5–6 mm y marcado `BMP`. | Permite recuperar el cabezal original y cerrar `J_NTC` como huella VH de 3,96 mm, pendiente solo de comprobación 1:1. |
| Inspección de pulsadores, 2026-08-31 | Ocho pulsadores THT de dos terminales, cuerpo 6 × 6 mm, altura aproximada de 6 mm y paso cercano a 5 mm. | Permite seleccionar Sigma `SW 2P 6X6X5.1MM`, paso 5,08 mm, y cerrar la compra de ocho unidades. |
| Inspección y continuidad de la placa `CM8501B-D-11` | Confirma ocho pulsadores organizados como `S1∥S2`, `S3∥S4`, `S5∥S6` y `S7∥S8`; cada botón plástico acciona un par. | Permite conservar cuatro acciones físicas con cuatro entradas lógicas en `PCB-MANDO`. |
| Inspección fotográfica de la tarjeta disponible, 2026-08-29 | La cubierta está marcada como `ESP32-S3-N16R8` y la placa dispone de mandos `BOOT` y `RST`. | Confirma la configuración que debe verificarse e impide documentar como adquirida una `N32R16V`. |
| Nomenclatura oficial de Espressif | `ESP32-S3-WROOM-1-N16R8` significa 16 MB de flash y 8 MB de PSRAM. | Permite interpretar el sufijo de memoria sin atribuir a la fotografía un modelo de placa no demostrado. |

La selección de una gramera externa es una decisión propia de ingeniería
motivada por el plazo, la práctica habitual del barista y la falta de necesidad
de cerrar el lazo por masa en el prototipo inicial.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- Se pueden validar por separado temperatura, presión y actuación.
- Se reutiliza el NTC ya instalado sin perforar el boiler, comprar una
  termocupla ni añadir un convertidor MAX6675.
- El conector original del NTC permanece intacto y permite desmontar la sonda;
  `PCB-CONTROL` incorpora un cabezal compatible o un adaptador reversible.
- Un componente puede reemplazarse sin rediseñar toda la electrónica.
- La primera adquisición de datos no depende del ciclo de fabricación de una PCB integral.
- Se evita modificar la bandeja, reducir la altura de taza o añadir una cadena de pesaje expuesta al agua.
- El rendimiento de extracción sigue siendo calculable con dosis, masa final y TDS ingresados manualmente.
- La arquitectura conserva una ruta clara hacia una integración posterior.
- La placa de mando reproduce la geometría de cuatro acciones con dos pulsadores por acción sin consumir ocho GPIO.
- La separación física entre mando, control y potencia reduce la posibilidad de llevar la red al panel o a los sensores.
- La cuarta acción queda dedicada a una tarea frecuente de limpieza y preparación hidráulica sin añadir otro actuador.
- El espumado conserva la actuación manual sobre la perilla y no introduce una apertura automática de vapor inexistente.

La familia y el paso del conector original del NTC se medirán antes de cerrar la
huella `J_NTC`. El parecido visual no autoriza denominarlo JST-XH. El NTC no
tiene polaridad, pero el esquema fija `J_NTC-1 = TEMP_NTC` y
`J_NTC-2 = GND_ISO` para facilitar pruebas y mantenimiento. **El arnés del sensor
no se corta.**

#### Revisión del 2026-09-14 — se admite una báscula externa de ensayo

ADR-006 excluyó la celda de carga y el HX711 para evitar modificar la bandeja,
reducir la altura de taza y exponer una cadena de pesaje al agua. Esa exclusión
se mantiene **para el montaje en servicio** y se levanta **para la
caracterización**, por un motivo que no estaba sobre la mesa cuando se decidió.

El modelado de la presión demostró que sin caudal resuelto en el tiempo no puede
separarse la resistencia de la cama del caudal en `P = R · q`, de modo que el
modelo hidráulico del gemelo digital no tiene cómo fundamentarse. El caudal no
es un lujo instrumental: es la variable que falta.

Se admite por tanto una **celda de barra de 1 kg con HX711 montada en voladizo
desde una base externa**, con un brazo rígido que alcanza el área bajo los picos
del portafiltro. La disposición responde a cada una de las tres objeciones
originales:

- **No se modifica la bandeja**: la celda no se apoya en ella ni la sustituye.
- **No se reduce la altura de taza**: bajo la taza queda solo el espesor de un
  plato, unos milímetros, en lugar de los 25–35 mm de una báscula apoyada encima.
  La disposición **gana** altura respecto a cualquier alternativa.
- **No se expone la cadena de pesaje al agua**: celda, convertidor y cableado
  quedan fuera de la máquina.

Condiciones que acompañan la revisión:

- es **instrumento de laboratorio**, presente solo durante los ensayos. La
  cafetera opera sin él y ninguna función depende de su presencia;
- no habilita pesaje integrado, ni serie de masa en el registro de operación, ni
  parada automática por peso. RD-003 sigue vigente: la masa de la bebida se
  ingresa manualmente;
- el voladizo introduce error si la carga no se apoya siempre en el mismo punto;
  el plato define una posición y la calibración se hace con la carga en ella;
- la interfaz `J_SCALE` se documenta en el esquema de interconexión y ocupa
  **GPIO10 y GPIO11**. Se eligieron GPIO1 y GPIO2 por estar reservados para
  ampliación, y se cambiaron el 2026-09-20 durante el trazado de `PCB-CONTROL`:
  llegar a la fila superior del módulo resultó impracticable —el interior está
  ocupado por pistas en ambas capas— mientras que los pines 16 y 17 pertenecen a
  la fila inferior, justo encima del bloque de la báscula, y bajan directos sin
  una sola vía. Ninguno de los dos es de *strapping* ni está reservado por uso
  interno de memoria. El cambio está aplicado en el esquemático, en `pin_map.h` y
  en el esquema de interconexión.

#### Revisión del 2026-09-14 — los cabezales originales sí se recuperan

La formulación anterior desaconsejaba desoldar el cabezal de la placa original.
Se revisa: los cabezales del NTC y de la bomba **se desoldaron deliberadamente de
la tarjeta desmontada para reutilizarlos**, y esa es ahora la solución adoptada.

El motivo es el mismo que sostiene la advertencia sobre el parecido visual. Los
enchufes del arnés permanecen intactos y sin cortar; el cabezal recuperado es la
pieza que se sabe que encaja con ellos, mientras que una equivalencia comercial
elegida por semejanza tendría que demostrarse. Reutilizar la pieza original
elimina esa demostración en lugar de aplazarla.

La prohibición tenía sentido mientras la tarjeta original conservara alguna
posibilidad de volver a servicio. Sustituida por completo la electrónica, esa
posibilidad no existe y el cabezal es más útil en la placa nueva que en una
tarjeta que no volverá a usarse.

Condiciones que acompañan la revisión:

- cada cabezal recuperado se **inspecciona antes de fabricar**: pines sin doblar
  ni fracturar, cuerpo plástico sin deformación térmica por el desoldado y
  retención del enchufe conservada;
- la huella se verifica **1:1 contra la pieza recuperada**, no contra una ficha
  de catálogo;
- si una pieza resulta dañada, se restituye por una equivalencia adquirida y
  **medida**, y se documenta el cambio antes de cerrar la huella.

#### Revisión del 2026-09-16 — las dos interfaces entre placas pasan a ser mixtas

Las dos interfaces entre placas se concibieron con conector en ambos extremos.
No fue posible conseguir los cuatro conectores: solo se consiguieron **dos cables
con conector en un único extremo**. El reparto adoptado es:

| Placa | Interfaz | Terminación |
|---|---|---|
| `PCB-CONTROL` | `J_CP` y `J_MC` | Conector |
| `PCB-MANDO` | `J_MC` | Soldadura directa sobre pads |
| `PCB-POTENCIA` | `J_CP` | Soldadura directa sobre pads |

`PCB-CONTROL` conserva los conectores por ser la placa central: es la que se
sustituye o se lleva al banco con más frecuencia durante la caracterización, y la
única a la que llegan las dos interfaces. Concentrar en ella la parte
desenchufable conserva la ventaja del montaje modular donde más se usa.

`PCB-MANDO` ya estaba resuelta así antes de esta revisión —su BOM declaraba «sin
conector; soldadura directa» y su README fija los diez pads con sus centros—, de
modo que el cambio real afecta a `PCB-POTENCIA`.

Esto **reduce** la modularidad que sostiene § 4.1, y conviene nombrarlo en lugar
de dejarlo implícito:

- **Las placas ya no se separan sin desoldar.** Cualquier procedimiento de
  intervención, inspección o sustitución que supusiera desconectar una placa
  tirando de un conector deja de ser aplicable. Afecta en particular a
  `PCB-POTENCIA`, que es la conectada a la red.
- **Sustituir `PCB-MANDO` o `PCB-POTENCIA` obliga a rehacer una soldadura de diez
  conductores.** La ventaja de «sustituir, medir y depurar cada función por
  separado» que motivó la construcción modular se conserva en `PCB-CONTROL` y se
  degrada en las otras dos.
- **La verificación 9 de § 7 sigue vigente y gana peso**: ninguna desconexión del
  arnés debe generar una orden activa. Con un extremo soldado, el modo de fallo
  ya no es un conector suelto sino un conductor roto o una soldadura fría, que se
  detectan peor.

No se reabre la decisión de construcción modular: la alternativa era retrasar el
montaje hasta conseguir los conectores, y el prototipo es supervisado. Si más
adelante se consiguen, restituir el conector en `PCB-POTENCIA` es un cambio
local que no afecta al resto de este ADR.

### 6.2 Consecuencias desfavorables

- Aumentan el cableado, los conectores y el volumen del montaje.
- Deben controlarse bucles de masa, ruido, vibración y desconexiones.
- Los módulos comerciales pueden incorporar reguladores, divisores o aislamiento diferentes a lo anunciado.
- La terminación no será tan compacta como una PCB integral.
- La masa no se adquiere en tiempo real y no puede utilizarse como parada automática.
- El ingreso manual puede producir errores de transcripción y requiere validación visible.
- Deben diseñarse y verificarse dos interfaces entre placas y una barrera interna en `PCB-POTENCIA`.
- Un pulsador de un par conectado en paralelo que falle en cortocircuito deja la acción permanentemente activa; el firmware deberá detectar un mando retenido durante el arranque y durante la operación.
- El modo vapor exige caracterizar una referencia térmica, tiempos máximos y el comportamiento hidráulico real antes de habilitarse.
- La purga ocupa permanentemente la cuarta acción física y renuncia a utilizarla como selector de perfil u otra función.

## 7. Verificación

La decisión quedará implementada cuando:

1. exista un diagrama de interconexión con referencia exacta, alimentación, interfaz y dominio eléctrico de cada placa y módulo;
2. se compruebe que ninguna entrada de la ESP32-S3 excede 3,3 V;
3. la interfaz acepte dosis, masa final y TDS con unidad y origen manual y rechace valores inválidos;
4. el rendimiento de extracción conserve trazabilidad hacia los tres valores introducidos;
5. se demuestre que ningún dato manual modifica referencias ni actuadores de una extracción activa;
6. la cadena de temperatura supere la comprobación funcional básica para el montaje y, antes de reportar desempeño experimental, su caracterización metrológica;
7. la cadena de presión no presente fugas, entregue un cero plausible sin presión, responda monótonamente al accionar la bomba, mantenga `PRESS_ADC` por debajo de 3,3 V y detecte entradas inválidas o saturadas; su corrección de cero y escala y su caracterización metrológica se realizarán antes de reportar desempeño experimental;
8. las etapas conectadas a 120 VAC superen una revisión eléctrica independiente antes de energizarse;
9. los cuatro pares de pulsadores produzcan cuatro acciones únicas, se detecte un mando retenido y ninguna desconexión del arnés genere una orden activa;
10. la barrera de `PCB-POTENCIA` se revise sin utilizar pistas, planos, conectores o elementos de montaje que unan accidentalmente la red con la isla lógica.

#### Revisión del 2026-09-20 — no habrá revisión eléctrica independiente

El punto 8 exige que las etapas de 120 VAC superen una revisión eléctrica
independiente antes de energizarse. **No va a haberla**, y el motivo es de
presupuesto: el proyecto no dispone de fondos para contratarla ni tiene prevista
otra vía para obtenerla. La verificación la ejecuta quien diseñó.

Esto no se resuelve declarando el requisito cumplido por otros medios. Una
revisión independiente aporta lo único que la autoverificación no puede dar
—ojos que no participaron en el diseño— y la historia de este proyecto lo
demuestra. Los cuatro defectos más graves encontrados hasta hoy fueron
**invisibles al DRC** y aparecieron al revisar a mano: el `ADS1115` espejado en
`PCB-CONTROL`, el `2N2222` con el colector en el pad central, la serigrafía de
los cuatro LED invertida en `PCB-MANDO` y el dimmer sin cable de vuelta. Ninguno
lo habría detectado una herramienta.

En su lugar, el punto 8 pasa a exigir, como **sustituto declaradamente
inferior**:

1. verificación de cada afirmación sobre el **artefacto de fabricación** —el
   Gerber exportado— y no sobre lo que muestre el editor;
2. contraste de cada huella contra la **pieza física en mano**, con plantilla
   impresa a escala 1:1 y calibre verificado sobre la barra de referencia;
3. **ensayo en banco** de los módulos de red antes de integrarlos, según
   [`firmware/tests/`](../../../firmware/tests/);
4. la verificación de aislamiento, distancias y cobre documentada en
   `aislamiento-y-cobre-pcb-power.md`;
5. la primera puesta en marcha **supervisada** de ADR-008 § 5, con el enchufe a
   mano;
6. una **lista de comprobación escrita antes de revisar**, no una mirada
   general. Revisar a ojo el propio trabajo encuentra lo que uno ya sabe mirar;
   una lista redactada de antemano obliga a comprobar puntos concretos aunque se
   crean resueltos. Los cuatro defectos citados aparecieron así, no por
   inspección general.

**Riesgo asumido, declarado y no mitigado:** puede quedar en el diseño un
defecto de seguridad eléctrica que su autor no vea, porque quien verifica es
quien diseñó. Las seis medidas anteriores reducen la probabilidad; no eliminan
el sesgo. Este prototipo **no está certificado, no opera desatendido y no debe
presentarse como validado eléctricamente**.

## 8. Aspectos pendientes

- Referencias exactas y revisión eléctrica de cada módulo comercial.
- Confirmación por impresión 1:1 de la huella física ya definida y prueba de los GPIO asignados antes de fabricar `PCB-CONTROL`.
- Documentar en la integración del firmware la tabla vigente: 16 MB de flash, 8 MB de PSRAM, 256 KiB de NVS, 3 MiB para la aplicación y cola de 32 sobres de hasta 1536 bytes.
- Diagrama de cableado, conectores y distribución física de las tres placas.
- Referencia térmica de vapor, límites temporales de espumado y purga y comprobación experimental de si el vapor sostenido requiere accionar la bomba; estas extensiones no pertenecen al modelo virtual inicial.
- Decisión posterior sobre una PCB integral que pudiera reemplazar las tres placas.

## 9. Trazabilidad

- Requisitos funcionales: RF-003–RF-018.
- Requisitos no funcionales: RNF-001 y RNF-003–RNF-007.
- Restricciones de diseño: RD-001–RD-005.
- Decisiones relacionadas: ADR-001, ADR-002, ADR-003, ADR-005, ADR-007 y ADR-008.
- Vapor y purga: extensiones físicas fuera del alcance mínimo; no crean requisitos adicionales ni modifican el modelo virtual inicial.

## 10. Fuentes

- [Gaggiuino `thermocouple.h`, revisión `bddc57a`](https://github.com/Zer0-bit/gaggiuino/blob/bddc57a63552b921a9f54024e41bc04965fa8dd8/src/peripherals/thermocouple.h#L7-L15).
- [Gaggiuino `pressure_sensor.cpp`, revisión `bddc57a`](https://github.com/Zer0-bit/gaggiuino/blob/bddc57a63552b921a9f54024e41bc04965fa8dd8/src/peripherals/pressure_sensor.cpp#L9-L13).
- Ficha técnica local de la Kalley K-EX.
- Caracterización local de la cafetera Kalley K-EX.
- [Página oficial de producto Kalley K-EX](https://www.kalley.com.co/cafetera-expreso-kalley-k-ex-negra/p/7705946997089).
- [Nomenclatura oficial de componentes Espressif](https://developer.espressif.com/blog/2025/03/espressif-part-numbers-explained/).
- [ESP32-S3-WROOM-1 — referencias oficiales disponibles](https://www.espressif.com/en/products/modules/esp32-s3/esp32-s3-wroom-1).
- [ESP32-S3-DevKitC-1 — referencia oficial para una eventual sustitución](https://www.espressif.com/en/dev-board/esp32-s3-devkitc-1-en).

## 11. Criterios de reconsideración

La decisión se revisará si el ingreso manual resulta insuficiente para el
protocolo experimental, si los módulos disponibles no satisfacen los requisitos
eléctricos o si una PCB integral resulta necesaria para las pruebas finales de
desempeño, seguridad o compatibilidad electromagnética. La medición automática
de masa solo podrá considerarse mediante una nueva decisión y una modificación
formal del alcance del sistema.
