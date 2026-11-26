import { useMemo } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Link, useSearchParams } from 'react-router-dom'
import { post } from '@/shared/api/client'
import type { ProfileComparison, ProfileComparisonItem } from '@/shared/api/types'
import { Alert, Breadcrumbs, LoadingState, Surface } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import { profilePropertyLabel } from './profile-options'
import { sensoryAttributeLabel, sensoryDescriptorLabel, sensoryDirectionLabel } from './sensory-options'
import styles from './routes.module.css'

const statusLabels: Record<string, string> = { draft: 'Borrador', approved: 'Aprobado', retired: 'Retirado', proposed: 'Propuesto', accepted: 'Aprobado', effective: 'Vigente' }
const validationLabels: Record<string, string> = { 'validated-profile': 'Perfil validado', 'new-profile-version': 'Nueva versión', rejected: 'Puesta a punto rechazada' }

function sensoryObjective(profile: ProfileComparisonItem) {
  const target = profile.sensoryTarget
  if (!target) return 'Sin objetivo definido'
  const descriptors = target.descriptors.map(sensoryDescriptorLabel).join(' · ')
  const changes = target.desiredChanges.map((change) => `${sensoryDirectionLabel(change.direction)} ${sensoryAttributeLabel(change.attribute).toLocaleLowerCase('es')}`).join(' · ')
  return [descriptors, changes].filter(Boolean).join(' — ')
}

function displayValue(value: unknown, key: string, unit?: string | null) {
  if (value === null || value === undefined || value === '') return 'Ausente'
  if (Array.isArray(value)) {
    if (!value.length) return 'Ninguno'
    if (key === 'sensoryTarget.descriptors') return value.map((item) => sensoryDescriptorLabel(String(item))).join(' · ')
    if (key === 'sensoryTarget.desiredChanges') return value.map((item) => {
      const change = item as { attribute?: string; direction?: string }
      return `${sensoryDirectionLabel(change.direction ?? '')} ${sensoryAttributeLabel(change.attribute ?? '').toLocaleLowerCase('es')}`
    }).join(' · ')
    return value.join(' · ')
  }
  const text = typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean'
    ? String(value)
    : JSON.stringify(value)
  return `${text}${unit ? ` ${unit}` : ''}`
}

function changeLabel(key: string, fallback: string) {
  const [property, field] = key.split('.')
  if (!field || property === 'sensoryTarget') return fallback
  const labels: Record<string, string> = { target: 'Objetivo', tolerance: 'Tolerancia', minimum: 'Mínimo', maximum: 'Máximo' }
  return `${profilePropertyLabel(property)} · ${labels[field] ?? field}`
}

export default function ProfileComparisonRoute() {
  const [searchParams] = useSearchParams()
  const ids = useMemo(() => (searchParams.get('ids') ?? '').split(',').filter(Boolean).slice(0, 4), [searchParams])
  const comparison = useQuery({
    queryKey: ['profile-comparison', ids],
    queryFn: () => post<ProfileComparison>('/api/v1/profiles/compare', { ids }),
    enabled: ids.length >= 2,
  })
  if (ids.length < 2) return <Alert variant="info" title="Selecciona al menos dos perfiles">Vuelve a perfiles y marca entre dos y cuatro versiones.</Alert>
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Perfiles', to: '/profiles' }, { label: 'Comparación' }]}/>
    <PageHeading title={`${ids.length} perfiles`}/>
    {comparison.isLoading ? <LoadingState label="Preparando comparación"/> : null}
    {comparison.error ? <Alert variant="danger" title="No se pudieron comparar los perfiles">{comparison.error.message}</Alert> : null}
    {comparison.data ? <ProfileComparisonContent data={comparison.data}/> : null}
  </div>
}

function ProfileComparisonContent({ data }: { data: ProfileComparison }) {
  const contextRows = [
    { label: 'Autor', values: data.items.map((item) => item.author || 'Sin información') },
    { label: 'Estado', values: data.items.map((item) => statusLabels[item.status] ?? item.status) },
    { label: 'Cafés aplicables', values: data.items.map((item) => item.applicableCoffees.map((coffee) => coffee.name).join(' · ') || 'Ninguno') },
    { label: 'Objetivo sensorial', values: data.items.map(sensoryObjective) },
  ]
  return <>
    <Surface><h2 className={styles.surfaceTitle}>Resumen lado a lado</h2><div className={styles.comparisonTable} role="table" aria-label="Resumen de perfiles"><div role="row" className={styles.comparisonHeader}><span role="columnheader">Dato</span>{data.items.map((item) => <strong role="columnheader" key={item.id}><Link to={`/profiles/${item.id}`}>{item.name} · v{item.version}</Link></strong>)}</div>{contextRows.map((row) => <div role="row" className={styles.comparisonRow} key={row.label}><strong role="rowheader">{row.label}</strong>{row.values.map((value, index) => <span role="cell" key={`${row.label}-${data.items[index].id}`}>{value}</span>)}</div>)}</div></Surface>
    <Surface><div className={styles.sectionHeading}><div><h2 className={styles.surfaceTitle}>Parámetros y diferencias</h2><p>Los cambios entre versiones aparecen resaltados.</p></div></div><div className={styles.comparisonTable} role="table" aria-label="Parámetros comparados"><div role="row" className={styles.comparisonHeader}><span role="columnheader">Parámetro</span>{data.items.map((item) => <strong role="columnheader" key={item.id}>Versión {item.version}</strong>)}</div>{data.changes.map((change) => <div role="row" className={styles.comparisonRow} key={change.key}><strong role="rowheader">{changeLabel(change.key, change.label)}<small>{change.status === 'equal' ? 'Sin cambios' : change.status === 'missing' ? 'Información ausente' : 'Modificado'}</small></strong>{change.values.map((value, index) => <span role="cell" data-state={change.status} key={`${change.key}-${data.items[index].id}`}>{displayValue(value, change.key, change.unit)}</span>)}</div>)}</div></Surface>
    <Surface><h2 className={styles.surfaceTitle}>Validación y preparaciones relacionadas</h2><div className={styles.profileEvidenceGrid}>{data.items.map((item) => <section key={item.id}><h3>{item.name} · v{item.version}</h3><h4>Validación</h4>{item.validationEvidence.length ? <ul className={styles.linkList}>{item.validationEvidence.map((evidence) => <li key={evidence.id}><Link to={`/calibrations/${evidence.id}`}>{validationLabels[evidence.result ?? ''] ?? 'Puesta a punto'} · {evidence.testExtractionIds.length} {evidence.testExtractionIds.length === 1 ? 'preparación' : 'preparaciones'}</Link>{evidence.finishedAt ? <small>{formatDateTime(evidence.finishedAt)}</small> : null}{evidence.rationale ? <span>{evidence.rationale}</span> : null}</li>)}</ul> : <p className={styles.muted}>Sin validación registrada.</p>}<h4>Preparaciones que usaron esta versión</h4>{item.supportingExtractions.length ? <ul className={styles.linkList}>{item.supportingExtractions.map((extraction) => <li key={extraction.id}><Link to={`/extractions/${extraction.id}`}>{extraction.coffeeReference || extraction.id}</Link>{extraction.finishedAt ? <small>{formatDateTime(extraction.finishedAt)}</small> : null}</li>)}</ul> : <p className={styles.muted}>Aún no hay preparaciones registradas.</p>}</section>)}</div></Surface>
  </>
}
