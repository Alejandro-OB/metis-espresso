import type { PreparationProfile, ProfileSpecification } from '@/shared/api/types'
import { formatNumber } from '@/shared/lib/format'

/* Los valores del perfil que se ven y se editan en Preparar café, y cómo se
   resume una versión en una línea. Aparte del componente para que el
   historial use el mismo resumen sin cargar el editor. */

export type RecipeKey = 'water' | 'pressure' | 'maxTime' | 'dose' | 'ratio'
export type Recipe = Record<RecipeKey, string>

export const FIELDS: Array<{ key: RecipeKey; label: string; unit: string; shownUnit?: string; placeholder?: string; properties: string[]; edge: 'target' | 'maximum'; step: string }> = [
  { key: 'water', label: 'Temperatura del agua', unit: '°C', properties: ['water-temperature'], edge: 'target', step: '0.1' },
  { key: 'pressure', label: 'Presión', unit: 'bar', properties: ['extraction-pressure', 'pressure'], edge: 'target', step: '0.1' },
  { key: 'maxTime', label: 'Tiempo máximo', unit: 's', properties: ['extraction-time'], edge: 'maximum', step: '1' },
  { key: 'dose', label: 'Dosis', unit: 'g', properties: ['coffee-dose'], edge: 'target', step: '0.1' },
  // Se escribe solo el segundo número del ratio: la unidad «1 : ?» y el
  // ejemplo dentro de la caja lo dicen, sin texto debajo que lo explique.
  // La unidad que se guarda sigue siendo la del catálogo (:1).
  { key: 'ratio', label: 'Ratio', unit: ':1', shownUnit: '1 : ?', placeholder: 'ej. 2', properties: ['brew-ratio'], edge: 'target', step: '0.1' },
]

function specFor(profile: PreparationProfile, properties: string[]) {
  return profile.specifications?.find((item) => properties.includes(item.property))
}

function valueOf(spec: ProfileSpecification | undefined, edge: 'target' | 'maximum'): string {
  if (!spec) return ''
  if (edge === 'maximum') return spec.maximum == null ? '' : String(spec.maximum)
  if (spec.strategy === 'target') return spec.target == null ? '' : String(spec.target)
  return spec.minimum != null && spec.maximum != null ? String((spec.minimum + spec.maximum) / 2) : ''
}

export function recipeFrom(profile: PreparationProfile): Recipe {
  const recipe = {} as Recipe
  for (const field of FIELDS) recipe[field.key] = valueOf(specFor(profile, field.properties), field.edge)
  return recipe
}

// «v15 · 92 °C · 8,5 bar · 20 s · 1 : 2»: lo que falta se omite, porque un
// «—» en la lista se leía como un valor.
export function describe(profile: PreparationProfile): string {
  const recipe = recipeFrom(profile)
  const number = (raw: string) => formatNumber(Number(raw), Number.isInteger(Number(raw)) ? 0 : 1)
  return [
    `v${profile.version}`,
    recipe.water && `${number(recipe.water)} °C`,
    recipe.pressure && `${number(recipe.pressure)} bar`,
    recipe.maxTime && `${number(recipe.maxTime)} s`,
    recipe.dose && `${number(recipe.dose)} g`,
    recipe.ratio && `1 : ${number(recipe.ratio)}`,
  ].filter(Boolean).join(' · ')
}

