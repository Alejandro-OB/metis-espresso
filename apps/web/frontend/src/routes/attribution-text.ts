/**
 * Si lo que cambió en la taza se le puede atribuir al ajuste, dicho para el barista.
 *
 * La plataforma compara la preparación inicial con la de comprobación entre sí
 * (`packages/espresso_stability/attribution.py`, 8-nov-2026): si el agua sobre el
 * café cambió más de ±1 °C o la presión más de ±0,18 bar, la diferencia en la
 * taza puede venir de ahí y no del ajuste. Esto solo lo pone en palabras.
 *
 * Qué no hace: no juzga la masa ni la dosis (la masa es resultado del ajuste y la
 * dosis no tiene banda definida); las enseña sin veredicto.
 */

import type { AttributionCheck, EpisodeAttribution } from '@/shared/api/types'
import { formatNumber } from '@/shared/lib/format'

const NAMES = { water: 'El agua estimada', pressure: 'La presión' } as const
const UNITS = { water: '°C', pressure: 'bar' } as const

export const attributionHeadline: Record<EpisodeAttribution['status'], string> = {
  attributable: 'El agua y la presión no cambiaron más que su banda: el resultado se le puede atribuir al ajuste.',
  'not-attributable': 'Entre los dos cafés cambió algo más que el ajuste: el resultado no se le puede atribuir solo a él.',
  undetermined: 'Faltan datos para saber si cambió algo más que el ajuste.',
}

export function checkText(check: AttributionCheck): string {
  const name = NAMES[check.condition]
  const unit = UNITS[check.condition]
  if (check.status === 'adjusted') return `${name}: es lo que se ajustó, no se juzga.`
  if (check.status === 'no-data') return `${name}: sin dato en alguno de los dos cafés.`
  if (check.status === 'line-changed') return `${name}: los dos cafés usan rectas distintas para estimarla, no se comparan.`
  const sign = (check.difference ?? 0) > 0 ? '+' : (check.difference ?? 0) < 0 ? '−' : ''
  const amount = `${sign}${formatNumber(Math.abs(check.difference ?? 0), 2)} ${unit}`
  return check.status === 'changed'
    ? `${name} cambió ${amount}, más que la banda de ±${formatNumber(check.band, 2)} ${unit}.`
    : `${name} cambió ${amount}, dentro de la banda de ±${formatNumber(check.band, 2)} ${unit}.`
}
