import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { api } from '@/shared/api/client'
import type { LiveSample, SimulationSample, TwinChannelDifference, TwinComparison } from '@/shared/api/types'
import { Alert, Button, Surface } from '@/shared/ui'
import styles from './routes.module.css'

const VERDICTS: Record<string, { label: string; variant: 'success' | 'warning' | 'danger' | 'info' }> = {
  'within-model-tolerance': { label: 'El modelo reprodujo la preparación dentro de la tolerancia', variant: 'success' },
  'outside-model-tolerance': { label: 'El modelo se apartó de lo medido más allá de la tolerancia', variant: 'danger' },
  'insufficient-coverage': { label: 'No hay lecturas suficientes para juzgar el modelo', variant: 'warning' },
  'no-tolerance-declared': { label: 'Sin tolerancia declarada para este canal', variant: 'info' },
  'not-evaluable': { label: 'Ningún canal puede evaluarse', variant: 'warning' },
}

const CHANNEL_LABELS: Record<string, string> = { temperatureC: 'Temperatura', pressureBar: 'Presión' }
const LIMITATIONS: Record<string, string> = {
  'static-hydraulic-model': 'El modelo hidráulico no representa todavía la dinámica de la presión, así que esta diferencia no evalúa al modelo y no cuenta para el resultado.',
  'experimental-model-channel': 'El modelo publicado todavía no está validado para este canal, así que esta diferencia se muestra pero no cuenta para el resultado.',
  'tolerance-pending': 'El modelo ya está validado para este canal, pero falta declarar cuánta diferencia se admite; mientras tanto no cuenta para el resultado.',
}

function formato(value: number | null, unit: string, signo = false) {
  if (value === null || value === undefined) return '—'
  const texto = signo && value > 0 ? `+${value.toFixed(3)}` : value.toFixed(3)
  return `${texto} ${unit}`
}

/** Curvas medida y simulada superpuestas sobre el mismo eje de tiempo. */
function ComparisonChart({ measured, simulated, channel, unit }: {
  measured: LiveSample[]; simulated: SimulationSample[]; channel: 'temperatureC' | 'pressureBar'; unit: string
}) {
  const width = 720, height = 220, inset = 28
  const medidos = measured.filter((s) => s[channel] !== null)
  const simulados = simulated.map((s) => ({ ms: Math.round(s.simulationTimeSeconds * 1000), value: s.channels[channel] }))
  const valores = [...medidos.map((s) => s[channel] as number), ...simulados.map((s) => s.value)]
  if (!valores.length) return null
  const maxTime = Math.max(1000, ...medidos.map((s) => s.elapsedMs), ...simulados.map((s) => s.ms))
  const min = Math.min(...valores), max = Math.max(...valores)
  const span = max - min || 1
  const x = (ms: number) => inset + (ms / maxTime) * (width - inset * 2)
  const y = (v: number) => height - inset - ((v - min) / span) * (height - inset * 2)
  const medida = medidos.map((s) => `${x(s.elapsedMs)},${y(s[channel] as number)}`).join(' ')
  const simulada = simulados.map((s) => `${x(s.ms)},${y(s.value)}`).join(' ')
  const etiqueta = CHANNEL_LABELS[channel] ?? channel
  return <figure className={styles.liveChart}>
    <figcaption className={styles.liveChartHeader}>
      <div><h3>{etiqueta}</h3><p>Línea continua: lo medido · línea discontinua: lo simulado</p></div>
    </figcaption>
    <svg viewBox={`0 0 ${width} ${height}`} role="img"
      aria-label={`${etiqueta}: curva medida entre ${min.toFixed(2)} y ${max.toFixed(2)} ${unit} comparada con la simulada`}>
      <title>{etiqueta}: comparación entre la preparación y su réplica simulada.</title>
      <line x1={inset} y1={height - inset} x2={width - inset} y2={height - inset} className={styles.chartAxis}/>
      <polyline points={simulada} className={styles.chartLineSimulated}/>
      <polyline points={medida} className={styles.chartLine}/>
    </svg>
  </figure>
}

function ChannelRow({ item }: { item: TwinChannelDifference }) {
  const etiqueta = CHANNEL_LABELS[item.channel] ?? item.channel
  return <div className={styles.evidence}>
    <strong>{etiqueta}{item.contributesToVerdict ? '' : ' · no cuenta para el resultado'}</strong>
    <dl className={styles.definition}>
      <div className={styles.definitionRow}><dt>Diferencia media</dt><dd>{formato(item.meanSignedDifference, item.unit, true)}</dd></div>
      <div className={styles.definitionRow}><dt>Error cuadrático medio</dt><dd>{formato(item.rootMeanSquareDifference, item.unit)}</dd></div>
      <div className={styles.definitionRow}><dt>Diferencia máxima</dt><dd>{formato(item.maxAbsoluteDifference, item.unit)}{item.elapsedMsAtMaxDifference !== null ? ` a los ${(item.elapsedMsAtMaxDifference / 1000).toFixed(1)} s` : ''}</dd></div>
      <div className={styles.definitionRow}><dt>Tolerancia</dt><dd>{item.modelTolerance === null ? 'No declarada' : `${item.modelTolerance} ${item.unit}`}</dd></div>
      <div className={styles.definitionRow}><dt>Lecturas comparadas</dt><dd>{item.comparedSampleCount} · {(item.coverageRatio * 100).toFixed(0)} % de cobertura</dd></div>
    </dl>
    {item.modelLimitation ? <Alert variant="info" title="Límite del modelo">{LIMITATIONS[item.modelLimitation] ?? item.modelLimitation}</Alert> : null}
  </div>
}

export function TwinComparisonPanel({ extractionId, comparison }: { extractionId: string; comparison: TwinComparison | null }) {
  const queryClient = useQueryClient()
  const solicitar = useMutation({
    mutationFn: () => api<TwinComparison>(`/api/v1/extractions/${extractionId}/twin-comparison`, {
      method: 'POST', body: JSON.stringify({}),
      headers: { 'Idempotency-Key': `twin-${extractionId}-${Date.now()}` },
    }),
    onSuccess: () => { void queryClient.invalidateQueries({ queryKey: ['extraction-result', extractionId] }) },
  })
  const medida = useQuery({
    queryKey: ['extraction-series', extractionId],
    queryFn: () => api<{ samples: LiveSample[] }>(`/api/v1/extractions/${extractionId}/series`),
    enabled: Boolean(comparison?.channels),
  })
  const simulada = useQuery({
    queryKey: ['simulation-series', comparison?.simulationRunId],
    queryFn: () => api<{ samples: SimulationSample[] }>(`/api/v1/simulations/${comparison!.simulationRunId}/series`),
    enabled: Boolean(comparison?.simulationRunId && comparison?.channels),
  })

  if (!comparison) {
    return <Surface>
      <h2 className={styles.surfaceTitle}>Gemelo digital</h2>
      <p>Esta preparación todavía no se ha comparado con su réplica simulada. La comparación ejecuta el modelo partiendo de las condiciones que tenía la máquina y durante el mismo tiempo, y mide cuánto se separó de lo observado.</p>
      {solicitar.error ? <Alert variant="danger" title="No fue posible solicitar la comparación">{solicitar.error.message}</Alert> : null}
      <Button onClick={() => solicitar.mutate()} isDisabled={solicitar.isPending}>
        {solicitar.isPending ? 'Encolando…' : 'Comparar con el modelo'}
      </Button>
      {solicitar.isSuccess ? <Alert variant="info" title="Comparación en cola">La simulación se está ejecutando; el resultado aparecerá aquí al terminar.</Alert> : null}
    </Surface>
  }

  if (comparison.status === 'failed') {
    return <Surface>
      <h2 className={styles.surfaceTitle}>Gemelo digital</h2>
      <Alert variant="danger" title="La comparación no pudo completarse">{comparison.error?.message ?? 'Sin detalle disponible.'}</Alert>
    </Surface>
  }

  const veredicto = VERDICTS[comparison.verdict ?? 'not-evaluable'] ?? VERDICTS['not-evaluable']
  const alignment = comparison.alignment
  return <Surface>
    <h2 className={styles.surfaceTitle}>Gemelo digital</h2>
    <Alert variant={veredicto.variant} title="Diferencia entre lo medido y lo simulado">{veredicto.label}</Alert>
    {alignment ? <p className={styles.meta}>
      <span>{alignment.comparedSampleCount} de {alignment.measuredSampleCount} lecturas comparadas</span>
      <span>Cobertura {(alignment.coverageRatio * 100).toFixed(0)} % · mínima exigida {(alignment.minimumCoverageRatio * 100).toFixed(0)} %</span>
      {comparison.model ? <span>Modelo {comparison.model.id} {comparison.model.version}</span> : null}
    </p> : null}
    {comparison.channels?.map((item) => <ChannelRow key={item.channel} item={item}/>)}
    {medida.data && simulada.data ? <div className={styles.stack}>
      {comparison.channels?.map((item) => <ComparisonChart key={`chart-${item.channel}`}
        measured={medida.data.samples} simulated={simulada.data.samples} channel={item.channel} unit={item.unit}/>)}
    </div> : null}
    {alignment && alignment.excluded.invalidQuality > 0 ? <p className={styles.chartGap}>
      {alignment.excluded.invalidQuality} lecturas quedaron fuera por no estar disponibles; no se han rellenado.
    </p> : null}
  </Surface>
}
