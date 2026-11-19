# Firmware del controlador ESP32-S3

Proyecto ESP-IDF 6.1 / FreeRTOS para la tarjeta `ESP32-S3-N16R8` (16 MB de
flash, 8 MB de PSRAM) montada en `PCB-CONTROL`. Mide temperatura y presión,
regula el calentador y la bomba con dos lazos PI, gobierna la máquina de estados
del panel y entrega los registros a la plataforma por Wi-Fi.

## Seguridad de las salidas de potencia

La bomba y el calentador pasan siempre por el **guardián de actuación**
(`actuation_guard`). Decide en cada ciclo, antes que cualquier controlador y por
una ruta propia, si cada salida puede energizarse. Inhibe, entre otras causas,
durante el arranque, fuera de un estado operativo que lo autorice, con una
medida no apta, por encima del límite duro de temperatura o de presión, sin
cruce por cero de la red, cuando la adquisición incumple su plazo y cuando la
bomba agota su servicio continuo (120 s, con 60 s de reposo obligatorio).

Un lazo solo puede actuar si está **puesto en servicio**: el firmware se compiló
con el archivo de planta identificada de ese lazo en
[`main/commissioned/`](main/commissioned/README.md). Sin ese archivo el guardián
inhibe la salida por esa causa. No hay opción de configuración, orden por el
puerto ni mensaje de red que lo cambie.

Además, el componente de cargador `bootloader_components/salidas_en_reposo` pone
`PUMP_CMD` y `HEATER_CMD` en bajo desde el cargador de arranque, antes de que
corra la aplicación.

## Estructura

Cada decisión que puede equivocarse vive en un módulo de C puro (sin ESP-IDF ni
FreeRTOS), que se prueba en el computador; el archivo `*_runtime.c` del mismo
nombre es la envoltura con el hardware. Algunos de los principales:

| Módulo | Función |
|---|---|
| `acquisition_runtime`, `ads1115`, `sensor_scaling`, `signal_filter`, `signal_quality` | Lectura por el ADS1115 de la presión cada 10 ms y de la temperatura (NTC) cada 100 ms, con su calidad de señal |
| `control_runtime`, `pi_controller`, `control_tuning`, `control_filter` | Lazos PI de presión (cada 20 ms) y de temperatura (cada 200 ms); la sintonía sale de la planta identificada por el método SIMC |
| `psm_scheduler`, `zero_cross_monitor` | Bomba: actuación por semiciclos sincronizada con el cruce por cero del dimmer |
| `heater_modulator` | Calentador: tiempo de conducción del SSR dentro de una ventana de 1 s |
| `actuation_guard`, `process_supervisor`, `supervisor_runtime` | Inhibición de las salidas y vigilancia del proceso |
| `operational_fsm` | Máquina de estados: `SAFE_INACTIVE`, `COFFEE_HEATING`, `READY`, `EXTRACTION`, `MANUAL_PURGE`, `ALARM` |
| `panel_runtime`, `panel_gesture`, `indicator_panel` | Pulsadores y LED de `PCB-MANDO` |
| `extraction_runtime`, `extraction_profile`, `extraction_session` | Registro de cada extracción contra el perfil vigente |
| `water_reference`, `first_use_offset` | Recta caldera → agua y compensación del primer uso |
| `nvs_outbox`, `sync_runtime`, `sync_transport` | Cola persistente y entrega a la plataforma |
| `firmware_update` | Actualización por Wi-Fi con vuelta atrás |
| `simulated_plant`, `control_plant` | Planta simulada y modelo de banco, sin cargas físicas |
| `pin_map.h` | Asignación de pines |

## Compilar

`idf.sh` carga ESP-IDF desde `IDF_PATH` o, si no está definida, desde
`~/esp/esp-idf-v6.1`, y luego llama a `idf.py` con los argumentos que reciba.

```bash
./idf.sh set-target esp32s3
./idf.sh menuconfig
./idf.sh build
```

`sdkconfig.defaults` deja activos `CAFE_SIMULATION` y, por omisión de Kconfig,
`CAFE_CONTROL_BENCH_LOOP`: los dos lazos regulan contra un modelo y las salidas
físicas quedan en bajo. Como `main/commissioned/` trae las plantas identificadas,
**esa combinación no compila** a propósito: un firmware que puede energizar la
máquina no admite planta simulada. Para el binario de operación hay que
desactivar en `menuconfig` (menú «Diagnóstico de la cafetera»):

- `CAFE_SIMULATION`
- `CAFE_CONTROL_BENCH_LOOP`

Opciones de enlace (requieren `CAFE_SYNC`):

| Opción | Uso |
|---|---|
| `CAFE_WIFI_SSID`, `CAFE_WIFI_PASSWORD` | Red de 2,4 GHz |
| `CAFE_SYNC_URL` | Dirección de `POST /api/v1/sync` en la plataforma |
| `CAFE_SYNC_TOKEN` | *Token* de escritura de la plataforma |
| `CAFE_DEVICE_ID` | `deviceId` estable de los mensajes |
| `CAFE_FIRMWARE_UPDATE`, `CAFE_FIRMWARE_UPDATE_URL` | Actualización por Wi-Fi y su manifiesto |

Estos valores quedan en `sdkconfig`, que no se versiona. Las credenciales no se
escriben en `sdkconfig.defaults`.

Los binarios de identificación (`CAFE_IDENTIFICATION`) y de ensayo hidráulico
(`CAFE_HYDRAULIC_TEST`) son variantes de prueba; un equipo en servicio lleva el
binario de operación.

## Cargar y observar

Por el puerto USB marcado `COM`, con el enlace `JP_EXT_5V` de `PCB-CONTROL`
retirado antes de conectar el cable:

```bash
./idf.sh -p /dev/ttyACM0 flash
./idf.sh -p /dev/ttyACM0 -b 921600 monitor
```

La consola va a **921600 baudios**, no a 115200: por el mismo puerto sale el
registro de los lazos, y a 115200 no cabe. Si la tarjeta aparece como
`/dev/ttyUSB0`, se cambia el puerto. Se sale del monitor con `Ctrl+]`.

Al arrancar, la consola recorre las comprobaciones (chip, flash, PSRAM, salidas
en estado seguro, NVS, cola persistente, radio, lazos de control, adquisición,
supervisión, mando, enlace) y termina con `RESULTADO GENERAL`. Cada lazo informa
su sintonía y si está puesto en servicio. Después se imprime un `heartbeat` cada
5 s con el estado de las salidas de potencia.

## Particiones y actualización por Wi-Fi

`partitions.csv` reserva 256 KiB de NVS y dos particiones de aplicación de 3 MiB
(`ota_0`, `ota_1`), sin partición `factory`. `sdkconfig.defaults` activa
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`.

La placa no descarga nada por iniciativa propia ni por orden de la red. Con la
máquina apagada, **tara y purgado sostenidos 3 s** le piden buscar el manifiesto
configurado (`CAFE_FIRMWARE_UPDATE_URL`, o la orden `C,ota,<dirección>` por el
puerto serie), cuyo formato es:

```json
{"project":"…","version":"…","size":1022656,"imageSha256":"<64 hex>"}
```

La imagen se busca en la misma carpeta como `firmware.bin`. Mientras baja,
azul y amarillo parpadean juntos; si se niega, destella el rojo. La versión nueva
arranca a prueba y, si no se confirma en 2 min, el cargador vuelve a la anterior.
La tabla de particiones y el cargador no viajan por Wi-Fi: la primera carga con
esta tabla se hace por USB.

## Puerto serie de servicio

La misma consola acepta órdenes de texto, una por línea: configuración (`C,…`),
calibración de sensores, sintonía y ensayos, e inyección de eventos de la máquina
de estados (`V,<evento>`).

También expone un canal de prueba con planta externa: recibe
`I,<time_ms>,<temperature_c>,<pressure_bar>,<mode>` y responde
`O,<time_ms>,<heater_duty>,<pump_duty>`. La respuesta sale de una regla fija de
prueba (todo o nada a 93 °C y 9 bar), no de los lazos PI, y nunca toca los GPIO
de potencia. El puente del lado del computador es
[`device/software/modelica/fmu_hil_bridge.py`](../../software/modelica/fmu_hil_bridge.py).

## Pruebas de anfitrión

Las pruebas de los módulos puros están en [`device/tests/host/`](../../tests/host/),
una por módulo con su `run_*.sh`. Se compilan con
`-std=c17 -Wall -Wextra -Werror -pedantic` y no necesitan ESP-IDF. Desde `device/`:

```bash
for s in tests/host/run_*.sh; do "$s" || break; done
```

`test_control_loop.c` monta la cadena completa —medida, filtro, PI,
cuantización, actuación discreta y planta— y comprueba que el conjunto regula.
Los ejecutables se escriben en `device/tests/build/`.
