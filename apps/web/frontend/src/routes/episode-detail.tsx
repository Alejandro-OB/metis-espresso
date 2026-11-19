import { useQueries, useQuery } from '@tanstack/react-query'
import { Link, useParams } from 'react-router-dom'
import { api } from '@/shared/api/client'
import type { Episode, Extraction } from '@/shared/api/types'
import { Alert, Breadcrumbs, LoadingState, Surface } from '@/shared/ui'
import { formatDateTime, formatNumber } from '@/shared/lib/format'
import { FlowStep, FlowSteps } from './flow-steps'
import { PageHeading } from './page-heading'
import { preparationMomentLabel } from './preparation-status'
import { sensoryDefectLabel } from './sensory-options'
import styles from './routes.module.css'

/* Un ajuste contado como lo vive el barista (24-oct-2026): el café de
   partida, el cambio y el café que lo comprobó, en ese orden y de izquierda
   a derecha, y debajo si funcionó. Antes eran cuatro tarjetas de datos
   sueltos («Registro anterior», «Variable de preparación», «0 variables
   comparadas», enlaces a la evidencia) y un formulario para evaluar que
   repetía el del café de comprobación, que es donde se responde ahora. */

const propertyLabels: Record<string, string> = { 'water-temperature': 'Temperatura del agua', 'extraction-time': 'Tiempo máximo', 'grind-setting': 'Molienda', 'coffee-dose': 'Dosis', 'brew-ratio': 'Ratio', pressure: 'Presión' }
// Las mismas respuestas que la pregunta «¿Funcionó el ajuste?» del café.
const outcomeLabels: Record<string, string> = { 'expected-observed': 'Sí', 'insufficient-observed': 'No alcanzó', 'adverse-observed': 'Empeoró', inconclusive: 'No se sabe' }
const acceptanceLabels: Record<string, string> = { accepted: 'Aceptada', rejected: 'Rechazada', inconclusive: 'No concluyente' }

function number(value: number) {
  return formatNumber(value, Number.isInteger(value) ? 0 : 1)
}

function targetText(episode: Episode) {
  const { kind, code } = episode.targetOutcome
  if (kind === 'sensory-defect') return sensoryDefectLabel(code)
  return propertyLabels[code] ?? code
}

export default function EpisodeDetailRoute() {
  const { id = '' } = useParams()
  const episode = useQuery({ queryKey: ['episode', id], queryFn: () => api<Episode>(`/api/v1/learning-episodes/${id}`) })
  const item = episode.data
  const [initial, followUp] = useQueries({ queries: [item?.initialExtractionId, item?.followUpExtractionId].map((extractionId) => ({
    queryKey: ['extraction', extractionId],
    queryFn: () => api<Extraction>(`/api/v1/extractions/${extractionId}`),
    enabled: Boolean(extractionId),
  })) })
  if (episode.isLoading) return <LoadingState/>
  if (episode.error || !item) return <Alert variant="danger" title="No se encontró el ajuste">{episode.error?.message ?? 'Sin datos'}</Alert>
  const property = propertyLabels[item.adjustment.property] ?? item.adjustment.property
  const change = `${number(item.adjustment.previousValue)} → ${number(item.adjustment.newValue)} ${item.adjustment.unit}`
  const when = item.adjustment.occurredAt ? formatDateTime(item.adjustment.occurredAt) : null
  const who = item.adjustment.performedBy ?? item.provenance?.actor
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Ajustes', to: '/episodes' }, { label: property }]}/>
    <PageHeading title={`${property}: ${change}`} detail={[when, who, item.adjustment.recommendationId ? 'desde una recomendación' : 'decidido por el barista'].filter(Boolean).join(' · ')}/>
    <FlowSteps>
      <CoffeeCard number={1} title="Café de partida" extraction={initial?.data} loading={initial?.isLoading ?? false}/>
      <FlowStep number={2} title="Ajuste">
        <p className={styles.episodeBig}>{property} {change}</p>
        <p>Para corregir: <strong>{targetText(item)}</strong></p>
        {item.adjustment.rationale ? <p className={styles.muted}>{item.adjustment.rationale}</p> : null}
        {item.adjustment.recommendationId ? <Link to={`/recommendations/${item.adjustment.recommendationId}`}>Ver la recomendación</Link> : null}
      </FlowStep>
      {item.followUpExtractionId
        ? <CoffeeCard number={3} title="Café de comprobación" extraction={followUp?.data} loading={followUp?.isLoading ?? false}/>
        : <FlowStep number={3} title="Café de comprobación" pending><p className={styles.muted}>Todavía no hay. En Preparar café, elige «De comprobación» en «Clase de preparación» del siguiente café.</p></FlowStep>}
    </FlowSteps>
    <Surface><h2 className={styles.surfaceTitle}>¿Funcionó el ajuste?</h2>
      {item.outcome
        ? <div className={styles.flowResult}><p className={styles.episodeBig}>{outcomeLabels[item.outcome.category] ?? item.outcome.category}</p>{item.outcome.rationale ? <p>{item.outcome.rationale}</p> : null}<span className={styles.meta}>{[item.outcome.assessedBy, formatDateTime(item.outcome.assessedAt)].filter(Boolean).join(' · ')}</span></div>
        : item.followUpExtractionId
          ? <p>Se responde al catar el café de comprobación. <Link to={`/extractions/${item.followUpExtractionId}`}>Ir a ese café</Link></p>
          : <p className={styles.muted}>Se sabrá después del café de comprobación.</p>}
    </Surface>
  </div>
}

function CoffeeCard({ number, title, extraction, loading }: { number: number; title: string; extraction: Extraction | undefined; loading: boolean }) {
  if (loading) return <FlowStep number={number} title={title}><LoadingState/></FlowStep>
  if (!extraction) return <FlowStep number={number} title={title}><p className={styles.muted}>No se encontró el café.</p></FlowStep>
  const sensory = extraction.sensoryEvaluation
  return <FlowStep number={number} title={title}>
    <p><strong>{extraction.coffeeReference ?? 'Sin café registrado'}</strong></p>
    <p className={styles.meta}>{preparationMomentLabel(extraction)}</p>
    <p>Cata: {sensory ? acceptanceLabels[sensory.acceptance.status] ?? sensory.acceptance.status : 'sin registrar'}{sensory?.defects.length ? ` · ${sensory.defects.map(sensoryDefectLabel).join(', ')}` : ''}</p>
    <Link to={`/extractions/${extraction.id}`}>Ver el café</Link>
  </FlowStep>
}
