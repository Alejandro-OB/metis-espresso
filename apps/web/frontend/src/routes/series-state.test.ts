import { describe, expect, it } from 'vitest'
import type { ExtractionHistoryRecord } from '@/shared/api/types'
import { seriesComplete } from './series-state'

const record = (durationMs: number | null, elapsed: number[]) => ({
  extraction: { durationMs },
  series: { samples: elapsed.map((elapsedMs) => ({ elapsedMs })) },
}) as unknown as ExtractionHistoryRecord

describe('seriesComplete', () => {
  it('no da por completa la serie mientras solo llegó el primer tramo', () => {
    expect(seriesComplete(record(25_000, [0, 500, 1_000, 3_000]))).toBe(false)
  })
  it('la da por completa cuando la última muestra alcanza el final del café', () => {
    expect(seriesComplete(record(25_000, [0, 12_000, 24_500]))).toBe(true)
  })
  it('sin muestras nunca está completa, y sin duración basta con tener alguna', () => {
    expect(seriesComplete(record(25_000, []))).toBe(false)
    expect(seriesComplete(record(null, [0]))).toBe(true)
  })
})
