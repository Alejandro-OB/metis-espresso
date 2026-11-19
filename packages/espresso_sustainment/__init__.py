"""Sostenimiento del objetivo aprobado (ADR-018).

Paquete puro: no conoce la aplicación web, el almacenamiento ni el reloj. Recibe
lo que necesita para decidir y devuelve lo que decidió; quién guarda la decisión
y quién la baja a la placa es la plataforma. Hoy contiene la subida del primer
uso (§ 12); la corrección después de cada café se retiró el 2026-10-22 (§ 13).
"""
