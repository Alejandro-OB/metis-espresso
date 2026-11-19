import { useMutation, useQuery } from '@tanstack/react-query'
import { Link, useParams } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { Episode, Extraction, Page, Recommendation } from '@/shared/api/types'
import { Alert, Breadcrumbs, LinkButton, LoadingState, Surface } from '@/shared/ui'
import { formatDateTime, formatNumber } from '@/shared/lib/format'
import { FlowStep, FlowSteps } from './flow-steps'
import { PageHeading } from './page-heading'
import { preparationMomentLabel } from './preparation-status'
import { recommendationOutcome } from './recommendation-outcome'
import { sensoryDefectLabel } from './sensory-options'
import styles from './routes.module.css'

/* Una recomendación contada en orden, como la página de un ajuste
   (25-oct-2026): el café que la motivó, qué propone y por qué, y qué decidió
   el barista. Solo consulta: se decide en la cata del café, donde la decisión
   lleva al ajuste. Antes había aquí otra decisión (Aceptar, Rechazar,
   Modificar con dirección, valor y unidad) que no registraba ningún ajuste,
   restos del modelo de aprendizaje que el sistema no tiene y datos técnicos
   (versión de la base, incertidumbre, códigos de evidencia). */

interface Explanation { summary: string; reasoning: string; limits: string[] }

const propertyLabels: Record<string, string> = { 'water-temperature': 'Temperatura del agua', 'extraction-time': 'Tiempo máximo', 'grind-setting': 'Molienda', 'coffee-dose': 'Dosis', 'brew-ratio': 'Ratio', pressure: 'Presión' }
const directionLabels: Record<string, string> = { increase: 'subir', decrease: 'bajar', maintain: 'mantener' }

function adjustmentText(adjustment: NonNullable<Recommendation['proposedAdjustment']>) {
  const value = adjustment.value != null ? ` a ${formatNumber(adjustment.value, Number.isInteger(adjustment.value) ? 0 : 1)} ${adjustment.unit ?? ''}` : ''
  return `${propertyLabels[adjustment.property] ?? adjustment.property}: ${directionLabels[adjustment.direction] ?? adjustment.direction}${value}`.trim()
}

export default function RecommendationDetailRoute() {
  const { id = '' } = useParams()
  const recommendation = useQuery({ queryKey: ['recommendation', id], queryFn: () => api<Recommendation>(`/api/v1/recommendations/${id}`) })
  const item = recommendation.data
  const extraction = useQuery({ queryKey: ['extraction', item?.extractionId], queryFn: () => api<Extraction>(`/api/v1/extractions/${item!.extractionId}`), enabled: Boolean(item?.extractionId) })
  const episodes = useQuery({ queryKey: ['episodes'], queryFn: () => api<Page<Episode>>('/api/v1/learning-episodes?page=1&page_size=100') })
  const explanation = useMutation({ mutationFn: () => post<Explanation>(`/api/v1/recommendations/${id}/explanation`, {}) })
  if (recommendation.isLoading) return <LoadingState/>
  if (recommendation.error || !item) return <Alert variant="danger" title="No se encontró la recomendación">{recommendation.error?.message ?? 'Sin datos'}</Alert>
  const coffee = extraction.data
  const episode = (episodes.data?.items ?? []).find((candidate) => candidate.adjustment.recommendationId === item.recommendationId)
  const abstained = item.status === 'abstained'
  const changed = item.decision?.modifiedAdjustment
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Recomendaciones', to: '/recommendations' }, { label: formatDateTime(item.createdAt) }]}/>
    <PageHeading title={abstained ? 'Nada que ajustar' : item.summary} detail={`Pedida el ${formatDateTime(item.createdAt)} · ${recommendationOutcome(item)}`}/>
    <FlowSteps>
      <FlowStep number={1} title="Café">
        {coffee ? <>
          <p><strong>{coffee.coffeeReference ?? 'Sin café registrado'}</strong></p>
          <p className={styles.meta}>{preparationMomentLabel(coffee)}</p>
          {coffee.sensoryEvaluation?.defects.length ? <p>Defectos: {coffee.sensoryEvaluation.defects.map(sensoryDefectLabel).join(', ')}</p> : null}
        </> : <p className={styles.muted}>{extraction.isLoading ? 'Cargando…' : 'No se encontró el café.'}</p>}
        <Link to={`/extractions/${item.extractionId}`}>Ver el café</Link>
      </FlowStep>
      <FlowStep number={2} title="Qué propone">
        {item.proposedAdjustment ? <p className={styles.episodeBig}>{adjustmentText(item.proposedAdjustment)}</p> : null}
        <p>{abstained ? item.summary : item.respondsTo.summary}</p>
        {item.heuristic ? <p className={styles.meta}>Por la regla «{item.heuristic.label}»: {item.heuristic.description} Fuente: {item.heuristic.source}.</p> : null}
        {item.diagnosis?.summary ? <p className={styles.meta}>Diagnóstico probable: {item.diagnosis.summary}</p> : null}
      </FlowStep>
      <FlowStep number={3} title="Qué se decidió">
        {abstained ? <p className={styles.muted}>No había nada que decidir.</p>
          : item.decision ? <>
            <p className={styles.episodeBig}>{recommendationOutcome(item)}</p>
            {changed ? <p>En su lugar: {adjustmentText(changed)}</p> : null}
            {item.decision.comment ? <p>{item.decision.comment}</p> : null}
            <p className={styles.meta}>{item.decision.decidedBy} · {formatDateTime(item.decision.decidedAt)}</p>
            {episode ? <Link to={`/episodes/${episode.id}`}>Ver el ajuste</Link> : null}
          </>
            : <><p className={styles.muted}>Todavía no se ha decidido.</p><LinkButton to={`/extractions/${item.extractionId}`}>Decidir en el café</LinkButton></>}
      </FlowStep>
    </FlowSteps>
    {item.alternatives?.length ? <Surface><h2 className={styles.surfaceTitle}>Otras pruebas posibles</h2><ul className={styles.list}>{item.alternatives.map((text) => <li key={text}>{text}</li>)}</ul></Surface> : null}
    {abstained ? null : <details className={styles.profileAdvanced} onToggle={(event) => { if ((event.target as HTMLDetailsElement).open && !explanation.data && !explanation.isPending) explanation.mutate() }}>
      <summary>Ver la explicación completa</summary>
      {explanation.isPending ? <LoadingState label="Preparando la explicación"/> : null}
      {explanation.error ? <Alert variant="danger" title="No se pudo preparar la explicación">{explanation.error.message}</Alert> : null}
      {explanation.data ? <div className={styles.stack}><strong>{explanation.data.summary}</strong><p>{explanation.data.reasoning}</p>{explanation.data.limits.length ? <ul className={styles.list}>{explanation.data.limits.map((limit) => <li key={limit}>{limit}</li>)}</ul> : null}</div> : null}
    </details>}
  </div>
}
