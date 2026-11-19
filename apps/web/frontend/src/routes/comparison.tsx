import { useMemo, useState } from 'react'
import { useQuery } from '@tanstack/react-query'
import { Link, useSearchParams } from 'react-router-dom'
import { post } from '@/shared/api/client'
import type { Extraction, LiveSample } from '@/shared/api/types'
import { Alert, Breadcrumbs, Button, Checkbox, LoadingState, Surface } from '@/shared/ui'
import { formatNumber } from '@/shared/lib/format'
import { tickDigits, ticks } from './chart-scale'
import { sensoryAttributeLabel, sensoryDefectLabel, sensoryDescriptorLabel } from './sensory-options'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

const dimensionOptions = [
  ['profile', 'Perfil'], ['coffee', 'Café'], ['equipment', 'Equipo'], ['barista', 'Barista'], ['environment', 'Ambiente'], ['preparation', 'Preparación'], ['sensory', 'Evaluación sensorial'], ['curves', 'Curvas'], ['adjustments', 'Ajustes'],
] as const
type Dimension = (typeof dimensionOptions)[number][0]
interface ComparisonMetric { dimension: Dimension; key: string; label: string; unit: string; values: Array<number | null>; comparisons: Array<{ absolute: number | null; relativePercent: number | null; status: 'equal' | 'different' | 'missing' }> }
interface ComparisonResponse { items: Extraction[]; dimensions: Dimension[]; metrics: ComparisonMetric[]; series: Array<{ extractionId: string; samples: LiveSample[] }>; adjustmentSequence: Array<{ id: string; property: string; previousValue: number; newValue: number; unit: string; occurredAt?: string; calibrationId?: string }>; sharedCalibrationId?: string | null }
const acceptanceLabels: Record<string, string> = { accepted: 'Aceptada', rejected: 'Rechazada', inconclusive: 'Inconclusa', 'not-recorded': 'Sin evaluar' }
const palette = ['#9a4f2f', '#27766b', '#8a6a22', '#5e579a']
const profilePropertyLabels: Record<string, string> = {
  temperatureC: 'Temperatura objetivo', pressureBar: 'Presión objetivo',
  coffeeDoseG: 'Dosis objetivo', beverageMassG: 'Bebida objetivo',
  extractionTimeS: 'Tiempo objetivo', flowRateGps: 'Flujo objetivo',
}

function comparisonState(values: Array<unknown>) {
  if (values.some((value) => value === null || value === undefined || value === '')) return 'missing'
  return values.every((value) => String(value) === String(values[0])) ? 'equal' : 'different'
}

function displayValue(value: unknown) {
  return typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean'
    ? String(value)
    : 'Ausente'
}

export default function ComparisonRoute() {
  const [searchParams] = useSearchParams()
  const ids = useMemo(() => (searchParams.get('ids') ?? '').split(',').filter(Boolean).slice(0, 4), [searchParams])
  const [dimensions, setDimensions] = useState<Set<Dimension>>(new Set(dimensionOptions.map(([value]) => value)))
  const comparison = useQuery({
    queryKey: ['comparison', ids, [...dimensions]],
    queryFn: () => post<ComparisonResponse>('/api/v1/extractions/compare', { ids, dimensions: [...dimensions] }),
    enabled: ids.length >= 2 && dimensions.size > 0,
  })
  const toggle = (dimension: Dimension) => setDimensions((current) => { const next = new Set(current); if (next.has(dimension)) { if (next.size > 1) next.delete(dimension) } else next.add(dimension); return next })
  if (ids.length < 2) return <Alert variant="info" title="Selecciona al menos dos preparaciones">Vuelve al historial y marca entre dos y cuatro registros.</Alert>
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Historial', to: '/extractions' }, { label: 'Comparación' }]}/>
    <PageHeading title={`${ids.length} preparaciones`}/>
    <Surface><fieldset className={styles.comparisonDimensions}><legend>Dimensiones de la comparación</legend>{dimensionOptions.map(([value, label]) => <Checkbox key={value} label={label} checked={dimensions.has(value)} onChange={() => toggle(value)}/>)}</fieldset></Surface>
    {comparison.isLoading ? <LoadingState label="Preparando comparación"/> : null}
    {comparison.error ? <Alert variant="danger" title="No se pudo comparar">{comparison.error.message}</Alert> : null}
    {comparison.data ? <ComparisonContent data={comparison.data} selected={dimensions}/> : null}
    <Alert variant="info" title="Cómo interpretar esta vista">Que dos preparaciones compartan perfil no significa que sus demás condiciones sean iguales. Esta comparación describe diferencias y no demuestra por sí sola que un cambio haya causado el resultado.</Alert>
  </div>
}

function ComparisonContent({ data, selected }: { data: ComparisonResponse; selected: Set<Dimension> }) {
  const profileProperties = [...new Set(data.items.flatMap((item) => Object.keys(item.profile.targets ?? {})))]
  const contextRows: Array<{ dimension: Dimension; label: string; values: Array<unknown> }> = [
    { dimension: 'profile', label: 'Perfil', values: data.items.map((item) => `${String(typeof item.profile.name === 'string' ? item.profile.name : item.profile.id)} · v${item.profile.version}`) },
    ...profileProperties.map((property) => ({
      dimension: 'profile' as const,
      label: profilePropertyLabels[property] ?? property,
      values: data.items.map((item) => {
        const target = item.profile.targets?.[property]
        const tolerance = item.profile.tolerances?.[property]
        if (target == null) return null
        return tolerance == null ? target : `${String(target)} ± ${String(tolerance)}`
      }),
    })),
    { dimension: 'coffee', label: 'Café', values: data.items.map((item) => item.coffeeReference) },
    { dimension: 'coffee', label: 'Variedad', values: data.items.map((item) => item.history?.variety) },
    { dimension: 'coffee', label: 'Proceso', values: data.items.map((item) => item.history?.process) },
    { dimension: 'coffee', label: 'Tueste', values: data.items.map((item) => item.history?.roast) },
    { dimension: 'equipment', label: 'Máquina', values: data.items.map((item) => item.machineId ?? item.deviceId) },
    { dimension: 'equipment', label: 'Molino', values: data.items.map((item) => item.grinderId) },
    { dimension: 'equipment', label: 'Puesta a punto', values: data.items.map((item) => item.calibrationId) },
    { dimension: 'barista', label: 'Barista', values: data.items.map((item) => item.barista) },
    { dimension: 'environment', label: 'Temperatura ambiente', values: data.items.map((item) => item.environment?.temperatureC == null ? null : `${item.environment.temperatureC} °C`) },
    { dimension: 'environment', label: 'Humedad ambiente', values: data.items.map((item) => item.environment?.humidityPercent == null ? null : `${item.environment.humidityPercent} %`) },
    { dimension: 'preparation', label: 'Finalización', values: data.items.map((item) => item.termination) },
  ]
  const visibleContext = contextRows.filter((row) => selected.has(row.dimension))
  const download = () => {
    const url = URL.createObjectURL(new Blob([JSON.stringify({ schemaVersion: '1.0', comparison: data }, null, 2)], { type: 'application/json' }))
    const anchor = document.createElement('a')
    anchor.href = url
    anchor.download = 'comparacion-preparaciones.json'
    anchor.click()
    URL.revokeObjectURL(url)
  }
  return <>
    <div className={styles.profileDetailActions}><Button variant="secondary" icon="download" onPress={download}>Descargar comparación</Button></div>
    {visibleContext.length ? <Surface><h2 className={styles.surfaceTitle}>Condiciones lado a lado</h2><div className={styles.comparisonTable} role="table" aria-label="Condiciones comparadas"><div role="row" className={styles.comparisonHeader}><span role="columnheader">Dato</span>{data.items.map((item, index) => <strong role="columnheader" key={item.id}><i style={{ background: palette[index] }}/><Link to={`/extractions/${item.id}`}>{item.id}</Link></strong>)}</div>{visibleContext.map((row) => { const state = comparisonState(row.values); return <div role="row" className={styles.comparisonRow} key={row.label}><strong role="rowheader">{row.label}<small>{state === 'equal' ? 'Igual' : state === 'different' ? 'Diferente' : 'Hay datos ausentes'}</small></strong>{row.values.map((value, index) => <span role="cell" data-state={value == null ? 'missing' : state} key={`${row.label}-${data.items[index].id}`}>{displayValue(value)}</span>)}</div>})}</div></Surface> : null}
    {selected.has('preparation') || selected.has('environment') ? <Surface><h2 className={styles.surfaceTitle}>Diferencias numéricas</h2><div className={styles.comparisonTable} role="table" aria-label="Diferencias absolutas y relativas"><div role="row" className={styles.comparisonHeader}><span role="columnheader">Variable</span>{data.items.map((item) => <strong role="columnheader" key={item.id}>{item.id}</strong>)}</div>{data.metrics.map((metric) => <div role="row" className={styles.comparisonRow} key={metric.key}><strong role="rowheader">{metric.label}<small>{metric.unit}</small></strong>{metric.values.map((value, index) => { const difference = metric.comparisons[index]; return <span role="cell" data-state={difference.status} key={`${metric.key}-${data.items[index].id}`}><b>{value == null ? 'Ausente' : formatNumber(value, 2)}</b>{index ? <small>{difference.absolute == null ? 'Sin diferencia calculable' : `Δ ${formatNumber(difference.absolute, 2)} ${metric.unit} · ${difference.relativePercent == null ? 'relativa no calculable' : `${formatNumber(difference.relativePercent, 2)} %`}`}</small> : <small>Referencia</small>}</span>})}</div>)}</div></Surface> : null}
    {selected.has('curves') ? <Surface><h2 className={styles.surfaceTitle}>Curvas sobre tiempo transcurrido</h2><div className={styles.liveCharts}><OverlayChart data={data} field="temperatureC" title="Temperatura" unit="°C"/><OverlayChart data={data} field="pressureBar" title="Presión" unit="bar"/></div></Surface> : null}
    {selected.has('sensory') ? <Surface><h2 className={styles.surfaceTitle}>Evaluación sensorial</h2><div className={styles.sensoryComparison}>{data.items.map((item, index) => <section key={item.id}><h3><i style={{ background: palette[index] }}/>{item.id}</h3><strong>{acceptanceLabels[item.sensoryEvaluation?.acceptance.status ?? 'not-recorded']}</strong>{item.sensoryEvaluation?.descriptorRatings?.map((rating) => <SensoryBar key={rating.descriptor} label={sensoryDescriptorLabel(rating.descriptor)} value={rating.intensity} color={palette[index]}/>)}{item.sensoryEvaluation?.scores.map((score) => <SensoryBar key={`${score.attribute}-${score.dimension}`} label={`${sensoryAttributeLabel(score.attribute)} · ${score.dimension === 'hedonic-liking' ? 'agrado' : 'intensidad'}`} value={score.value} color={palette[index]}/>) ?? <p>Sin evaluación.</p>}<SensoryTerms title="Descriptores" values={item.sensoryEvaluation?.descriptors.map(sensoryDescriptorLabel) ?? []}/><SensoryTerms title="Defectos" values={item.sensoryEvaluation?.defects.map(sensoryDefectLabel) ?? []}/></section>)}</div></Surface> : null}
    {selected.has('adjustments') ? <Surface><h2 className={styles.surfaceTitle}>Secuencia de ajustes</h2>{data.sharedCalibrationId ? <p>Puesta a punto {data.sharedCalibrationId}</p> : null}{data.adjustmentSequence.length ? <ol className={styles.timeline}>{data.adjustmentSequence.map((adjustment) => <li key={adjustment.id}><div><strong>{adjustment.property}: {adjustment.previousValue} → {adjustment.newValue} {adjustment.unit}</strong>{adjustment.occurredAt ? <span>{new Date(adjustment.occurredAt).toLocaleString('es-CO')}</span> : null}</div></li>)}</ol> : <p className={styles.muted}>No hay ajustes vinculados entre estas preparaciones.</p>}</Surface> : null}
  </>
}

function SensoryBar({ label, value, color }: { label: string; value: number; color: string }) {
  return <div className={styles.sensoryBar}><span>{label}</span><div><i style={{ width: `${Math.max(0, Math.min(100, value * 10))}%`, background: color }}/></div><strong>{value}</strong></div>
}

function SensoryTerms({ title, values }: { title: string; values: string[] }) {
  return <div className={styles.sensoryTerms}><b>{title}</b>{values.length ? <ul>{values.map((value) => <li key={value}>{value}</li>)}</ul> : <span>Ausentes</span>}</div>
}

function OverlayChart({ data, field, title, unit }: { data: ComparisonResponse; field: 'temperatureC' | 'pressureBar'; title: string; unit: string }) {
  const width = 720
  const height = 240
  /* Mismos márgenes que la curva de una preparación: dos gráficas que se
     comparan tienen que repartir el espacio igual. */
  const padLeft = 58
  const padRight = 18
  const padTop = 16
  const padBottom = 34
  const plotLeft = padLeft
  const plotRight = width - padRight
  const plotTop = padTop
  const plotBottom = height - padBottom
  const values = data.series.flatMap((series) => series.samples.map((sample) => sample[field]).filter((value): value is number => value !== null))
  const maximumTime = Math.max(1, ...data.series.flatMap((series) => series.samples.map((sample) => sample.elapsedMs)))
  const minimum = values.length ? Math.min(...values) : 0
  const maximum = values.length ? Math.max(...values) : 1
  const spread = Math.max(1, maximum - minimum)
  const atValue = (value: number) => plotBottom - (value - minimum) / spread * (plotBottom - plotTop)
  const points = (samples: LiveSample[]) => samples.filter((sample) => sample[field] !== null).map((sample) => `${plotLeft + sample.elapsedMs / maximumTime * (plotRight - plotLeft)},${atValue(Number(sample[field]))}`).join(' ')
  const marcasY = ticks(minimum, maximum)
  const digitosY = tickDigits(minimum, maximum)
  const marcasX = ticks(0, maximumTime)
  const patterns = ['', '10 6', '3 5', '12 4 3 4']
  const summaries = data.series.map((series) => {
    const seriesValues = series.samples.map((sample) => sample[field]).filter((value): value is number => value !== null)
    return {
      id: series.extractionId,
      minimum: seriesValues.length ? Math.min(...seriesValues) : null,
      maximum: seriesValues.length ? Math.max(...seriesValues) : null,
      latest: seriesValues.at(-1) ?? null,
      missing: series.samples.length - seriesValues.length,
    }
  })
  const valueLabel = (value: number | null) => value === null ? 'Sin datos' : `${formatNumber(value, 2)} ${unit}`
  return <section className={styles.overlayChart} aria-label={`Comparación de ${title.toLowerCase()}`}>
    <div className={styles.liveChartHeader}><div><h3>{title}</h3><p>Tiempo transcurrido · {unit}</p></div></div>
    <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label={`Curvas superpuestas de ${title.toLowerCase()}`}>
      <title>{`${title}: ${data.series.map((series) => series.extractionId).join(', ')}`}</title>
      {marcasY.map((valor) => <g key={`y-${valor}`}>
        <line x1={plotLeft} y1={atValue(valor)} x2={plotRight} y2={atValue(valor)} className={styles.chartGrid}/>
        <text x={plotLeft - 10} y={atValue(valor)} className={`${styles.chartTick} ${styles.chartTickY}`}>{formatNumber(valor, digitosY)}</text>
      </g>)}
      {marcasX.map((ms) => <text key={`x-${ms}`} x={plotLeft + ms / maximumTime * (plotRight - plotLeft)} y={plotBottom + 22} className={`${styles.chartTick} ${styles.chartTickX}`}>{formatNumber(ms / 1000, 0)}</text>)}
      <line x1={plotLeft} y1={plotBottom} x2={plotRight} y2={plotBottom} className={styles.chartAxis}/>
      <line x1={plotLeft} y1={plotTop} x2={plotLeft} y2={plotBottom} className={styles.chartAxis}/>
      {data.series.map((series, index) => <polyline key={series.extractionId} points={points(series.samples)} fill="none" stroke={palette[index]} strokeDasharray={patterns[index]} strokeWidth="3" vectorEffect="non-scaling-stroke"/>)}
      <text x={plotLeft - 10} y={plotTop - 4} className={`${styles.chartTick} ${styles.chartTickY}`}>{unit}</text>
      <text x={plotRight} y={plotBottom + 22} className={`${styles.chartTick} ${styles.chartTickX}`} textAnchor="end">s</text>
    </svg>
    <div className={styles.chartLegend}>{data.items.map((item, index) => <span key={item.id}><i className={styles[`linePattern${index + 1}`]} style={{ borderColor: palette[index] }}/>{item.id}</span>)}</div>
    <details className={styles.chartData}><summary>Datos de la comparación</summary><div className={styles.chartDataTable} role="table" aria-label={`Valores de ${title.toLowerCase()}`}><div role="row"><strong role="columnheader">Preparación</strong><strong role="columnheader">Mínimo</strong><strong role="columnheader">Máximo</strong><strong role="columnheader">Último</strong><strong role="columnheader">Sin datos</strong></div>{summaries.map((summary) => <div role="row" key={summary.id}><span role="rowheader">{summary.id}</span><span role="cell">{valueLabel(summary.minimum)}</span><span role="cell">{valueLabel(summary.maximum)}</span><span role="cell">{valueLabel(summary.latest)}</span><span role="cell">{summary.missing}</span></div>)}</div></details>
  </section>
}
