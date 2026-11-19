import { useState, type Key } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Link, useNavigate } from 'react-router-dom'
import { useAuth } from '@/app/auth-context'
import { api } from '@/shared/api/client'
import type { AdminUser, Coffee, Extraction, Page, PreparationProfile } from '@/shared/api/types'
import { Alert, Button, DataTable, Field, Input, Pagination, Select, Surface, type Column } from '@/shared/ui'
import { terminationLabel } from '@/shared/lib/format'
import { preparationMomentLabel } from './preparation-status'
import { downloadAttachment } from '@/shared/lib/download'
import { sensoryDefects } from './sensory-options'
import { coffeeName } from './preparation-context'
import { describe } from './recipe-values'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

type Filters = Record<string, string>

/* Los filtros que le sirven al barista. Variedad, proceso y tueste los da el
   café elegido; máquina y molino hay uno de cada uno; la puesta a punto es de
   una página oculta y la causa de finalización no la usa al revisar sus cafés.
   El servidor los sigue aceptando, por si se vuelven a necesitar. Quién
   preparó no es un filtro para el barista: el servidor le devuelve solo los
   suyos por la sesión (antes una casilla que se podía desmarcar). */
const initialFilters: Filters = {
  from: '', to: '', barista: '', coffeeId: '', profileId: '', technicalConformance: '', sensoryAcceptance: '', defect: '',
}
const conformityLabels: Record<string, string> = { conformant: 'Sí', 'non-conformant': 'No', 'insufficient-information': 'Sin datos' }
const acceptanceLabels: Record<string, string> = { accepted: 'Aceptada', rejected: 'Rechazada', inconclusive: 'Inconclusa', 'not-recorded': 'Sin evaluar' }

// Desde y Hasta abren en los últimos 7 días, hoy incluido (25-oct-2026):
// con solo el día de hoy, el historial salía vacío cualquier día sin café. La
// fecha sale del reloj del aparato con que se mira la página, no de la
// Raspberry: el teléfono o el computador la toman de la red. «Limpiar
// filtros» las deja vacías para ver todo.
const RANGE_DAYS = 7

function dayText(date: Date) {
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`
}

function lastDaysRange(): Pick<Filters, 'from' | 'to'> {
  const now = new Date()
  const start = new Date(now.getFullYear(), now.getMonth(), now.getDate() - (RANGE_DAYS - 1))
  return { from: `${dayText(start)}T00:00`, to: `${dayText(now)}T23:59` }
}

function instant(value: string) {
  return value ? new Date(value).toISOString() : ''
}

function profileName(item: Extraction) {
  return typeof item.profile.name === 'string' ? item.profile.name : item.profile.id
}

// Los filtros del historial como parámetros. El buscador de texto se quitó
// (23-oct-2026): pedía un identificador que el barista no conoce, y el café
// ya se elige de la lista. Los usan la lista y la exportación, que debe bajar
// lo mismo que se ve.
function historyParameters(filters: Filters) {
  const parameters = new URLSearchParams()
  Object.entries(filters).forEach(([name, value]) => { if (value) parameters.set(name, ['from', 'to'].includes(name) ? instant(value) : value) })
  return parameters
}

export default function ExtractionsRoute() {
  const navigate = useNavigate()
  const { user } = useAuth()
  const [page, setPage] = useState(1)
  const [filters, setFilters] = useState<Filters>(() => ({ ...initialFilters, ...lastDaysRange() }))
  const isAdmin = user?.role === 'admin'
  const coffees = useQuery({ queryKey: ['coffees', 'picker'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100') })
  const profiles = useQuery({ queryKey: ['profiles', 'prepare'], queryFn: () => api<Page<PreparationProfile>>('/api/v1/profiles?page=1&page_size=100') })
  const accounts = useQuery({ queryKey: ['admin-users'], queryFn: () => api<Page<AdminUser>>('/api/v1/admin/users?page=1&page_size=100'), enabled: isAdmin })
  // Las versiones con las que pudo hacerse un café: un borrador nunca rigió.
  const versions = (profiles.data?.items ?? []).filter((item) => item.status !== 'draft').sort((a, b) => b.version - a.version)
  const families = new Set(versions.map((item) => item.familyId ?? item.id))
  const versionLabel = (item: PreparationProfile) => families.size > 1 ? `${item.name} · ${describe(item)}` : describe(item)
  const [selected, setSelected] = useState<Set<Key>>(new Set())
  const updateFilter = (name: string, value: string) => { setFilters((current) => ({ ...current, [name]: value })); setPage(1) }
  const history = useQuery({
    queryKey: ['extractions', page, filters],
    queryFn: () => {
      const parameters = historyParameters(filters)
      parameters.set('page', String(page))
      parameters.set('page_size', '25')
      return api<Page<Extraction>>(`/api/v1/extractions?${parameters}`)
    },
  })
  const items = history.data?.items ?? []
  const columns: Column<Extraction>[] = [
    { id: 'id', header: 'Preparación', cell: (item) => <Link to={`/extractions/${item.id}`} className={styles.data}>{item.id}</Link>, sortable: true, sortValue: (item) => item.id },
    { id: 'coffee', header: 'Café', cell: (item) => item.coffeeReference ?? 'Sin café', sortable: true, sortValue: (item) => item.coffeeReference ?? '' },
    { id: 'finished', header: 'Fecha y hora', cell: (item) => preparationMomentLabel(item), sortable: true, sortValue: (item) => item.finishedAt ?? item.ingestedAt ?? '' },
    { id: 'profile', header: 'Perfil', cell: (item) => `${profileName(item)} · v${item.profile.version}`, mobile: false },
    { id: 'technical', header: 'Técnica', cell: (item) => conformityLabels[item.history?.technicalConformance ?? ''] ?? 'Sin evaluar', mobile: false },
    { id: 'sensory', header: 'Sensorial', cell: (item) => acceptanceLabels[item.history?.sensoryAcceptance ?? ''] ?? 'Sin evaluar', mobile: false },
    { id: 'termination', header: 'Finalización', cell: (item) => terminationLabel(item.termination), mobile: false },
  ]
  return <div className={styles.page}>
    <PageHeading title="Preparaciones" />
    <Surface>
      <div className={styles.historyToolbar}>
        <div className={styles.historyPrimaryFilters}>
          <Field label="Desde"><Input type="datetime-local" value={filters.from} onChange={(event) => updateFilter('from', event.target.value)}/></Field>
          <Field label="Hasta"><Input type="datetime-local" value={filters.to} onChange={(event) => updateFilter('to', event.target.value)}/></Field>
          <Field label="Café"><Select value={filters.coffeeId} onChange={(event) => updateFilter('coffeeId', event.target.value)}><option value="">Todos</option>{(coffees.data?.items ?? []).map((item) => <option key={item.id} value={item.id}>{coffeeName(item)}</option>)}</Select></Field>
          {isAdmin ? <Field label="Barista"><Select value={filters.barista} onChange={(event) => updateFilter('barista', event.target.value)}><option value="">Todos</option>{(accounts.data?.items ?? []).map((item) => <option key={item.id} value={item.id}>{item.displayName}</option>)}</Select></Field> : null}
        </div>
        <details className={styles.historyAdvanced}><summary>Más filtros</summary><div className={styles.historyFilterGrid}>
          <Field label="Perfil"><Select value={filters.profileId} onChange={(event) => updateFilter('profileId', event.target.value)}><option value="">Todos</option>{versions.map((item) => <option key={item.id} value={item.id}>{versionLabel(item)}</option>)}</Select></Field>
          <Field label="¿Cumplió el perfil?"><Select value={filters.technicalConformance} onChange={(event) => updateFilter('technicalConformance', event.target.value)}><option value="">Todos</option>{Object.entries(conformityLabels).map(([value, label]) => <option key={value} value={value}>{label}</option>)}</Select></Field>
          <Field label="Aceptación en la cata"><Select value={filters.sensoryAcceptance} onChange={(event) => updateFilter('sensoryAcceptance', event.target.value)}><option value="">Todas</option>{Object.entries(acceptanceLabels).map(([value, label]) => <option key={value} value={value}>{label}</option>)}</Select></Field>
          <Field label="Defecto"><Select value={filters.defect} onChange={(event) => updateFilter('defect', event.target.value)}><option value="">Todos</option>{sensoryDefects.map((item) => <option key={item.value} value={item.value}>{item.label}</option>)}</Select></Field>
        </div></details>
        <div className={styles.historyActions}><Button variant="quiet" onPress={() => { setFilters(initialFilters); setPage(1) }}>Limpiar filtros</Button><Button variant="secondary" icon="compare" isDisabled={selected.size < 2 || selected.size > 4} onPress={() => void navigate(`/compare?ids=${[...selected].map(String).join(',')}`)}>Comparar ({selected.size})</Button><Button variant="secondary" icon="download" onPress={() => { const parameters = historyParameters(filters); parameters.set('format', 'json'); downloadAttachment(`/api/v1/extractions/export?${parameters}`) }}>Exportar</Button></div>
      </div>
      {history.error ? <Alert variant="danger" title="No se pudo cargar el historial">{history.error.message}</Alert> : <DataTable label="Historial de extracciones" rows={items} columns={columns} getKey={(item) => item.id} loading={history.isLoading} selectedKeys={selected} onSelectionChange={setSelected} emptyTitle="No hay coincidencias" emptyMessage="Cambia o limpia los filtros para ver otros resultados." />}
      <Pagination page={history.data?.page ?? page} pages={history.data?.pages ?? 1} onChange={setPage}/>
    </Surface>
  </div>
}
