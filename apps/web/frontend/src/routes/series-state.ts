import type { ExtractionHistoryRecord } from '@/shared/api/types'

/* Si ya llegó toda la serie de una preparación, para la ventana del café
   terminado y para la página de la preparación, que esperan lo mismo. */

// Holgura al comparar la última muestra con la duración del café.
const SERIES_END_MARGIN_MS = 2_000

// La serie está completa cuando su última muestra alcanza el final del café.
// Antes bastaba con que trajera alguna: el primer tramo es el minuto previo y
// los primeros segundos, y la ventana mostraba una curva de 3 s como la del café.
export function seriesComplete(data: ExtractionHistoryRecord | undefined): boolean {
  const samples = data?.series.samples ?? []
  if (!samples.length) return false
  const duration = data?.extraction.durationMs
  if (duration == null) return true
  return Math.max(...samples.map((sample) => sample.elapsedMs)) >= duration - SERIES_END_MARGIN_MS
}
