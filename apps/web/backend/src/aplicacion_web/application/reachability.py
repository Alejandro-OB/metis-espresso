"""Si la máquina responde en la red cuando no manda lecturas.

Sin lecturas, la plataforma no distingue una máquina desenchufada de una
encendida que dejó de enviar: las dos se veían como «Sin señal» y el barista
no sabía qué hacer (pedido del usuario del 26-oct-2026). La placa va con la
corriente de la cafetera, así que si no contesta un ping está apagada o sin
red; si contesta y no manda lecturas, el fallo es otro y se dice distinto.

La dirección es la última desde la que la placa sincronizó (`X-Real-IP` que
pone nginx); no se configura a mano porque el router la asigna por DHCP.

Se usa un socket ICMP de datagrama, que Linux permite sin privilegios dentro
del rango `net.ipv4.ping_group_range` (Docker lo abre para los contenedores).
Si el sistema no lo permite, la respuesta es «no se sabe» (None) y no «no
responde»: negarla afirmaría que la máquina está apagada sin haberlo visto.

No hace: no despierta ni reinicia la placa, ni le manda nada aparte del eco.
"""

import os
import select
import socket
import struct
import time

ECHO_REQUEST = 8
ECHO_REPLY = 0


def _checksum(data: bytes) -> int:
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack(f"!{len(data) // 2}H", data))
    total = (total >> 16) + (total & 0xFFFF)
    total += total >> 16
    return ~total & 0xFFFF


def answers_ping(address: str, timeout_s: float = 1.0) -> bool | None:
    """True si contesta el eco, False si no en `timeout_s`, None si no se puede probar."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_ICMP)
    except OSError:
        return None
    with sock:
        payload = os.urandom(8)
        header = struct.pack("!BBHHH", ECHO_REQUEST, 0, 0, 0, 1)
        packet = struct.pack("!BBHHH", ECHO_REQUEST, 0, _checksum(header + payload), 0, 1) + payload
        try:
            sock.sendto(packet, (address, 0))
        except OSError:
            return False
        deadline = time.monotonic() + timeout_s
        while (left := deadline - time.monotonic()) > 0:
            ready, _, _ = select.select([sock], [], [], left)
            if not ready:
                break
            try:
                reply = sock.recv(1024)
            except OSError:
                return False
            # Con socket de datagrama el núcleo entrega el ICMP sin cabecera IP.
            if reply[:1] == bytes([ECHO_REPLY]) and reply[8:16] == payload:
                return True
        return False
