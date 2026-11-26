# ADR-021 — Firmware de la placa por Wi-Fi desde el computador

**Estado:** Aceptada e implementada (verificada en la placa, § 9)

## 1. Contexto

Con la cafetera sellada, el conector USB de la placa queda inaccesible, y cerca
del vapor el USB llegó a caerse. La placa tenía una sola partición de programa
(`factory`, 3 MB en 0x50000); la memoria es de 16 MB y la imagen ocupa 1,06 MB.
El firmware se genera en el computador de desarrollo, no en la plataforma.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| La plataforma ordena la actualización | Le daría poder sobre el programa que decide el guardián, contra ADR-001 § 6. Descartada. |
| Servidor web en la placa | Cualquiera en la red podría empujar código. Descartada. |
| Conector USB en la carcasa | Se consideró primero y no se adoptó. |
| Gesto en el mando y descarga desde el computador | **Seleccionada.** |
| `factory` más dos copias | Si ambas fallaran, arrancaría una imagen vieja con un acta que ya no rige. Descartada. |
| Solo la verificación de `esp_ota_end` | No distingue un binario viejo del publicado. Descartada. |
| Imágenes firmadas | Exige custodiar una clave y cambiar el cargador. Aplazada (§ 7). |

## 4. Decisión

1. Particiones `ota_0` en 0x50000 y `ota_1` en 0x350000, de 3 MB, y `otadata`
   en 0x4A000 (`firmware/partitions.csv`); `nvs` no se mueve.
2. La actualización solo empieza al sostener tara y purgado 3 s
   (`panel_gesture`), con la máquina apagada (`OP_STATE_SAFE_INACTIVE`) y sin
   modo restringido (`firmware_update_decide`). Ningún mensaje de red la pide.
3. Mientras dura, el guardián corta ambas salidas con
   `ACTUATION_INHIBITED_STARTING_UP`. Si alguien enciende la máquina, se abandona.
4. La placa baja del computador un manifiesto con proyecto, versión, tamaño y
   huella. Rechaza el manifiesto inválido y la imagen escrita cuya huella no
   coincide. La dirección se fija con `C,ota,<dirección>` o al compilar.
5. La imagen nueva arranca a prueba y se confirma solo si la supervisión corre y
   la placa obtiene red en 2 min; si no, el cargador vuelve a la anterior. Con la
   cafetera sellada, una imagen sin red ya no podría reemplazarse.

Escribir la memoria de programa detiene la caché, por eso se exige reposo. La
vuelta atrás vive en el cargador porque una imagen rota no puede retirarse sola.

## 6. Consecuencias

La primera carga con la tabla nueva se hace por USB antes de sellar. Cada
`sdkconfig` necesita `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`; sin ella el
firmware no compila. Los registros detallados del lazo siguen saliendo por USB.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Alguien suplanta al computador y espera al gesto | Aceptado: red aislada ([ADR-014](ADR-014-exposicion-de-la-plataforma-en-la-red.md)) y gesto presencial; se cierra con imágenes firmadas si la red deja de ser de confianza. |
| Corte de energía durante la escritura | Se escribe en la copia que no corre. |
| La red tarda más de 2 min | La imagen vuelve atrás; el plazo `FIRMWARE_UPDATE_CONFIRM_TIMEOUT_MS` es provisional. |

## 9. Verificación en la placa

Una imagen de 1 061 056 bytes se escribió en 5,6 s y se confirmó a los 8,8 s. Una
imagen sin red válida volvió a la anterior a los 125,7 s. Faltan el rechazo con
la máquina encendida y el encendido durante la descarga.
