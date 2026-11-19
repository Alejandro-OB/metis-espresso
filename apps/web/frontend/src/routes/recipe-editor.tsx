import { useEffect, useState, type MutableRefObject } from 'react'
import { useMutation, useQueryClient } from '@tanstack/react-query'
import { post } from '@/shared/api/client'
import type { PreparationProfile, ProfileSpecification } from '@/shared/api/types'
import { Alert, Button, Field, NumberField, Select } from '@/shared/ui'
import { WATER_TEMPERATURE_RANGE_C } from './profile-options'
import { describe, FIELDS, recipeFrom, type Recipe } from './recipe-values'
import styles from './routes.module.css'

/* Los campos del perfil que rige en la máquina, llenos con sus valores, y
   encima la lista de versiones ya aprobadas del mismo perfil. Elegir una llena
   las cajas para que el barista vea qué va a usar antes de confirmar, y hay un
   solo lugar para confirmar, abajo:
   - una versión guardada sin tocar nada vuelve a regir tal cual, sin crear
     otra (vía «selección» de ADR-017 § 4.1). Es lo que pide el protocolo de
     catación al alternar el perfil y su variante: si cada vuelta creara una
     versión, los cafés quedarían en perfiles distintos y el razonador no los
     compararía entre sí;
   - si cambió alguna caja, se crea una versión nueva a partir de la versión
     elegida, se aprueba y se activa: el camino de Perfiles en un solo botón.
     Se parte de la elegida, y no de la que rige, para que lo guardado sea lo
     que el barista vio más su cambio, con las tolerancias de esa versión.
   Antes el selector iba aparte con su propio botón, y el barista tenía dos
   sitios para hacer lo mismo. */

// Una especificación cambiada conserva lo que el barista no tocó: la
// tolerancia, la unidad y, en el tiempo, el mínimo.
function updatedSpecifications(profile: PreparationProfile, recipe: Recipe): ProfileSpecification[] {
  const specs = (profile.specifications ?? []).map((spec) => ({ ...spec }))
  for (const field of FIELDS) {
    const raw = recipe[field.key]
    const spec = specs.find((item) => field.properties.includes(item.property))
    if (raw === '') continue
    const value = Number(raw)
    if (spec && field.edge === 'maximum') { spec.maximum = value; continue }
    if (spec && spec.strategy === 'target') { spec.target = value; continue }
    if (spec) { const half = ((spec.maximum ?? value) - (spec.minimum ?? value)) / 2; spec.minimum = value - half; spec.maximum = value + half; continue }
    specs.push(field.edge === 'maximum'
      ? { property: field.properties[0], unit: field.unit, strategy: 'range', minimum: 0, maximum: value }
      : { property: field.properties[0], unit: field.unit, strategy: 'target', target: value, tolerance: null })
  }
  return specs
}

const APPROVED = ['approved', 'accepted', 'effective']

// Lo que «Iniciar preparación» necesita del editor (26-oct-2026): si hay
// cambios sin usar y cómo usarlos. Antes el barista tenía que pulsar «Usar en
// la máquina» antes de iniciar, y si lo olvidaba el café salía con el perfil
// anterior aunque las cajas mostraran otro.
export interface RecipeController { pending: boolean; invalid: boolean; apply: () => Promise<void> }

export function RecipeEditor({ ruling, profiles, coffeeId, onDose, controller }: { ruling: PreparationProfile; profiles: PreparationProfile[]; coffeeId: string; onDose: (dose: string) => void; controller?: MutableRefObject<RecipeController | null> }) {
  const queryClient = useQueryClient()
  const family = ruling.familyId ?? ruling.id
  const versions = profiles
    .filter((item) => (item.familyId ?? item.id) === family && APPROVED.includes(item.status))
    .sort((a, b) => b.version - a.version)
  const [baseId, setBaseId] = useState(ruling.id)
  const base = versions.find((item) => item.id === baseId) ?? ruling
  const [recipe, setRecipe] = useState(() => recipeFrom(ruling))
  useEffect(() => { setBaseId(ruling.id); setRecipe(recipeFrom(ruling)) }, [ruling])
  useEffect(() => { onDose(recipe.dose) }, [recipe.dose, onDose])
  const baseRecipe = recipeFrom(base)
  const edited = FIELDS.some((field) => recipe[field.key] !== baseRecipe[field.key])
  const pending = edited || base.id !== ruling.id
  const water = Number(recipe.water)
  const waterError = recipe.water !== '' && (water < WATER_TEMPERATURE_RANGE_C.minimum || water > WATER_TEMPERATURE_RANGE_C.maximum)
  const choose = (id: string) => {
    const next = versions.find((item) => item.id === id) ?? ruling
    setBaseId(next.id)
    setRecipe(recipeFrom(next))
  }
  const undo = () => { setBaseId(ruling.id); setRecipe(recipeFrom(ruling)) }
  const save = useMutation({
    mutationFn: async () => {
      if (!edited) {
        await post(`/api/v1/profiles/${base.id}/activate`, { via: 'selection' })
        return
      }
      const coffeeIds = [...new Set([...(base.coffeeIds ?? []), ...(coffeeId ? [coffeeId] : [])])]
      const version = await post<PreparationProfile>(`/api/v1/profiles/${ruling.id}/versions`, {
        name: base.name,
        coffeeIds,
        specifications: updatedSpecifications(base, recipe),
        ...(base.purgeMaximumTimeMs != null ? { purgeMaximumTimeMs: base.purgeMaximumTimeMs } : {}),
      })
      await post(`/api/v1/profiles/${version.id}/approve`, {})
      await post(`/api/v1/profiles/${version.id}/activate`, { via: 'edition' })
    },
    onSuccess: async () => { await queryClient.invalidateQueries({ queryKey: ['profiles'] }) },
  })
  useEffect(() => {
    if (!controller) return
    controller.current = { pending, invalid: waterError, apply: () => save.mutateAsync() }
  })
  return <div className={styles.stack}>
    <p className={styles.formHint}>Ingresa los valores del perfil para esta preparación o elige uno guardado.</p>
    {versions.length > 1 ? <div className={styles.compactSelect}><Field label="Perfiles guardados"><Select value={base.id} onChange={(event) => choose(event.target.value)}>{versions.map((item) => <option key={item.id} value={item.id}>{item.id === ruling.id ? `En uso: ${describe(item)}` : describe(item)}</option>)}</Select></Field></div> : null}
    <div className={styles.compactFields}>
      {FIELDS.map((field) => <div key={field.key} className={styles.fieldNarrow}><Field label={`${field.label} (${field.shownUnit ?? field.unit})`}><NumberField step={field.step} placeholder={field.placeholder} min={field.key === 'water' ? String(WATER_TEMPERATURE_RANGE_C.minimum) : '0'} max={field.key === 'water' ? String(WATER_TEMPERATURE_RANGE_C.maximum) : undefined} value={recipe[field.key]} onChange={(event) => setRecipe((current) => ({ ...current, [field.key]: event.target.value }))}/></Field></div>)}
    </div>
    {waterError ? <p role="alert" className={styles.formError}>El agua debe estar entre {WATER_TEMPERATURE_RANGE_C.minimum} y {WATER_TEMPERATURE_RANGE_C.maximum} °C, que es lo que la máquina alcanza.</p> : null}
    {save.error ? <Alert variant="danger" title="No se pudo usar el perfil">{save.error.message}</Alert> : null}
    {pending ? <div className={styles.recipeActions}><Button variant="quiet" className={styles.recipeUndo} onPress={undo}>Deshacer cambios</Button><Button className={styles.recipeSave} onPress={() => save.mutate()} isDisabled={save.isPending || waterError}>{save.isPending ? 'Guardando…' : 'Usar en la máquina'}</Button></div> : null}
  </div>
}
