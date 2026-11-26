import { useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { api } from '@/shared/api/client'
import { Alert, Field, LoadingState, NumberField, Surface } from '@/shared/ui'
import { formatNumber } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

interface Estimate {
  status: 'estimated' | 'no-data'
  reason?: string
  massG?: number
  cutSeconds?: number
  uncertaintyG?: number
  source: string
  scope: { doseG: number; pressureBar: number; cutSeconds: [number, number]; grindClicks: [number, number]; basket: string }
}
interface Guide {
  facts: Array<{ id: string; status: 'measured'; summary: string; source: string }>
  cupMass: Estimate | null
  timeForRatio: Estimate | null
}

// Cada dato dice si se midió o se estimó y de dónde sale; fuera de lo medido
// el servidor responde «no hay datos» y aquí se muestra tal cual, sin rellenar.
export default function BaristaGuideRoute() {
  const [cut, setCut] = useState('20')
  const [ratio, setRatio] = useState('2')
  const [dose, setDose] = useState('13')
  const query = new URLSearchParams({ doseG: dose || '13' })
  if (cut) query.set('cutSeconds', cut)
  if (ratio) query.set('ratio', ratio)
  const guide = useQuery({ queryKey: ['barista-guide', cut, ratio, dose], queryFn: () => api<Guide>(`/api/v1/barista-guide?${query.toString()}`) })
  return <div className={styles.page}>
    <PageHeading title="Guía para el barista"/>
    {guide.isLoading ? <LoadingState/> : guide.error ? <Alert variant="danger" title="No se pudo cargar la guía">{guide.error.message}</Alert> : guide.data ? <>
      <Surface>
        <h2 className={styles.surfaceTitle}>La máquina (medido)</h2>
        <ul className={styles.list}>{guide.data.facts.map((fact) => <li key={fact.id}>{fact.summary}</li>)}</ul>
        <p className={styles.meta}>Origen: {guide.data.facts[0]?.source}</p>
      </Surface>
      <Surface>
        <h2 className={styles.surfaceTitle}>Masa en taza y tiempo (estimado)</h2>
        <div className={styles.formGrid}>
          <Field label="Dosis de café (g)"><NumberField min="1" step="0.1" value={dose} onChange={(event) => setDose(event.target.value)}/></Field>
          <Field label="Tiempo de corte (s)"><NumberField min="1" step="0.5" value={cut} onChange={(event) => setCut(event.target.value)}/></Field>
          <Field label="Ratio taza/café"><NumberField min="0.5" step="0.1" value={ratio} onChange={(event) => setRatio(event.target.value)}/></Field>
        </div>
        <EstimateLine estimate={guide.data.cupMass} text={(item) => `Con corte a los ${cut} s salen unos ${formatNumber(item.massG ?? 0, 1)} g ± ${formatNumber(item.uncertaintyG ?? 0, 0)} g.`}/>
        <EstimateLine estimate={guide.data.timeForRatio} text={(item) => `Para un ratio de ${ratio} (${formatNumber(item.massG ?? 0, 1)} g) corte hacia los ${formatNumber(item.cutSeconds ?? 0, 1)} s, con ± ${formatNumber(item.uncertaintyG ?? 0, 0)} g de margen en la masa.`}/>
        {guide.data.cupMass ? <p className={styles.meta}>Vale para {guide.data.cupMass.scope.doseG} g de café, {guide.data.cupMass.scope.pressureBar} bar, molienda de {guide.data.cupMass.scope.grindClicks[0]} a {guide.data.cupMass.scope.grindClicks[1]} clics, canastilla {guide.data.cupMass.scope.basket} y cortes de {guide.data.cupMass.scope.cutSeconds[0]} a {guide.data.cupMass.scope.cutSeconds[1]} s. Origen: {guide.data.cupMass.source}</p> : null}
      </Surface>
    </> : null}
  </div>
}

function EstimateLine({ estimate, text }: { estimate: Estimate | null; text: (estimate: Estimate) => string }) {
  if (!estimate) return null
  return estimate.status === 'estimated' ? <p>{text(estimate)}</p> : <p className={styles.muted}>No hay datos: {estimate.reason}.</p>
}
