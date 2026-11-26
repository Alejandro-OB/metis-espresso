/* Cómo se presenta una preparación según cómo terminó. Va aparte de los
   componentes para poder probarse sin montar la pantalla, como water-pairing.ts. */
import type { Extraction } from '@/shared/api/types'
import { formatDateTime } from '@/shared/lib/format'

const POWER_LOSS = 'unexpected-power-loss'

/** Si un corte de energía dejó la preparación abierta (ADR-005 § 8 punto 10). */
export function interruptedByPowerLoss(extraction: Pick<Extraction, 'termination'>): boolean {
  return extraction.termination === POWER_LOSS
}

/** Fecha con que se presenta una preparación: el final; si no consta, el inicio; si tampoco, nada. */
export function preparationMomentLabel(extraction: Pick<Extraction, 'termination' | 'startedAt' | 'finishedAt'>): string {
  if (!interruptedByPowerLoss(extraction)) return formatDateTime(extraction.finishedAt)
  return extraction.startedAt ? `Empezó ${formatDateTime(extraction.startedAt)} · incompleta` : 'Sin hora de inicio · incompleta'
}
