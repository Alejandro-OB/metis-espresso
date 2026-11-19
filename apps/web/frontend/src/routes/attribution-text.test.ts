import { describe, expect, it } from 'vitest'
import type { AttributionCheck } from '@/shared/api/types'
import { attributionHeadline, checkText } from './attribution-text'

const water = (extra: Partial<AttributionCheck>): AttributionCheck => ({ condition: 'water', initial: 90.96, followUp: 92.42, band: 0.5, difference: 1.46, changed: true, status: 'changed', ...extra })

describe('atribución del ajuste', () => {
  it('dice cuánto cambió el agua y que superó la banda', () => {
    const text = checkText(water({}))
    expect(text).toContain('+1,46 °C')
    expect(text).toContain('más que la banda')
  })

  it('lo ajustado no se juzga y la falta de datos se dice', () => {
    expect(checkText(water({ status: 'adjusted' }))).toContain('no se juzga')
    expect(checkText(water({ status: 'no-data', difference: null }))).toContain('sin dato')
  })

  it('cada estado tiene su frase', () => {
    expect(attributionHeadline['not-attributable']).toContain('no se le puede atribuir')
    expect(attributionHeadline.undetermined).toContain('Faltan datos')
  })
})
