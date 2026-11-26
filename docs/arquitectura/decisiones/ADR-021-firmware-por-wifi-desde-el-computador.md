# ADR-021 — Firmware de la placa por Wi-Fi desde el computador

**Estado:** Propuesta
**Fecha de redacción:** 2026-10-25
**Fecha de aprobación:** pendiente
**Ámbito:** cómo llega un firmware nuevo a la ESP32-S3 con la cafetera sellada, quién lo ordena, qué lo bloquea y cómo se vuelve atrás

## 1. Contexto

Hasta ahora el firmware se carga solo por USB. Con la cafetera sellada el
conector queda dentro y no se alcanza (el usuario, 2026-10-25). El USB tampoco es
fiable cerca del vapor: el 2026-10-11 la placa se cayó del bus dos veces
(hallazgos § 14.17).

### 1.1 Situación comprobada

Comprobado el 2026-10-25 sobre el repositorio:

| Elemento | Situación |
|---|---|
| Tabla de particiones | Una sola partición de programa, `factory`, de 3 MB en 0x50000; sin `otadata` |
| Memoria | 16 MB; la imagen de operación ocupa 1,06 MB |
| Vuelta atrás del cargador | Desactivada |
| Corte por actualización en el guardián | Prevista desde el principio (`ACTUATION_INHIBITED_STARTING_UP`) y nunca usada: `.starting_up` era siempre falso |
| Origen del firmware | El computador de desarrollo; no pasa por la plataforma externa (`docs/alcance-produccion-plataforma-externa.md` § 9) |

## 2. Problema de decisión

Se debe definir cómo recibe la placa un firmware nuevo sin USB, sin abrir una vía
por la que la red pueda mandar sobre el dispositivo y sin dejar la máquina sin
programa si la imagen nueva falla.

## 3. Alternativas consideradas

### 3.1 Quién ordena la actualización

| Alternativa | Evaluación |
|---|---|
| La plataforma externa avisa en la sincronización y la placa instala | Le da a la Raspberry el poder de cambiar el programa que decide el guardián, contra ADR-001 § 6 y RD-002, y hace pasar el firmware por ella, contra el alcance de producción § 9. Descartada. |
| Un servidor web en la placa que recibe la imagen | Abre un puerto en la placa por el que cualquiera en la red puede empujar código. Descartada. |
| Un gesto del mando, delante de la máquina, y la placa baja la imagen del computador | La red solo trae el archivo; la orden exige estar presente. **Seleccionada.** |
| Sacar el USB a un conector en la carcasa | No toca el firmware y sirve también para los registros. Se recomendó primero; el usuario eligió la vía por Wi-Fi. |

### 3.2 Particiones

| Alternativa | Evaluación |
|---|---|
| `factory` más dos copias | Gasta 9 MB, y si las dos copias fallaran el cargador arrancaría una imagen vieja con un acta de control que ya no rige. Descartada. |
| Dos copias sin `factory`, `ota_0` donde estaba `factory` | La primera carga por USB deja el programa donde estaba y `nvs` no se mueve. **Seleccionada.** |

### 3.3 Integridad de la imagen

| Alternativa | Evaluación |
|---|---|
| Solo la verificación de `esp_ota_end` | Dice que el archivo está entero, no que sea el publicado: un binario viejo en la carpeta pasaría igual. Insuficiente. |
| Huella publicada en un manifiesto, comparada tras escribir | Asegura que lo que arranca es lo que el computador publicó. **Seleccionada.** |
| Imágenes firmadas | Cierra también la suplantación del computador. Exige custodiar una clave y cambiar el cargador; aplazada (§ 7). |

## 4. Decisión

1. **Dos particiones de programa, `ota_0` en 0x50000 y `ota_1` en 0x350000, de
   3 MB cada una, y `otadata` en 0x4A000.** `nvs` y `phy_init` no se mueven.
2. **La actualización solo empieza con un gesto del mando:** tara y purgado
   sostenidos 3 s con la máquina apagada (`panel_gesture`). Ningún mensaje de red
   puede pedirla.
3. **Solo con la máquina apagada (`OP_STATE_SAFE_INACTIVE`), sin ninguna salida
   admitida por el guardián y sin el modo restringido armado**
   (`firmware_update_decide`).
4. **Mientras dura, el guardián corta las dos salidas** por
   `ACTUATION_INHIBITED_STARTING_UP`, y no se escribe nada hasta verlo
   publicado. Si alguien enciende la máquina durante la descarga, se abandona y
   sigue la versión que corría.
5. **La placa baja la imagen del computador, nunca de la plataforma.** El
   manifiesto declara proyecto, versión, tamaño y la huella anexada a la imagen;
   la imagen se busca en la misma carpeta. La dirección se fija una vez por el
   puerto de servicio (`C,ota,<dirección>`) o al compilar.
6. **Se rechaza** un manifiesto incompleto, de otro proyecto, que no cabe, o
   cuya imagen ya corre, y una imagen escrita cuya huella no es la del manifiesto.
7. **La imagen nueva arranca a prueba** y se declara válida solo si la
   supervisión corre, el guardián ya decidió y la placa obtuvo dirección en la
   red en 2 min. Si no, o si la placa se reinicia antes, el cargador vuelve solo a
   la anterior. La red forma parte de la prueba porque, con la cafetera sellada,
   una imagen sin red ya no se podría reemplazar.
8. **El panel lo muestra:** azul y amarillo parpadean juntos mientras dura; un
   rechazo destella el rojo como cualquier botón negado.

## 5. Justificación

1. **La autoridad sigue fuera de la red.** Es la misma regla que ya rige la
   configuración del enlace (`config_command`, RD-002): la red puede traer datos,
   no órdenes.
2. **Reposo, no solo «sin bomba».** Escribir la memoria de programa detiene la
   caché en cada sector; con la máquina apagada no hay nada que regular. En
   `READY` el primer dedo en el purgado abriría el agua.
3. **La vuelta atrás vive en el cargador** porque una imagen rota no puede
   confiarse a sí misma la decisión de retirarse.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- Se puede cambiar el firmware con la cafetera sellada.
- Una imagen que no arranca bien no deja la máquina sin programa.
- La versión que corre queda en cada extracción (`firmwareVersion`), así que la
  plataforma muestra qué firmware hizo cada café.

### 6.2 Consecuencias desfavorables

- **La primera vez hay que cargar por USB, antes de sellar:** cambian la tabla y
  el cargador de arranque.
- **Cada `sdkconfig` necesita `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`.** No
  heredan `sdkconfig.defaults`; el firmware no compila sin ella si admite la
  actualización, para que el fallo salga en el computador y no en la placa.
- Los registros detallados del lazo siguen saliendo solo por USB.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Alguien suplanta al computador en la red local y espera al gesto | Aceptado por ahora: red aislada (ADR-014) y gesto presencial. Se cierra con imágenes firmadas si la red deja de ser de confianza. |
| Los retrasos de la escritura disparan una alarma | La alarma no abandona la actualización: en alarma las salidas ya están cortadas. |
| Corte de energía a mitad de la escritura | Se escribe en la copia que no corre; la que corre no se toca hasta elegir la nueva. |
| La red del laboratorio tarda más de 2 min tras el reinicio | La imagen vuelve atrás sin estar rota; se repite, o se sube el plazo (`FIRMWARE_UPDATE_CONFIRM_TIMEOUT_MS`, provisional). |
| Se carga por USB un binario sin vuelta atrás en el cargador | El firmware no compila (§ 6.2). |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Prueba en la placa | Hechas la actualización completa y la vuelta atrás (§ 9.1). Falta el rechazo con la máquina encendida, que exige la cafetera enchufada. |
| Aprobación | Este ADR, por el usuario. |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. carga por USB con la tabla nueva y arranque desde `ota_0` con `nvs` intacta;
2. gesto con la máquina apagada: descarga, reinicio, confirmación y versión nueva
   en la siguiente extracción;
3. gesto con la máquina encendida: rechazo con destello rojo y nada escrito;
4. encendido durante la descarga: se abandona y sigue la versión anterior;
5. imagen que no alcanza la red: vuelta a la anterior a los 2 min;
6. manifiesto con una huella cambiada: imagen rechazada tras escribirla.

### 9.1 Verificado el 2026-10-25 en la placa

Placa alimentada por USB, cafetera sin red eléctrica, máquina apagada. Registros
de consola en `~/.local/share/cafe-builds/operacion-v9-wifi-b/` (`placa.log`,
`placa-pruebas.log`), fuera del repositorio.

| Punto | Resultado |
|---|---|
| 1 | Carga por USB de `operacion-v9-wifi` (acta v9 más este ADR): arranca desde `ota_0`, diagnóstico aprobado; calibración, báscula, cola (0/96) y dirección guardada intactas. |
| 2 | Gesto: manifiesto e imagen de 1 061 056 bytes escritos en `ota_1` en 5,6 s, con bomba y calentador en `starting-up` y azul y amarillo lentos; arranca `v9-wifi-prueba-b` a prueba y se confirma a los 8,8 s, al obtener red. |
| 3 | No probado: sin red eléctrica el encendido no sale del reposo. Probado en su lugar el rechazo de la versión que ya corre: lee el manifiesto, no pide la imagen y no escribe nada. |
| 4 | No probado, por la misma razón que el punto 3. |
| 5 | Imagen con una red inexistente: arranca a prueba en `ota_0`, a los 125,7 s se declara no confirmada y el cargador vuelve a `ota_1` (`v9-wifi-prueba-b`), que arranca con red. |
| 6 | No probado en la placa; cubierto por las pruebas de anfitrión de `firmware_update`. |

Abrir la consola por USB reinicia la placa: ocurrió dos veces, siempre con la
máquina apagada.

## 10. Trazabilidad

- [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md) § 6: la plataforma
  no escribe actuadores.
- [ADR-014](ADR-014-exposicion-de-la-plataforma-en-la-red.md): aislamiento de la red.
- [ADR-016](ADR-016-arranque-actualizacion-y-reversion.md): el mismo principio para
  la plataforma, actualización deliberada y con vuelta atrás.
- Código: `firmware_update`, `firmware_update_runtime`, `panel_gesture`,
  `supervisor_runtime_set_updating`, `partitions.csv`;
  `device/software/firmware_update/publish.py`.
- Requisitos: RD-002.

## 11. Criterios de reconsideración

La decisión deberá revisarse si la red deja de estar aislada, si la placa pasa a
servirse desde otra máquina que no sea el computador de desarrollo, o si la
imagen supera los 3 MB de cada copia.
