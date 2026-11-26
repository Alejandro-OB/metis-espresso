import { useMemo } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { Link, useParams } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { SimulationMetric, SimulationRun, SimulationSemantic, SimulationSeries } from '@/shared/api/types'
import { Alert, Button, Progress, ProvenanceBadge, Surface } from '@/shared/ui'
import { formatNumber } from '@/shared/lib/format'
import { tickDigits, ticks } from './chart-scale'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

const statusLabels: Record<string, string> = { queued: 'En cola', running: 'En ejecución', succeeded: 'Terminada', failed: 'Fallida', cancelled: 'Cancelada', pending: 'Pendiente de publicar', validating: 'Validando RDF', published: 'Publicada en EKO', rejected: 'Rechazada por SHACL' }

const CHART = { width: 760, height: 240, padLeft: 58, padRight: 18, padTop: 16, padBottom: 34 }

// Desde la FMU v2 el canal `temperatureC` es el metal de la caldera, como en la
// placa; antes el registro lo declaraba agua. El nombre sale del registro.
function temperatureLabel(channels: Array<{ id: string; quantity: string }> | undefined) {
  const quantity = channels?.find((channel) => channel.id === 'temperatureC')?.quantity
  return quantity === 'boiler-body-temperature' ? 'Temperatura del metal de la caldera' : 'Temperatura del agua'
}

function SimulationChart({ series, channel, metric, label }: { series: SimulationSeries; channel: 'temperatureC' | 'pressureBar'; metric: SimulationMetric; label: string }) {
  const { plotLeft, plotRight, plotTop, plotBottom, low, high, points } = useMemo(() => {
    const samples = series.samples
    const stride = Math.max(1, Math.ceil(samples.length / 320))
    const selected = samples.filter((_, index) => index % stride === 0 || index === samples.length - 1)
    const values = selected.map((sample) => sample.channels[channel])
    /* La escala sigue incluyendo la banda de tolerancia aunque la curva no la
       alcance: si el eje se ajustara solo a los valores simulados, una curva
       lejos del objetivo se vería centrada y daría la impresión contraria.
       Sin tolerancia declarada no hay banda y la escala incluye solo el
       objetivo: una tolerancia puesta para dibujar afirmaría algo que no consta. */
    const band = metric.tolerance ?? 0
    const low = Math.min(...values, metric.target - band)
    const high = Math.max(...values, metric.target + band)
    const range = Math.max(0.001, high - low)
    const plotLeft = CHART.padLeft
    const plotRight = CHART.width - CHART.padRight
    const plotTop = CHART.padTop
    const plotBottom = CHART.height - CHART.padBottom
    const points = selected
      .map((sample, index) => `${plotLeft + index * (plotRight - plotLeft) / Math.max(1, selected.length - 1)},${plotBottom - (sample.channels[channel] - low) * (plotBottom - plotTop) / range}`)
      .join(' ')
    return { plotLeft, plotRight, plotTop, plotBottom, low, high, points }
  }, [channel, metric.target, metric.tolerance, series.samples])

  const atValue = (value: number) => plotBottom - (value - low) / Math.max(0.001, high - low) * (plotBottom - plotTop)
  const marcasY = ticks(low, high)
  const digitosY = tickDigits(low, high)

  return <section className={styles.simulationChart}>
    <header><div><span>{label}</span><strong>{metric.final.toLocaleString('es-CO', { maximumFractionDigits: 2 })} <small>{metric.unit}</small></strong></div><p>{metric.tolerance === null ? `Objetivo ${metric.target} ${metric.unit} · sin tolerancia declarada` : `Objetivo ${metric.target} ± ${metric.tolerance} ${metric.unit}`}</p></header>
    <svg viewBox={`0 0 ${CHART.width} ${CHART.height}`} role="img" aria-label={`Curva simulada de ${label}`}>
      {marcasY.map((valor) => <g key={`y-${valor}`}>
        <line x1={plotLeft} y1={atValue(valor)} x2={plotRight} y2={atValue(valor)} className={styles.chartGrid}/>
        <text x={plotLeft - 10} y={atValue(valor)} className={`${styles.chartTick} ${styles.chartTickY}`}>{formatNumber(valor, digitosY)}</text>
      </g>)}
      {metric.tolerance === null ? null : <><rect x={plotLeft} y={atValue(metric.target + metric.tolerance)} width={plotRight - plotLeft} height={Math.max(1, atValue(metric.target - metric.tolerance) - atValue(metric.target + metric.tolerance))} className={styles.targetBand}/>
      <text x={plotRight - 6} y={atValue(metric.target + metric.tolerance) - 5} className={styles.chartBandLabel} textAnchor="end">objetivo</text></>}
      <line x1={plotLeft} y1={plotBottom} x2={plotRight} y2={plotBottom} className={styles.chartAxis}/>
      <line x1={plotLeft} y1={plotTop} x2={plotLeft} y2={plotBottom} className={styles.chartAxis}/>
      <polyline points={points} className={styles.chartLine}/>
      <text x={plotLeft - 10} y={plotTop - 4} className={`${styles.chartTick} ${styles.chartTickY}`}>{metric.unit}</text>
    </svg>
    <dl><div><dt>MAE</dt><dd>{metric.meanAbsoluteError.toFixed(2)} {metric.unit}</dd></div><div><dt>En tolerancia</dt><dd>{metric.withinTolerancePercent === null ? 'Sin tolerancia definida' : `${metric.withinTolerancePercent.toFixed(1)}%`}</dd></div><div><dt>Rango</dt><dd>{metric.minimum.toFixed(1)}–{metric.maximum.toFixed(1)}</dd></div></dl>
  </section>
}

export default function SimulationDetailRoute() {
  const { id = '' } = useParams()
  const queryClient = useQueryClient()
  const run = useQuery({ queryKey: ['simulation', id], queryFn: () => api<SimulationRun>(`/api/v1/simulations/${id}`), refetchInterval: (query) => { const data = query.state.data; return data && (data.status === 'queued' || data.status === 'running' || data.semanticStatus === 'pending' || data.semanticStatus === 'validating') ? 1200 : false } })
  const completed = run.data?.status === 'succeeded'
  const series = useQuery({ queryKey: ['simulation-series', id], queryFn: () => api<SimulationSeries>(`/api/v1/simulations/${id}/series`), enabled: completed })
  const semantic = useQuery({ queryKey: ['simulation-semantic', id], queryFn: () => api<SimulationSemantic>(`/api/v1/simulations/${id}/semantic`), enabled: completed, refetchInterval: (query) => query.state.data?.status === 'pending' || query.state.data?.status === 'validating' ? 1500 : false })
  const cancel = useMutation({ mutationFn: () => post<SimulationRun>(`/api/v1/simulations/${id}/cancel`, {}), onSuccess: async () => { await queryClient.invalidateQueries({ queryKey: ['simulation', id] }) } })
  if (run.error) return <div className={styles.page}><Alert variant="danger" title="No fue posible abrir la simulación">{run.error.message}</Alert></div>
  const item = run.data
  return <div className={styles.page}>
    <nav className={styles.backLink}><Link to="/simulations">← Volver a simulaciones</Link></nav>
    <div className={styles.simulationDetailHeading}><PageHeading title={item?.profileSnapshot.profileId ?? 'Cargando simulación'} detail={item ? `${item.model.id} · versión ${item.model.version} · ${item.durationSeconds} segundos simulados` : 'Recuperando el contrato y sus resultados.'}/><ProvenanceBadge source="simulation"/></div>
    {item ? <><Surface className={styles.simulationStatusPanel}><div className={styles.simulationStatusMain}><span>ESTADO DEL TRABAJO</span><strong>{statusLabels[item.status] ?? item.status}</strong><p>{item.error?.message ?? `Creada ${new Date(item.createdAt).toLocaleString('es-CO')}`}</p></div><div><span>{item.progress.percent.toFixed(0)}%</span><Progress value={item.progress.percent} label="Progreso de la simulación"/></div>{item.status === 'queued' || item.status === 'running' ? <Button variant="danger" isDisabled={cancel.isPending || item.cancelRequested} onPress={() => cancel.mutate()}>{item.cancelRequested ? 'Cancelación solicitada' : 'Cancelar'}</Button> : null}</Surface>
      {item.error ? <Alert variant="danger" title={item.error.code}>{item.error.message}</Alert> : null}
      {completed && item.summary && series.data ? <section className={styles.simulationCharts}><SimulationChart series={series.data} channel="temperatureC" metric={item.summary.metrics.temperatureC} label={temperatureLabel(item.model.channels)}/><SimulationChart series={series.data} channel="pressureBar" metric={item.summary.metrics.pressureBar} label="Presión de extracción"/></section> : null}
      <div className={styles.simulationEvidenceGrid}><Surface><span className={styles.simulationStamp}>CONTRATO CONGELADO</span><h2>Procedencia de ejecución</h2><dl className={styles.definition}><div className={styles.definitionRow}><dt>Identificador</dt><dd><code>{item.simulationRunId}</code></dd></div><div className={styles.definitionRow}><dt>FMU</dt><dd>{item.model.id}@{item.model.version}</dd></div><div className={styles.definitionRow}><dt>SHA-256</dt><dd><code>{item.model.sha256}</code></dd></div><div className={styles.definitionRow}><dt>Perfil</dt><dd>{item.profileSnapshot.profileId} · v{item.profileSnapshot.profileVersion}</dd></div><div className={styles.definitionRow}><dt>Controlador</dt><dd>{item.controller.id}@{item.controller.version}</dd></div><div className={styles.definitionRow}><dt>Muestras</dt><dd>{item.artifact?.sampleCount ?? 'Pendiente'}</dd></div></dl></Surface><Surface><span className={styles.simulationStamp}>GRAFO CANÓNICO</span><h2>Evidencia semántica</h2><p className={styles.semanticState}>{statusLabels[semantic.data?.status ?? item.semanticStatus] ?? item.semanticStatus}</p>{semantic.data?.publication ? <dl className={styles.definition}><div className={styles.definitionRow}><dt>Validación</dt><dd>{semantic.data.publication.validation.conforms ? 'Conforme con SHACL' : 'No conforme'}</dd></div><div className={styles.definitionRow}><dt>Formas</dt><dd>{semantic.data.publication.validation.shapesVersion}</dd></div><div className={styles.definitionRow}><dt>Grafo</dt><dd><code>{semantic.data.publication.namedGraphIri}</code></dd></div></dl> : <p className={styles.muted}>La publicación comenzará cuando termine la simulación.</p>}</Surface></div>
    </> : null}
  </div>
}
