# ADR-020 — Cómo entran al firmware la calibración y la planta identificada

**Estado:** Aceptada, 2026-10-01

## 1. Contexto

La puesta en servicio necesita introducir en el firmware la calibración de las
cadenas de medida (F2) y la planta identificada de cada lazo (F3).
`sensor_calibration_store` ya guardaba la calibración en NVS y cada medida
declaraba su identificador, pero nada la escribía. `config_command` ya
interpretaba órdenes `C,…` del puerto de servicio. `control_tuning` calculaba las
ganancias sobre el modelo de banco, con `commissioned` fijo en falso.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Canal de configuración de la plataforma ([ADR-017](ADR-017-activacion-de-la-configuracion-efectiva.md)) | Es para perfiles que el barista elige, y la autoridad sobre el dispositivo queda fuera de la red. Descartada. |
| Actualización remota del firmware | Cada calibración cambiaría el firmware y choca con que la autoridad sobre el dispositivo quede fuera de la red. Descartada. |
| Puerto de servicio (USB) | Exige estar frente a la máquina. **Seleccionada para la calibración**; descartada para la planta, cuya marca habilita la actuación. |
| Archivo versionado, fijado en compilación | Cada cambio pasa por un commit y un acta; para la calibración es un costo sin beneficio. **Seleccionada para la planta.** |

## 4. Decisión

### 4.1 F2 — La calibración entra por el puerto de servicio

Los coeficientes del NTC (`R25`, `β`), de la cadena de presión y de la báscula
llegan como una orden USB, que interpreta el módulo puro de
`calibration_command.h` y guarda `sensor_calibration_store`. Las tres cadenas
comparten un registro con un identificador. Una orden sin identificador y versión
se rechaza, y unos coeficientes imposibles se rechazan sin escribir. La
calibración rige desde el siguiente arranque, para que una serie no mezcle dos.
Un programa del computador calcula los coeficientes desde los registros y los
envía solo si quien calibra los aprueba.

### 4.2 F3 — La planta identificada se fija en compilación

`K`, `θ`, `τ`, `λ`, la constante del filtro y los límites de cada lazo se
escriben, desde el acta de puesta en servicio, en un archivo versionado que se
compila en el firmware
([`firmware/main/commissioned/`](../../../firmware/main/commissioned/README.md)).
`commissioned` es verdadero, por lazo, solo en un binario compilado con ese
archivo, y ninguna orden externa cambia la planta ni la marca. El identificador
de la sintonía viaja en el registro.

Solo el binario de identificación (`CAFE_IDENTIFICATION`) admite, tras la
confirmación física en el mando y con el modo restringido corriendo, cambiar en
caliente `λ` y la integral por USB (`A,lambda,<s>`, `A,integral,on|off`).
[ADR-009](ADR-009-control-local-temperatura-pi-ciclo-integral.md) § 5 pide subir
la ganancia poco a poco, y recompilar por cada valor sería inviable. El guardián
conserva su tope y sus causas de corte, y la salida del PI no pasa del techo de
la secuencia. La planta provisional llega por Kconfig; sin ella, el calentador
queda en cero. Cada cambio queda en la columna `tuning` del registro, y el valor
hallado se congela en un acta.
La red queda fuera también aquí: el operador ya está junto a la máquina.

## 5. Justificación

La calibración solo cambia cómo se interpreta una lectura, y un error se delata
al compararla con el termómetro de referencia. La planta va unida a
`commissioned`, la llave con que el guardián deja energizar la bomba y el
calentador; esa llave no debe abrirse con una orden de una línea.

## 6. Consecuencias

El equipo puede recalibrarse sin herramientas de desarrollo, y cada sintonía
queda en el repositorio con su acta
([`docs/engineering/actas/`](../../engineering/actas/README.md)). Repetir la
puesta en servicio exige recompilar. Una calibración cargada por USB solo deja
rastro en el identificador de las medidas y en su acta.

## 9. Verificación de la decisión

La decisión está implementada si una calibración cargada por USB rige tras el
reinicio con su identificador, una orden inválida se rechaza sin escribir y
ningún binario sin archivo de planta reporta `commissioned` verdadero.
