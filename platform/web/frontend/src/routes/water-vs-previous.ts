/**
 * El agua estimada de una preparación frente a la anterior, dicha para el barista.
 *
 * La plataforma ya la calculaba (`water_between_preparations` en
 * `packages/espresso_stability/machine.py`, banda de ±1 °C) y ninguna pantalla
 * la mostraba (8-nov-2026). Es la estabilidad térmica que juzga la monografía,
 * café a café, y la ficha enseña solo el agua estimada de esa preparación.
 *
 * Qué no hace: no recalcula la diferencia ni la estima cuando la plataforma no
 * pudo (sin preparación anterior, fuera del rango de la recta o con la recta
 * cambiada entre las dos): en esos casos no dice nada.
 */

import type { ExtractionResult } from '@/shared/api/types'
import { formatNumber } from '@/shared/lib/format'

export function waterVsPrevious(result: ExtractionResult | undefined): string | null {
  const water = result?.machineStability?.water
  if (water?.status !== 'estimated' || water.differenceC == null) return null
  const sign = water.differenceC > 0 ? '+' : water.differenceC < 0 ? '−' : ''
  const verdict = water.within ? 'dentro' : 'fuera'
  return `${sign}${formatNumber(Math.abs(water.differenceC), 2)} °C frente a la preparación anterior, ${verdict} de la banda de ±${formatNumber(water.bandC, 1)} °C`
}
