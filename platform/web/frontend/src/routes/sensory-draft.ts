import type { DesiredSensoryChange, SensoryTarget } from '@/shared/api/types'
import { sensoryDescriptorValue, sensoryDescriptorValues } from './sensory-options'

/* El sabor buscado mientras se edita dentro del formulario del perfil, y cómo
   se convierte en lo que recibe el servidor. Aparte del componente para que
   el formulario lo use sin cargar la pantalla de edición. */

export type Direction = DesiredSensoryChange['direction']
export type SensoryTargetPayload = Omit<SensoryTarget, 'id' | 'definedBy' | 'recordedAt' | 'inheritedFrom'>

export interface SensoryDraft { descriptors: Set<string>; others: string[]; changes: Record<string, Direction>; rationale: string }

export function sensoryDraftFrom(target: SensoryTarget | null | undefined): SensoryDraft {
  return {
    descriptors: new Set(target?.descriptors.map(sensoryDescriptorValue).filter((value) => sensoryDescriptorValues.has(value)) ?? []),
    others: [...(target?.otherDescriptors ?? [])],
    changes: Object.fromEntries((target?.desiredChanges ?? []).map((change) => [change.attribute, change.direction])),
    rationale: target?.rationale ?? '',
  }
}

// Sin descriptores no hay sabor buscado; con ellos y sin notas, un error. Los
// cambios deseados que traiga un objetivo viejo viajan tal cual.
export function sensoryPayload(draft: SensoryDraft, coffeeId: string): { target: SensoryTargetPayload | null; error?: string } {
  const desiredChanges = Object.entries(draft.changes).map(([attribute, direction]) => ({ attribute: attribute as DesiredSensoryChange['attribute'], direction }))
  if (!draft.descriptors.size && !draft.others.length) return { target: null }
  if (!draft.rationale.trim()) return { target: null, error: 'Escribe las notas del barista sobre el sabor buscado.' }
  return { target: { coffeeId, rationale: draft.rationale.trim(), descriptors: [...draft.descriptors], otherDescriptors: draft.others, desiredChanges } }
}

