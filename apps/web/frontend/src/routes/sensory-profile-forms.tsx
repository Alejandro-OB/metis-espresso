import { useState } from 'react'
import type { Coffee } from '@/shared/api/types'
import { Button, Checkbox, Field, Input, TextArea } from '@/shared/ui'
import { sensoryDescriptorGroups, sensoryDescriptorValue, sensoryDescriptorValues } from './sensory-options'
import type { SensoryDraft } from './sensory-draft'
import styles from './routes.module.css'

export interface DeclaredProfilePayload {
  declaredBy: string
  source: string
  originalText: string
  descriptors: string[]
}


// Los descriptores como en la ficha CVA (23-oct-2026): cada familia en negrita
// con sus casillas al lado, igual que en la cata. Con `others`, además, un
// campo para escribir los que no están en la lista, como los dijo el barista.
export function DescriptorChoices({ selected, onChange, others, onOthersChange }: { selected: Set<string>; onChange: (value: Set<string>) => void; others?: string[]; onOthersChange?: (value: string[]) => void }) {
  const [name, setName] = useState('')
  const toggle = (descriptor: string) => {
    const next = new Set(selected)
    if (next.has(descriptor)) next.delete(descriptor); else next.add(descriptor)
    onChange(next)
  }
  const add = () => {
    const value = name.trim()
    if (!value || !onOthersChange || (others ?? []).some((item) => item.toLocaleLowerCase() === value.toLocaleLowerCase())) return
    onOthersChange([...(others ?? []), value])
    setName('')
  }
  return <fieldset className={styles.descriptorChoices}><legend>Descriptores</legend>
    <div className={styles.descriptorFamilies}>{sensoryDescriptorGroups.map((group) => <div key={group.label}><strong>{group.label}</strong><div>{group.options.map((option) => <Checkbox key={option.value} label={option.label} checked={selected.has(option.value)} onChange={() => toggle(option.value)}/>)}</div></div>)}</div>
    {onOthersChange ? <>
      {(others ?? []).length ? <div className={styles.choiceRow}>{(others ?? []).map((item) => <span key={item} className={styles.chipStatic}>{item} <Button variant="quiet" className={styles.inlineLink} aria-label={`Quitar ${item}`} onPress={() => onOthersChange((others ?? []).filter((other) => other !== item))}>quitar</Button></span>)}</div> : null}
      <div className={styles.otherDescriptor}><Field label="Otro descriptor" description="si no está en la lista"><Input maxLength={60} value={name} placeholder="ej. azúcar morena" onChange={(event) => setName(event.target.value)} onKeyDown={(event) => { if (event.key === 'Enter') { event.preventDefault(); add() } }}/></Field><Button variant="secondary" isDisabled={!name.trim() || (others ?? []).length >= 5} onPress={add}>Agregar</Button></div>
    </> : null}
  </fieldset>
}

export function DeclaredSensoryForm({ initial, pending, onSubmit }: { initial?: Coffee['declaredSensoryProfile']; pending: boolean; onSubmit: (payload: DeclaredProfilePayload) => void }) {
  const [declaredBy, setDeclaredBy] = useState(initial?.declaredBy ?? '')
  const [source, setSource] = useState(initial?.source ?? '')
  const [originalText, setOriginalText] = useState(initial?.originalText ?? '')
  const [descriptors, setDescriptors] = useState(new Set(initial?.descriptors.map(sensoryDescriptorValue).filter((value) => sensoryDescriptorValues.has(value)) ?? []))
  const [error, setError] = useState('')
  const submit = () => {
    if (!descriptors.size) return setError('Selecciona al menos un descriptor.')
    setError('')
    onSubmit({ declaredBy: declaredBy.trim(), source: source.trim(), originalText: originalText.trim(), descriptors: [...descriptors] })
  }
  return <form className={styles.profileForm} onSubmit={(event) => { event.preventDefault(); submit() }}>
    <Field label="Declarado por" description="Productor, tostador u otra fuente"><Input required value={declaredBy} onChange={(event) => setDeclaredBy(event.target.value)} /></Field>
    <Field label="Fuente"><Input required value={source} onChange={(event) => setSource(event.target.value)} placeholder="Etiqueta, ficha técnica…" /></Field>
    <Field label="Descripción original"><TextArea required value={originalText} onChange={(event) => setOriginalText(event.target.value)} /></Field>
    <DescriptorChoices selected={descriptors} onChange={setDescriptors}/>
    {error ? <p className={styles.formError} role="alert">{error}</p> : null}
    <div className={styles.actions}><Button type="submit" isDisabled={pending}>{pending ? 'Guardando…' : 'Guardar descripción'}</Button></div>
  </form>
}

// El sabor buscado se llena dentro del formulario del perfil y se guarda con
// sus mismos botones (23-oct-2026): antes tenía su propio botón, plegado
// aparte, y no se veía como parte del perfil aunque se guarda con la versión.
//
// Lleva descriptores y notas, sin cambios deseados: un «menos amargo» solo
// tiene sentido frente a una taza ya probada, y eso es la cata. Un objetivo
// viejo que los traiga los conserva sin mostrarlos.
export function SensoryTargetFields({ value, onChange }: { value: SensoryDraft; onChange: (value: SensoryDraft) => void }) {
  return <div className={styles.stack}>
    <DescriptorChoices selected={value.descriptors} onChange={(descriptors) => onChange({ ...value, descriptors })} others={value.others} onOthersChange={(others) => onChange({ ...value, others })}/>
    <Field label="Notas del barista"><TextArea value={value.rationale} onChange={(event) => onChange({ ...value, rationale: event.target.value })} /></Field>
  </div>
}
