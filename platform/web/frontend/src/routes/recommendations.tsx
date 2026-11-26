import { useQuery } from '@tanstack/react-query'
import { Link } from 'react-router-dom'
import { api } from '@/shared/api/client'
import type { Extraction, Page, Recommendation } from '@/shared/api/types'
import { Alert, DataTable, Surface, type Column } from '@/shared/ui'
import { PageHeading } from './page-heading'
import { preparationMomentLabel } from './preparation-status'
import { recommendationOutcome } from './recommendation-outcome'
import styles from './routes.module.css'

/* Las recomendaciones, solo para consultar (25-oct-2026). Se piden y se
   deciden en la cata del café que las motiva («¿Qué me recomiendas?»); aquí
   se ve qué propuso cada una y qué hizo el barista. Antes la lista mostraba
   códigos internos, un aviso fijo de «Análisis limitado» y un botón que pedía
   escribir a mano el código de la preparación. */


function CoffeeCell({ extractionId }: { extractionId: string }) {
  const extraction = useQuery({ queryKey: ['extraction', extractionId], queryFn: () => api<Extraction>(`/api/v1/extractions/${extractionId}`) })
  if (!extraction.data) return <Link to={`/extractions/${extractionId}`}>{extraction.isLoading ? 'Cargando…' : 'Ver el café'}</Link>
  return <Link to={`/extractions/${extractionId}`}>{extraction.data.coffeeReference ?? 'Sin café registrado'} · {preparationMomentLabel(extraction.data)}</Link>
}

export default function RecommendationsRoute() {
  const recommendations = useQuery({ queryKey: ['recommendations'], queryFn: () => api<Page<Recommendation>>('/api/v1/recommendations?page=1&page_size=100') })
  const items = [...(recommendations.data?.items ?? [])].sort((a, b) => b.createdAt.localeCompare(a.createdAt))
  const columns: Column<Recommendation>[] = [
    { id: 'coffee', header: 'Café', cell: (item) => <CoffeeCell extractionId={item.extractionId}/> },
    { id: 'summary', header: 'Qué propone', cell: (item) => <Link to={`/recommendations/${item.recommendationId}`}>{item.status === 'abstained' ? 'Nada que ajustar' : item.summary}</Link> },
    { id: 'outcome', header: 'Qué se decidió', cell: recommendationOutcome },
  ]
  return <div className={styles.page}>
    <PageHeading title="Recomendaciones"/>
    <Surface>{recommendations.error ? <Alert variant="danger" title="No se pudieron cargar las recomendaciones">{recommendations.error.message}</Alert> : <DataTable label="Recomendaciones pedidas" rows={items} columns={columns} getKey={(item) => item.recommendationId} loading={recommendations.isLoading}/>}</Surface>
  </div>
}
