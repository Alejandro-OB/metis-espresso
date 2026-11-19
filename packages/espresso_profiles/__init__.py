"""Lectura de una configuración de preparación, venga en la forma que venga.

El sistema guarda las referencias de una preparación de tres maneras distintas,
y las tres son legítimas porque nacieron para usos distintos:

- el **catálogo** de la aplicación las describe como especificaciones, cada una
  con su magnitud, su objetivo y su tolerancia;
- una **instantánea** de preparación las agrupa por magnitud en objetivos y
  tolerancias, que es lo que la interfaz necesita para dibujar bandas;
- el **contrato del dispositivo** las lleva como campos planos, porque el
  firmware no interpreta estructuras anidadas.

Este módulo traduce cualquiera de las tres a la forma del contrato. Existe para
que ese criterio viva en un solo sitio: estaba escrito en el lado de análisis y
el backend lo necesitaba para enviar configuraciones al dispositivo, y
duplicarlo habría dejado dos definiciones de la misma cosa que podían divergir.

No completa lo que falte. Si una referencia no está declarada en ninguna de las
tres formas, se devuelve vacía y quien la reciba decide.
"""

from .contract_view import profile_contract_view, profile_reference

__all__ = ["profile_contract_view", "profile_reference"]
