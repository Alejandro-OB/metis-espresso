import { useState } from 'react'
import type { LiveSample } from '@/shared/api/types'
import { formatNumber } from '@/shared/lib/format'
import { tickDigits, ticks } from './chart-scale'
import styles from './routes.module.css'

type ValueField = 'temperatureC' | 'pressureBar'

function segments(samples: LiveSample[], field: ValueField, x: (sample: LiveSample) => number, y: (value: number) => number) {
  const result: string[][] = []
  let current: string[] = []
  for (const sample of samples) {
    const value = sample[field]
    if (value === null) {
      if (current.length) result.push(current)
      current = []
    } else {
      current.push(`${x(sample)},${y(value)}`)
    }
  }
  if (current.length) result.push(current)
  return result
}

// El rango pedido es el mínimo que se muestra; si los datos o la banda se
// salen, el eje se abre para contenerlos (26-oct-2026: con la subida del
// primer uso la caldera iba a 105,7 °C, el eje paraba en 100,8 y la curva se
// dibujaba encima del título).
function openRange(minimum: number, maximum: number, values: number[]): [number, number] {
  if (!values.length) return [minimum, maximum]
  const low = Math.min(minimum, ...values)
  const high = Math.max(maximum, ...values)
  const pad = (high - low) * 0.05
  return [low < minimum ? low - pad : minimum, high > maximum ? high + pad : maximum]
}

let clipCount = 0

export function LiveChart({ samples, field, title, unit, target, tolerance, minimum: requestedMinimum, maximum: requestedMaximum, durationMs, digits = 1 }: { samples: LiveSample[]; field: ValueField; title: string; unit: string; target: number; tolerance: number; minimum: number; maximum: number; durationMs?: number | null | undefined; digits?: number }) {
  const width = 720
  const height = 232
  /* El margen izquierdo deja sitio a las etiquetas del eje vertical y el
     inferior a las del tiempo. Antes el dibujo usaba un margen igual por los
     cuatro lados porque no había nada que rotular. */
  const padLeft = 58
  const padRight = 18
  const padTop = 16
  const padBottom = 34
  const plotLeft = padLeft
  const plotRight = width - padRight
  const plotTop = padTop
  const plotBottom = height - padBottom

  const latest = [...samples].reverse().find((sample) => sample[field] !== null)?.[field]
  const values = samples.map((sample) => sample[field]).filter((value): value is number => value !== null)
  const [minimum, maximum] = openRange(requestedMinimum, requestedMaximum, [...values, target - tolerance, target + tolerance])
  // Con la duración del café el eje del tiempo queda fijo y la curva crece
  // a medida que llegan los tramos de la serie, en vez de estirarse.
  const maxTime = Math.max(1000, durationMs ?? 0, ...samples.map((sample) => sample.elapsedMs))
  const [clipId] = useState(() => `curva-${++clipCount}`)
  const shown = (value: number | null | undefined) => value == null ? 'Sin datos' : `${formatNumber(value, digits)} ${unit}`
  const x = (sample: LiveSample) => plotLeft + sample.elapsedMs / maxTime * (plotRight - plotLeft)
  const atValue = (value: number) => plotBottom - (value - minimum) / (maximum - minimum) * (plotBottom - plotTop)
  const targetTop = atValue(Math.min(maximum, target + tolerance))
  const targetBottom = atValue(Math.max(minimum, target - tolerance))
  const lines = segments(samples, field, x, atValue)
  const missing = samples.filter((sample) => sample[field] === null).length

  const marcasY = ticks(minimum, maximum)
  const digitosY = tickDigits(minimum, maximum)
  const marcasX = ticks(0, maxTime)
  const digitosX = tickDigits(0, maxTime / 1000)

  return <section className={styles.liveChart} aria-label={`${title}: ${shown(latest)}; objetivo ${shown(target)}; ${missing} datos ausentes`}>
    <div className={styles.liveChartHeader}><div><h2>{title}</h2><p>Objetivo {formatNumber(target, digits)} ± {formatNumber(tolerance, digits)} {unit}</p></div><strong>{latest == null ? '—' : formatNumber(latest, digits)} <small>{unit}</small></strong></div>
    <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label={`Curva de ${title.toLowerCase()} durante la preparación`}>
      <defs><clipPath id={clipId}><rect x={plotLeft} y={plotTop} width={plotRight - plotLeft} height={plotBottom - plotTop}/></clipPath></defs>
      {/* La rejilla y las marcas van primero: por detrás de la curva. */}
      {marcasY.map((valor) => <g key={`y-${valor}`}>
        <line x1={plotLeft} y1={atValue(valor)} x2={plotRight} y2={atValue(valor)} className={styles.chartGrid}/>
        <text x={plotLeft - 10} y={atValue(valor)} className={`${styles.chartTick} ${styles.chartTickY}`}>{formatNumber(valor, digitosY)}</text>
      </g>)}
      {marcasX.map((ms) => <text key={`x-${ms}`} x={plotLeft + ms / maxTime * (plotRight - plotLeft)} y={plotBottom + 22} className={`${styles.chartTick} ${styles.chartTickX}`}>{formatNumber(ms / 1000, digitosX)}</text>)}
      <rect x={plotLeft} y={targetTop} width={plotRight - plotLeft} height={Math.max(1, targetBottom - targetTop)} className={styles.targetBand}/>
      <text x={plotRight - 6} y={targetTop - 5} className={styles.chartBandLabel} textAnchor="end">objetivo</text>
      <line x1={plotLeft} y1={plotBottom} x2={plotRight} y2={plotBottom} className={styles.chartAxis}/>
      <line x1={plotLeft} y1={plotTop} x2={plotLeft} y2={plotBottom} className={styles.chartAxis}/>
      <g clipPath={`url(#${clipId})`}>{lines.map((points, index) => <polyline key={`${field}-${index}`} points={points.join(' ')} className={styles.chartLine}/>)}</g>
      <text x={plotLeft - 10} y={plotTop - 4} className={`${styles.chartTick} ${styles.chartTickY}`}>{unit}</text>
      <text x={plotRight} y={plotBottom + 22} className={`${styles.chartTick} ${styles.chartTickX}`} textAnchor="end">s</text>
    </svg>
    {missing ? <p className={styles.chartGap}>{missing} {missing === 1 ? 'lectura no estuvo disponible' : 'lecturas no estuvieron disponibles'}; la curva deja el espacio sin unir.</p> : null}
    <details className={styles.chartData}><summary>Resumen de la gráfica</summary><dl><div><dt>Último valor</dt><dd>{shown(latest)}</dd></div><div><dt>Valor mínimo</dt><dd>{shown(values.length ? Math.min(...values) : undefined)}</dd></div><div><dt>Valor máximo</dt><dd>{shown(values.length ? Math.max(...values) : undefined)}</dd></div><div><dt>Lecturas no disponibles</dt><dd>{missing}</dd></div></dl></details>
  </section>
}
