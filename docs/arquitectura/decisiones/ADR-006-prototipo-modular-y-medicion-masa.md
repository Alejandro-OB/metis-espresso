# ADR-006 — Construcción modular en tres placas y medición externa de masa

**Estado:** Aceptada (2026-08-29)
**Ámbito:** integración física, instrumentación, mando del panel e ingreso de mediciones externas

## 1. Contexto

Una única ESP32-S3 adquiere temperatura, presión y tiempo y regula localmente
(ADR-001). Faltaba decidir entre una PCB integral y módulos comerciales, cómo
agruparlos y cómo registrar la masa de la bebida. Se busca un prototipo
funcional pronto, con componentes reemplazables durante la caracterización. La
bandeja de la cafetera mide 13,5 × 9,2 cm con 2 cm de profundidad: limita la
altura, expone al agua cualquier montaje y dificulta el paso de cables.

## 2. Alternativas consideradas

| Alternativa | Razón |
|---|---|
| PCB integral propia | Descartada: exige cerrar huellas, protección, fabricación y depuración antes de validar las cadenas de medida y actuación. |
| Módulos con cableado libre | Descartada: multiplica conexiones sueltas y no separa la red del dominio lógico. |
| Módulos sobre tres placas portadoras | **Seleccionada.** |
| Celda de carga bajo la bandeja | Descartada en servicio: modifica la bandeja, reduce la altura de taza y expone el pesaje al agua. |
| Gramera externa e ingreso manual | **Seleccionada.** Es la práctica del barista; el prototipo no necesita parar por peso. |

## 4. Decisión

### 4.1 Construcción modular

| Placa | Responsabilidad | Dominio eléctrico |
|---|---|---|
| `PCB-MANDO` | Ocho pulsadores en cuatro pares en paralelo y cuatro indicadores. | Solo 3,3 V aislados. |
| `PCB-CONTROL` | ESP32-S3, módulos de temperatura y presión, acondicionamiento y señales lógicas hacia potencia. | 5 V aislados y lógica de 3,3 V; sin red. |
| `PCB-POTENCIA` | Entrada y protección de red, `HLK-10M05`, SSR, dimmer, calentador y bomba. | 120 VAC y una isla SELV, separadas por una barrera explícita. |

La autoridad del mando se rige por ADR-005 y las protecciones de
`PCB-POTENCIA` por ADR-008; por eso no hay contactor `K_SAFE`, relé intermedio
ni parada de emergencia dedicada. El encendido es lógico y no sustituye el
seccionamiento: para intervenir se retira el enchufe.

La tarjeta, marcada `ESP32-S3-N16R8`, tiene 16 MB de flash y 8 MB de PSRAM
confirmados en prueba física; si fallara, el reemplazo a evaluar es
`ESP32-S3-DevKitC-1-N32R16V`. La temperatura se mide con la sonda original del
calentador CM8502-UL, AMPRON `MF58-104X3950YA` (100 kΩ a 25 °C, beta 3950 K),
con divisor y un canal de ADS1115 a 3,3 V. La presión se mide con un HK1100C
G1/4 de 0–12 bar alimentado a 5 V, en derivación en T; su salida de 0,5–4,5 V
pasa por un divisor 2/3 hacia el ADS1115 y se lee cada 10 ms. El calentador usa
un módulo de estado sólido aislado, y la bomba, un control de fase con cruce por
cero.

La red, el calentador y la bomba no se montan en protoboard; quedan encerrados y
separados del dominio lógico. Cada módulo se acepta por sus características
verificadas, no por el integrado que anuncia.

Los cabezales del NTC y de la bomba se desoldaron de la tarjeta original, que no
volverá a servicio, para reutilizarlos. Se sabe que encajan con el arnés, que no
se corta; una equivalencia elegida por parecido tendría que demostrarse. Cada
huella se verifica 1:1 contra la pieza recuperada.

Solo se consiguieron dos cables con conector en un extremo. `PCB-CONTROL`, la
placa que más va al banco, lleva los conectores de `J_CP` y `J_MC`; en
`PCB-MANDO` y `PCB-POTENCIA` el cable se suelda sobre pads. Se descartó esperar
a tener cuatro conectores porque retrasaba el montaje.

### 4.2 Medición externa de masa e ingreso manual

El barista mide dosis, masa final y TDS con instrumentos externos y los
introduce en la aplicación web con unidad y origen. El sistema valida el rango y
calcula el rendimiento de extracción cuando tiene los tres datos. Una masa
esperada en el perfil es referencia de comparación, no consigna: la bomba se
detiene por orden del barista o por tiempo máximo.

### 4.3 Báscula externa de ensayo

Para la caracterización se admite una celda de barra de 1 kg con HX711. Sin
caudal resuelto en el tiempo no se separa la resistencia de la cama del caudal
en `P = R · q`, y el modelo hidráulico queda sin fundamento.

La celda va en voladizo desde una base externa, con un brazo bajo los picos del
portafiltro. Así no toca la bandeja, deja bajo la taza solo un plato en vez de
25–35 mm de báscula y queda fuera del agua. Es instrumento de laboratorio:
ninguna función depende de ella, no habilita parada por peso y la masa de la
bebida sigue ingresándose a mano. Se calibra con la carga en la
posición del plato, porque el voladizo da error si el apoyo cambia. `J_SCALE`
ocupa GPIO10 y GPIO11, que no son de *strapping*.

### 4.4 Panel de mando

Cada botón acciona dos pulsadores en paralelo, como en la tarjeta original
`CM8501B-D-11`, de modo que bastan cuatro entradas activas en bajo. El prototipo
no tiene función de vapor (revisión del 2026-10-06). De izquierda a derecha:

1. **Encendido** lógico.
2. **Café:** inicia la extracción; una segunda pulsación la termina, sin pausa.
3. **Tara** de la báscula de ensayo. No acciona salidas ni cambia el estado. Se
   rechaza con la bomba autorizada (`operational_fsm_tare_allowed`), porque
   anularía una masa que se está midiendo.
4. **Purga** del grupo mientras se mantiene pulsado, solo con la máquina lista.
   Se detiene al soltar, por tiempo máximo o ante un fallo, y se registra como
   evento, no como preparación.

La tara usa los pulsadores que eran de vapor. Las redes conservan su nombre
para no rehacer placas: la tara lee `BTN_PURGE_N` y la purga `BTN_STEAM_N`
(`panel_runtime.c`). Se descartó una segunda función de la tara por pulsación
larga, porque el barista tendría que distinguir dos usos por la duración.

Cada estado enciende un solo LED (`indicator_panel`), para no obligar a leer
combinaciones: amarillo parpadeando al calentar; verde fijo en «lista» dentro de
±0,6 °C y parpadeando fuera; azul con café o purga; rojo fijo en alarma y
parpadeando ante avería o pulsador retenido. Cualquier botón negado destella el
rojo. Si la báscula se aleja más de 1 g de cero tras la tara, un doble destello
azul cada 15 s lo avisa (`tare_watch`); el umbral es provisional, tres veces la
desviación en reposo de 0,29 g.

Encendido sostenido 3 s en alarma pide despejarla, y el supervisor solo lo
acepta si la causa ya no está (`process_supervisor_clear_allowed`). Encendido y
tara juntos 3 s reinician la placa, salvo con la bomba en uso; se descartó
encendido con café porque café solo arranca la bomba antes de completar el
gesto. Ningún gesto vale con un pulsador retenido (10 s), para que un contacto
en cortocircuito no reinicie en bucle (`panel_gesture`).

Sin red, que el detector de cruce por cero reconoce por sus 120 cruces por
segundo, encendido no enciende y no se avisan averías del guardián, porque nada
puede accionarse. Si la red falta 1 s seguido, la máquina vuelve a reposo y un
café en curso se cierra como pérdida de energía, sin alarma. El segundo es
provisional (`PROCESS_MAINS_LOSS_MS`).

## 5. Fundamento

El ADS1115 sigue el antecedente de Gaggiuino (`pressure_sensor.cpp`, revisión
`bddc57a`). La ficha del HK1100C dimensiona el divisor. La caracterización de
la Kalley K-EX mostró placas originales sin interfaz aislada reutilizable, lo
que justifica sustituirlas. El NTC original midió 94,7 kΩ cerca de 22 °C, con
respuesta monótona y aislado del calentador. La continuidad de `CM8501B-D-11`
confirmó los pares `S1∥S2`, `S3∥S4`, `S5∥S6` y `S7∥S8`.

## 6. Consecuencias

**Favorables.** Temperatura, presión y actuación se validan por separado. Se
reutiliza el NTC sin perforar el calentador. Un módulo se reemplaza sin
rediseñar. No se modifica la bandeja. La separación física aleja la red del
panel y los sensores.

**Desfavorables.** Más cableado, conectores y volumen, con riesgo de ruido y
desconexiones. Los módulos pueden diferir de lo anunciado. El ingreso manual
admite errores de transcripción. `PCB-MANDO` y `PCB-POTENCIA` no se separan sin
desoldar diez conductores, y una falla de arnés aparece como conductor roto, que
se detecta peor que un conector suelto. Un pulsador en cortocircuito deja la
acción activa, por lo que el firmware detecta el mando retenido.

## 7. Verificación

1. Existe un diagrama de interconexión con el dominio eléctrico de cada placa.
2. Ninguna entrada de la ESP32-S3 excede 3,3 V.
3. La interfaz acepta dosis, masa y TDS con unidad y rechaza valores inválidos.
4. El rendimiento conserva trazabilidad a esos tres valores.
5. Ningún dato manual modifica una extracción activa.
6. La cadena de temperatura se caracteriza antes de reportar desempeño.
7. La presión no tiene fugas, da un cero plausible, responde monótonamente y
   mantiene `PRESS_ADC` bajo 3,3 V.
8. Las etapas de 120 VAC se verifican antes de energizarse.
9. Los cuatro pares producen cuatro acciones únicas, se detecta un mando
   retenido y ninguna desconexión del arnés genera una orden activa.
10. La barrera de `PCB-POTENCIA` no une la red con la isla lógica.

El punto 8 pedía una revisión eléctrica independiente, que el proyecto no puede
pagar. La hace quien diseñó, con medidas inferiores: comprobación sobre el
Gerber exportado, huellas contra la pieza física a escala 1:1, módulos de red
probados en banco ([`firmware/tests/`](../../../firmware/tests/)), primera
puesta en marcha supervisada (ADR-008 § 5) y una lista de comprobación escrita
de antemano. Riesgo asumido: puede quedar un defecto que su autor no vea. El
prototipo no está certificado, no opera desatendido y no debe presentarse como
validado eléctricamente.

## 8. Criterios de reconsideración

Se revisa si el ingreso manual no basta para el protocolo, si los módulos no
cumplen los requisitos eléctricos o si las pruebas finales exigen una PCB
integral. Pesar en servicio requiere una nueva decisión.
