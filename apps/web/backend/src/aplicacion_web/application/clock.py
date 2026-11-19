"""Si el reloj de esta máquina está sincronizado, para dar la hora a la placa.

La placa no tiene reloj con respaldo y toma la hora de la respuesta de
sincronización. El 26-oct-2026 la Raspberry arrancó con el reloj de la noche
anterior, NTP lo corrigió 36 s después, y en ese rato la placa se quedó con la
hora vieja: fechó todo 9 h 55 min atrás. Mientras el reloj no esté sincronizado
la plataforma no entrega hora; la placa la vuelve a pedir en cada envío hasta
recibirla (`sync_runtime.c`, `synchronise_clock`).

El estado lo da el núcleo con adjtimex(2), que comparten los contenedores con
el anfitrión: devuelve TIME_ERROR (5) mientras el reloj no está sincronizado,
y systemd-timesyncd lo deja en sincronizado al ajustarlo. Sin adjtimex (otro
sistema operativo, solo en desarrollo) se da por sincronizado: no hay cómo
saberlo y negar la hora dejaría a la placa sin ella.
"""

import ctypes
import ctypes.util

TIME_ERROR = 5


def clock_is_synchronised() -> bool:
    try:
        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
        state = libc.adjtimex(ctypes.create_string_buffer(512))
    except (OSError, AttributeError):
        return True
    return state != TIME_ERROR
