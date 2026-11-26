import { useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { Coffee, CoffeeOptionalField, Page } from '@/shared/api/types'
import { Alert, Button, Checkbox, DataTable, Field, Input, Modal, NumberField, Pagination, Surface, TableToolbar, type Column, useToast } from '@/shared/ui'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

export default function CoffeesRoute() {
  const [page, setPage] = useState(1)
  const [query, setQuery] = useState('')
  const [createOpen, setCreateOpen] = useState(false)
  const queryClient = useQueryClient()
  const toast = useToast()
  const coffees = useQuery({ queryKey: ['coffees', page, query], queryFn: () => api<Page<Coffee>>(`/api/v1/coffees?page=${page}&page_size=25&query=${encodeURIComponent(query)}`) })
  const create = useMutation({ mutationFn: (payload: Record<string, unknown>) => post<Coffee>('/api/v1/coffees', payload), onSuccess: async () => { await queryClient.invalidateQueries({ queryKey: ['coffees'] }); setCreateOpen(false); toast.push('Café registrado', 'Los campos ausentes se conservaron como ausentes.', 'success') } })
  const columns: Column<Coffee>[] = [
    { id: 'reference', header: 'Café', cell: (item) => <Link to={`/coffees/${item.id}`}>{coffeeName(item)}</Link>, sortable: true, sortValue: coffeeName },
    { id: 'variety', header: 'Variedad', cell: (item) => coffeeValue(item, 'variety') },
    { id: 'process', header: 'Proceso', cell: (item) => coffeeValue(item, 'postHarvestProcess') },
    { id: 'roast', header: 'Tueste', cell: (item) => coffeeValue(item, 'roastLevel'), mobile: false },
    { id: 'lot', header: 'Lote', cell: (item) => coffeeValue(item, 'lotIdentifier'), mobile: false },
    { id: 'region', header: 'Región', cell: (item) => coffeeValue(item, 'region') },
  ]
  return <div className={styles.page}>
    <PageHeading title="Cafés"/>
    <Surface><TableToolbar query={query} onQueryChange={(value) => { setQuery(value); setPage(1) }}><Button icon="plus" onPress={() => setCreateOpen(true)}>Registrar café</Button></TableToolbar>{coffees.error ? <Alert variant="danger" title="No fue posible cargar el catálogo">{coffees.error.message}</Alert> : <DataTable label="Catálogo de cafés" rows={coffees.data?.items ?? []} columns={columns} getKey={(item) => item.id} loading={coffees.isLoading}/>}<Pagination page={coffees.data?.page ?? page} pages={coffees.data?.pages ?? 1} onChange={setPage}/></Surface>
    <CoffeeModal isOpen={createOpen} onOpenChange={setCreateOpen} pending={create.isPending} error={create.error} onSubmit={(payload) => create.mutate(payload)}/>
  </div>
}

function coffeeName(coffee: Coffee) {
  return coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || 'Café sin nombre'
}

function coffeeValue(coffee: Coffee, field: CoffeeOptionalField) {
  if (coffee.notApplicableFields?.includes(field)) return 'No aplica'
  const value = coffee[field]
  return value === null || value === undefined || value === '' ? 'Sin información' : String(value)
}

const fieldNames: CoffeeOptionalField[] = [
  'commercialReference', 'lotIdentifier', 'bagIdentifier', 'variety', 'postHarvestProcess',
  'roastLevel', 'roastDate', 'region', 'altitudeM', 'soilCharacteristics',
  'mineralComposition', 'mineralSource',
]

type CoffeeValues = Record<CoffeeOptionalField, string>
const emptyValues = Object.fromEntries(fieldNames.map((field) => [field, ''])) as CoffeeValues

function valuesFrom(coffee?: Coffee | null): CoffeeValues {
  if (!coffee) return emptyValues
  return Object.fromEntries(fieldNames.map((field) => {
    const value = coffee[field]
    if (value === null || value === undefined) return [field, '']
    return [field, field === 'roastDate' ? String(value).slice(0, 10) : String(value)]
  })) as CoffeeValues
}

/* Registrar y corregir usan el mismo formulario. A la vista va lo que define el
   café para la catación; lote, bolsa, suelo y minerales quedan plegados como
   opcionales (pedido del usuario del 23-oct-2026): casi nunca se conocen y
   empujaban lo importante fuera de la pantalla. */
export function CoffeeModal({ isOpen, onOpenChange, pending, error, onSubmit, initial, title = 'Registrar café' }: { isOpen: boolean; onOpenChange: (open: boolean) => void; pending: boolean; error: Error | null; onSubmit: (payload: Record<string, unknown>) => void; initial?: Coffee | null; title?: string }) {
  const [values, setValues] = useState<CoffeeValues>(() => valuesFrom(initial))
  const [notApplicable, setNotApplicable] = useState<Set<CoffeeOptionalField>>(() => new Set(initial?.notApplicableFields ?? []))
  const setValue = (field: CoffeeOptionalField, value: string) => setValues((current) => ({ ...current, [field]: value }))
  const setFieldNotApplicable = (field: CoffeeOptionalField, checked: boolean) => {
    setNotApplicable((current) => {
      const next = new Set(current)
      if (checked) next.add(field); else next.delete(field)
      return next
    })
    if (checked) setValue(field, '')
  }
  const submit = () => onSubmit({
    commercialReference: valueOrNull(values.commercialReference),
    lotIdentifier: valueOrNull(values.lotIdentifier),
    bagIdentifier: valueOrNull(values.bagIdentifier),
    variety: valueOrNull(values.variety),
    postHarvestProcess: valueOrNull(values.postHarvestProcess),
    roastLevel: valueOrNull(values.roastLevel),
    roastDate: values.roastDate ? `${values.roastDate}T00:00:00Z` : null,
    region: valueOrNull(values.region),
    altitudeM: values.altitudeM ? Number(values.altitudeM) : null,
    soilCharacteristics: valueOrNull(values.soilCharacteristics),
    mineralComposition: valueOrNull(values.mineralComposition),
    mineralSource: valueOrNull(values.mineralSource),
    notApplicableFields: [...notApplicable],
    ...(initial ? {} : { declaredSensoryProfile: null }),
  })
  const field = (name: CoffeeOptionalField, label: string, type: 'text' | 'date' | 'number' = 'text') => <OptionalCoffeeField field={name} label={label} type={type} value={values[name]} notApplicable={notApplicable} onValue={setValue} onNotApplicable={setFieldNotApplicable}/>
  const folded: CoffeeOptionalField[] = ['lotIdentifier', 'bagIdentifier', 'soilCharacteristics', 'mineralComposition', 'mineralSource']
  return <Modal isOpen={isOpen} onOpenChange={onOpenChange} title={title}><form className={styles.formGrid} onSubmit={(event) => { event.preventDefault(); submit() }}>
    <p className={`${styles.formHint} ${styles.formFull}`}>Completa al menos la referencia comercial, el lote o la bolsa. Lo que dejes vacío queda como “Sin información”; marca “No aplica” solo si el dato no existe para este café.</p>
    {field('commercialReference', 'Referencia comercial')}
    {field('variety', 'Variedad')}
    {field('postHarvestProcess', 'Proceso poscosecha')}
    {field('roastLevel', 'Nivel de tueste')}
    {field('roastDate', 'Fecha de tueste', 'date')}
    {field('region', 'Región')}
    {field('altitudeM', 'Altitud (m s. n. m.)', 'number')}
    <details className={`${styles.sensoryDetails} ${styles.formFull}`} open={folded.some((name) => values[name] || notApplicable.has(name))}><summary>Datos opcionales</summary><div className={styles.formGrid}>
      {field('lotIdentifier', 'Lote')}
      {field('bagIdentifier', 'Bolsa')}
      {field('soilCharacteristics', 'Características del suelo')}
      {field('mineralComposition', 'Composición mineral')}
      {field('mineralSource', 'Fuente de la composición mineral')}
    </div></details>
    {error ? <div className={styles.formFull}><Alert variant="danger" title="No se pudo guardar">{error.message}</Alert></div> : null}<div className={`${styles.actions} ${styles.formFull}`}><Button variant="quiet" onPress={() => onOpenChange(false)}>Cancelar</Button><Button type="submit" isDisabled={pending}>{pending ? 'Guardando…' : 'Guardar café'}</Button></div>
  </form></Modal>
}

function valueOrNull(value: string) { return value.trim() || null }

interface OptionalCoffeeFieldProps {
  field: CoffeeOptionalField
  label: string
  value: string
  type?: 'text' | 'date' | 'number'
  notApplicable: Set<CoffeeOptionalField>
  onValue: (field: CoffeeOptionalField, value: string) => void
  onNotApplicable: (field: CoffeeOptionalField, checked: boolean) => void
}

function OptionalCoffeeField({ field, label, value, type = 'text', notApplicable, onValue, onNotApplicable }: OptionalCoffeeFieldProps) {
  const disabled = notApplicable.has(field)
  const id = `coffee-${field}`
  return <div className={styles.optionalCoffeeField}><Field label={label} id={id}>{type === 'number' ? <NumberField id={id} name={field} min="-500" max="6000" value={value} disabled={disabled} onChange={(event) => onValue(field, event.target.value)}/> : <Input id={id} name={field} type={type} value={value} disabled={disabled} onChange={(event) => onValue(field, event.target.value)}/>}</Field><Checkbox aria-label={`Marcar ${label} como no aplicable`} label="No aplica" checked={disabled} onChange={(event) => onNotApplicable(field, event.target.checked)}/></div>
}
