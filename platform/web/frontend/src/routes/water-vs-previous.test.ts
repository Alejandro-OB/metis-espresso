import { describe, expect, it } from 'vitest'
import type { ExtractionResult } from '@/shared/api/types'
import { waterVsPrevious } from './water-vs-previous'

function result(water: Record<string, unknown>): ExtractionResult {
  return { machineStability: { water: { bandC: 0.5, estimatedC: 91.04, previousEstimatedC: 91.65, method: 'recta', ...water } } } as unknown as ExtractionResult
}

describe('agua frente a la preparación anterior', () => {
  it('dice la diferencia con su signo y si cabe en la banda', () => {
    const text = waterVsPrevious(result({ status: 'estimated', differenceC: -0.61, within: false }))
    expect(text).toContain('−')
    expect(text).toContain('0,61')
    expect(text).toContain('fuera de la banda')
  })

  it('una diferencia pequeña queda dentro', () => {
    expect(waterVsPrevious(result({ status: 'estimated', differenceC: 0.2, within: true }))).toContain('+0,2')
    expect(waterVsPrevious(result({ status: 'estimated', differenceC: 0.2, within: true }))).toContain('dentro de la banda')
  })

  it('sin anterior o con la recta cambiada no dice nada', () => {
    expect(waterVsPrevious(result({ status: 'no-previous', differenceC: null, within: null }))).toBeNull()
    expect(waterVsPrevious(result({ status: 'line-changed', differenceC: null, within: null }))).toBeNull()
    expect(waterVsPrevious(undefined)).toBeNull()
  })
})
