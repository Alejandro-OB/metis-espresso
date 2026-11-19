import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { api } from '@/shared/api/client'
import type { Episode, Page } from '@/shared/api/types'
import { Alert, DataTable, Surface, type Column } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

const propertyLabels: Record<string, string> = { 'water-temperature': 'Temperatura del agua', 'extraction-time': 'Tiempo máximo', 'grind-setting': 'Molienda', 'coffee-dose': 'Dosis', 'brew-ratio': 'Ratio', pressure: 'Presión' }
const outcomeLabels: Record<string, string> = { 'expected-observed': 'Esperado', 'insufficient-observed': 'Insuficiente', 'adverse-observed': 'Adverso', inconclusive: 'Inconcluso' }

export default function EpisodesRoute() {
  const episodes = useQuery({ queryKey: ['episodes'], queryFn: () => api<Page<Episode>>('/api/v1/learning-episodes?page=1&page_size=100') })
  const columns: Column<Episode>[] = [
    { id: 'id', header: 'Ajuste', cell: (item) => <Link to={`/episodes/${item.id}`} className={styles.data}>{propertyLabels[item.adjustment.property] ?? item.adjustment.property}</Link> },
    { id: 'values', header: 'Cambio', cell: (item) => `${item.adjustment.previousValue} → ${item.adjustment.newValue} ${item.adjustment.unit}` },
    { id: 'responsible', header: 'Responsable', cell: (item) => item.adjustment.performedBy ?? item.provenance?.actor ?? 'Sin registrar' },
    { id: 'time', header: 'Fecha', cell: (item) => item.adjustment.occurredAt ? formatDateTime(item.adjustment.occurredAt) : 'Sin registrar', mobile: false },
    { id: 'outcome', header: 'Resultado', cell: (item) => item.outcome ? outcomeLabels[item.outcome.category] ?? item.outcome.category : item.followUpExtractionId ? 'Por evaluar' : 'Pendiente de comprobación' },
  ]
  return <div className={styles.page}>
    {/* Solo consulta (24-oct-2026): el ajuste se registra en la cata del
        café que lo motiva, que ya trae el café, los valores y la unidad. El
        formulario de aquí los pedía a mano, incluido el identificador. */}
    <PageHeading title="Ajustes"/>
    <Surface>{episodes.error ? <Alert variant="danger" title="No se pudieron cargar los ajustes">{episodes.error.message}</Alert> : <DataTable label="Ajustes registrados" rows={episodes.data?.items ?? []} columns={columns} getKey={(item) => item.id} loading={episodes.isLoading}/>}</Surface>
  </div>
}
