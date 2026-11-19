import { useQuery } from '@tanstack/react-query'
import { Link, useParams } from 'react-router-dom'
import { api } from '@/shared/api/client'
import type { TraceabilityRecord } from '@/shared/api/types'
import { Alert, Breadcrumbs, LoadingState, Surface } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

const kindLabels: Record<string, string> = {
  barista: 'Barista', profile: 'Perfil', extraction: 'Preparación', observation: 'Observaciones',
  evaluation: 'Evaluación', diagnosis: 'Diagnóstico', explanation: 'Explicación',
  recommendation: 'Recomendación', decision: 'Decisión', adjustment: 'Ajuste', result: 'Resultado',
}

export default function TraceabilityRoute() {
  const { kind = '', id = '' } = useParams()
  const supported = ['barista', 'profile', 'extraction'].includes(kind)
  const record = useQuery({
    queryKey: ['traceability', kind, id],
    queryFn: () => api<TraceabilityRecord>(`/api/v1/traceability/${kind}/${id}`),
    enabled: supported && Boolean(id),
  })
  if (!supported) return <Alert variant="danger" title="Origen no válido">Abre la trazabilidad desde un barista, perfil o preparación.</Alert>
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Inicio', to: '/' }, { label: 'Trazabilidad' }]}/>
    <PageHeading title={record.data?.start.label ?? 'Trazabilidad'}/>
    {record.isLoading ? <LoadingState label="Reconstruyendo trazabilidad"/> : null}
    {record.error ? <Alert variant="danger" title="No se pudo reconstruir la trazabilidad">{record.error.message}</Alert> : null}
    {record.data ? <Surface><h2 className={styles.surfaceTitle}>Ruta de registros</h2>{record.data.nodes.length > 1 ? <ol className={styles.traceTimeline}>{record.data.nodes.map((node) => <li key={node.id}><Link to={node.detailUrl}><small>{kindLabels[node.kind] ?? node.kind}</small><strong>{node.label}</strong>{node.summary ? <span>{node.summary}</span> : null}{node.occurredAt ? <time dateTime={node.occurredAt}>{formatDateTime(node.occurredAt)}</time> : null}</Link></li>)}</ol> : <p className={styles.muted}>Aún no hay preparaciones relacionadas con este registro.</p>}</Surface> : null}
  </div>
}
