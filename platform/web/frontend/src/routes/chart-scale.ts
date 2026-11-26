/* Escala compartida por las gráficas de la aplicación.

   Las tres —la curva de una preparación, la vista en vivo y la comparación—
   dibujaban la línea y la banda del objetivo sin un solo número en los ejes.
   Se veía la forma, pero no se podía leer a qué temperatura iba la curva ni en
   qué segundo ocurrió nada. Los valores ya estaban en el dibujo; lo que
   faltaba era decir cuánto valen.

   Vive aparte porque las tres gráficas deben repartir las marcas igual: si
   cada una elige las suyas, dos curvas puestas lado a lado dejan de ser
   comparables de un vistazo, que es justo para lo que están. */

/** Marcas repartidas por igual entre dos valores, extremos incluidos. */
export function ticks(minimum: number, maximum: number, count = 4): number[] {
  if (!Number.isFinite(minimum) || !Number.isFinite(maximum) || maximum <= minimum) return [minimum]
  return Array.from({ length: count + 1 }, (_, index) => minimum + (maximum - minimum) * (index / count))
}

/** Cuántos decimales necesita la escala para que dos marcas no se repitan. */
export function tickDigits(minimum: number, maximum: number, count = 4): number {
  const step = (maximum - minimum) / count
  if (!Number.isFinite(step) || step <= 0) return 0
  if (step >= 10) return 0
  if (step >= 1) return 1
  return 2
}
