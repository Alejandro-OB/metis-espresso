import { useState } from 'react'
import type { Coffee, PreparationProfile, ProfileSpecification } from '@/shared/api/types'
import { Button, Checkbox, Field, Input, NumberField } from '@/shared/ui'
import { WATER_TEMPERATURE_RANGE_C } from './profile-options'
import { sensoryDraftFrom, sensoryPayload, type SensoryTargetPayload } from './sensory-draft'
import { SensoryTargetFields } from './sensory-profile-forms'
import styles from './routes.module.css'

/* El formulario de un perfil, para crearlo o para una versión nueva
   (23-oct-2026). Antes cada valor se agregaba a mano con «Añadir variable»,
   eligiéndolo entre catorce y diciendo si era objetivo o intervalo. Ahora la
   lista es fija: los valores que usa la catación, en el orden de Preparar
   café, ya llenos con los de la versión anterior. El tiempo es un intervalo
   porque su máximo es el corte de la máquina; los demás, objetivo y
   tolerancia. Un valor vacío no se pide.

   Las variables que la catación no usa (molienda, preinfusión, ambiente,
   Brix, rendimiento, estabilidad) ya no se ofrecen; si una versión las traía,
   pasan sin cambios a la nueva, para que nadie las pierda sin decidirlo.

   La masa en taza y el TDS tampoco se piden (24-oct-2026): el barista no
   puede fijarlos de antemano, porque salen de la taza. Los pone «Calcular
   tolerancia» con la media de las repeticiones medidas (`dispersion.py`,
   estado `added`), y una versión nueva los hereda por la misma vía que las
   demás variables no ofrecidas. */

export interface ProfilePayload {
  name: string
  coffeeIds: string[]
  specifications: ProfileSpecification[]
  purgeMaximumTimeMs?: number
}

// El purgado no se pide (23-oct-2026): el barista no lo ajusta y todos los
// perfiles llevan el mismo. Se manda el que ya traía la versión, o los 10 s
// de siempre en un perfil nuevo, para que la placa reciba un valor declarado
// y no el suyo por omisión.
const PURGE_DEFAULT_MS = 10_000

type Kind = 'target' | 'range'
interface FixedField { key: string; label: string; unit: string; shownUnit?: string; properties: string[]; kind: Kind }

// La presión la guardan Preparar café y la placa como `extraction-pressure`;
// un perfil viejo puede traerla como `pressure`, y se respeta la que traiga.
const FIELDS: FixedField[] = [
  { key: 'water', label: 'Temperatura del agua', unit: '°C', properties: ['water-temperature'], kind: 'target' },
  { key: 'pressure', label: 'Presión', unit: 'bar', properties: ['extraction-pressure', 'pressure'], kind: 'target' },
  { key: 'time', label: 'Tiempo', unit: 's', properties: ['extraction-time'], kind: 'range' },
  { key: 'dose', label: 'Dosis', unit: 'g', properties: ['coffee-dose'], kind: 'target' },
  { key: 'ratio', label: 'Ratio', unit: ':1', shownUnit: '1 : ?', properties: ['brew-ratio'], kind: 'target' },
]

interface Values { first: string; second: string }

const text = (value: number | null | undefined) => value == null ? '' : String(value)

// Lo que trae la versión, en la forma fija del campo. Un objetivo guardado
// como intervalo se lee por su centro y su media anchura, igual que lo juzga
// la placa; un tiempo guardado como objetivo, como el intervalo que cubre.
function valuesFor(field: FixedField, specification: ProfileSpecification | undefined): Values {
  if (!specification) return { first: '', second: '' }
  if (field.kind === 'range') {
    if (specification.strategy === 'range') return { first: text(specification.minimum), second: text(specification.maximum) }
    const half = specification.tolerance ?? 0
    return specification.target == null ? { first: '', second: '' } : { first: text(specification.target - half), second: text(specification.target + half) }
  }
  if (specification.strategy === 'target') return { first: text(specification.target), second: text(specification.tolerance) }
  if (specification.minimum == null || specification.maximum == null) return { first: '', second: '' }
  return { first: text((specification.minimum + specification.maximum) / 2), second: text((specification.maximum - specification.minimum) / 2) }
}

// `withSensory` añade el sabor buscado, que solo existe sobre un perfil ya
// creado (se guarda en su propia ruta del servidor): en «Crear perfil» no sale.
export function ProfileForm({ coffees, initial, pending, submitLabel = 'Guardar perfil', useLabel, withSensory = false, onSubmit, onSubmitAndUse }: { coffees: Coffee[]; initial?: PreparationProfile; pending: boolean; submitLabel?: string; useLabel?: string; withSensory?: boolean; onSubmit: (payload: ProfilePayload, target: SensoryTargetPayload | null) => void; onSubmitAndUse?: (payload: ProfilePayload, target: SensoryTargetPayload | null) => void }) {
  const specifications = initial?.specifications ?? []
  const [name, setName] = useState(initial?.name ?? '')
  // Con un solo café registrado no hay qué elegir: queda puesto y se nombra.
  const [coffeeIds, setCoffeeIds] = useState(() => new Set(initial?.coffeeIds?.length ? initial.coffeeIds : coffees.length === 1 ? [coffees[0].id] : []))
  const [values, setValues] = useState<Record<string, Values>>(() => Object.fromEntries(FIELDS.map((field) => [field.key, valuesFor(field, specifications.find((item) => field.properties.includes(item.property)))])))
  const [sensory, setSensory] = useState(() => sensoryDraftFrom(initial?.sensoryTarget))
  const [error, setError] = useState('')
  const update = (key: string, change: Partial<Values>) => setValues((current) => ({ ...current, [key]: { ...current[key], ...change } }))
  const toggleCoffee = (coffeeId: string) => setCoffeeIds((current) => {
    const next = new Set(current)
    if (next.has(coffeeId)) next.delete(coffeeId); else next.add(coffeeId)
    return next
  })

  const build = (): { payload: ProfilePayload; target: SensoryTargetPayload | null } | null => {
    if (!name.trim()) { setError('Escribe un nombre para el perfil.'); return null }
    if (!coffeeIds.size) { setError('Selecciona al menos un café.'); return null }
    const handled = new Set(FIELDS.flatMap((field) => field.properties))
    const normalized: ProfileSpecification[] = []
    for (const field of FIELDS) {
      const { first, second } = values[field.key]
      if (first === '' && second === '') continue
      const existing = specifications.find((item) => field.properties.includes(item.property))
      const property = existing?.property ?? field.properties[0]
      if (field.kind === 'range') {
        if (first === '' || second === '') { setError(`Completa el mínimo y el máximo de ${field.label.toLocaleLowerCase()}.`); return null }
        if (Number(first) > Number(second)) { setError(`El mínimo de ${field.label.toLocaleLowerCase()} no puede superar el máximo.`); return null }
        normalized.push({ property, unit: field.unit, strategy: 'range', minimum: Number(first), maximum: Number(second) })
        continue
      }
      if (first === '') { setError(`Indica el valor de ${field.label.toLocaleLowerCase()}.`); return null }
      if (second !== '' && Number(second) < 0) { setError('La tolerancia no puede ser negativa.'); return null }
      if (field.key === 'water' && (Number(first) < WATER_TEMPERATURE_RANGE_C.minimum || Number(first) > WATER_TEMPERATURE_RANGE_C.maximum)) {
        setError(`El agua debe estar entre ${WATER_TEMPERATURE_RANGE_C.minimum} y ${WATER_TEMPERATURE_RANGE_C.maximum} °C, que es lo que la máquina alcanza.`)
        return null
      }
      normalized.push({ property, unit: field.unit, strategy: 'target', target: Number(first), tolerance: second === '' ? null : Number(second) })
    }
    const kept = specifications.filter((item) => !handled.has(item.property))
    if (!normalized.length && !kept.length) { setError('Escribe al menos un valor.'); return null }
    const sensoryResult = withSensory ? sensoryPayload(sensory, initial?.sensoryTarget?.coffeeId && coffeeIds.has(initial.sensoryTarget.coffeeId) ? initial.sensoryTarget.coffeeId : [...coffeeIds][0]) : { target: null }
    if (sensoryResult.error) { setError(sensoryResult.error); return null }
    setError('')
    return { payload: { name: name.trim(), coffeeIds: [...coffeeIds], specifications: [...normalized, ...kept], purgeMaximumTimeMs: initial?.purgeMaximumTimeMs ?? PURGE_DEFAULT_MS }, target: sensoryResult.target }
  }

  const onlyCoffee = coffees.length === 1 ? coffees[0] : undefined
  const coffeeLabel = (coffee: Coffee) => coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || coffee.id
  return <form className={styles.profileForm} onSubmit={(event) => { event.preventDefault(); const built = build(); if (built) onSubmit(built.payload, built.target) }}>
    <div className={styles.formGrid}>
      <Field label="Nombre del perfil"><Input required value={name} onChange={(event) => setName(event.target.value)} /></Field>
      {onlyCoffee ? <Field label="Café"><Input value={coffeeLabel(onlyCoffee)} readOnly disabled /></Field> : null}
    </div>
    {onlyCoffee ? null : <fieldset className={styles.coffeeChoices}><legend>Cafés</legend><div>{coffees.map((coffee) => <Checkbox key={coffee.id} label={coffeeLabel(coffee)} checked={coffeeIds.has(coffee.id)} onChange={() => toggleCoffee(coffee.id)} />)}</div></fieldset>}
    {/* Un recuadro por variable, en filas que llenan el ancho, como en la
        vista del perfil (24-oct-2026). Una lista de una variable por línea
        dejaba media tarjeta vacía y seguía ocupando la pantalla. */}
    <div className={styles.profileValueEditor}>
      {FIELDS.map((field) => {
        const unit = field.shownUnit ?? field.unit
        const { first, second } = values[field.key]
        const range = field.kind === 'range'
        return <fieldset key={field.key} className={styles.profileValueTile}>
          <legend>{field.label} <small>({unit})</small></legend>
          <div>
            <label><span>{range ? 'Mínimo' : 'Valor'}</span><NumberField step="any" min="0" placeholder={field.key === 'ratio' ? 'ej. 2' : undefined} value={first} onChange={(event) => update(field.key, { first: event.target.value })}/></label>
            <label><span>{range ? 'Máximo' : 'Tolerancia (±)'}</span><NumberField step="any" min="0" value={second} onChange={(event) => update(field.key, { second: event.target.value })}/></label>
          </div>
        </fieldset>
      })}
    </div>
    {withSensory ? <section className={styles.profileSensory}><h2 className={styles.surfaceTitle}>Sabor buscado</h2><SensoryTargetFields value={sensory} onChange={setSensory}/></section> : null}
    {error ? <p className={styles.formError} role="alert">{error}</p> : null}
    <div className={styles.recipeActions}>
      <Button type="submit" variant={onSubmitAndUse ? 'secondary' : 'primary'} isDisabled={pending}>{pending ? 'Guardando…' : submitLabel}</Button>
      {onSubmitAndUse ? <Button isDisabled={pending} onPress={() => { const built = build(); if (built) onSubmitAndUse(built.payload, built.target) }}>{useLabel ?? 'Guardar y usar en la máquina'}</Button> : null}
    </div>
  </form>
}
