import { describe, expect, it } from 'vitest'
import type { Extraction } from '@/shared/api/types'
import { claimablePreparation, contextFromExtraction, contextPayload, emptyContext, newPreparationContext, nextCupContext } from './preparation-context'

describe('contexto de la preparación', () => {
  it('no manda lo vacío: un campo en blanco no borra lo guardado', () => {
    expect(contextPayload(emptyContext)).toEqual({})
  })

  it('manda el cuarto como número y la sesión como un bloque, con el código en mayúsculas', () => {
    expect(contextPayload({ ...emptyContext, ambientTemperatureC: '23.4', ambientHumidityPercent: '0', blindCode: ' kxt ', block: 'C' })).toEqual({
      ambientTemperatureC: 23.4,
      ambientHumidityPercent: 0,
      tastingSession: { blindCode: 'KXT', block: 'C' },
    })
  })

  it('la taza siguiente no hereda el código ciego ni la condición', () => {
    const next = nextCupContext({ ...emptyContext, coffeeId: 'c', sessionCode: 's', block: 'C', blindCode: 'KXT', condition: 'activo', ambientTemperatureC: '23' })
    expect(next).toEqual({ ...emptyContext, coffeeId: 'c', sessionCode: 's', block: 'C', ambientTemperatureC: '23' })
  })

  it('lee de la preparación lo que se guardó, y deja vacío lo que no consta', () => {
    const item = { coffeeId: 'c', environment: { temperatureC: 22 }, tastingSession: { blindCode: 'ABC' } } as unknown as Extraction
    expect(contextFromExtraction(item)).toEqual({ ...emptyContext, coffeeId: 'c', ambientTemperatureC: '22', blindCode: 'ABC' })
  })
})

describe('preparación que toma «Preparar café»', () => {
  const at = (minute: number) => `2026-10-21T10:${String(minute).padStart(2, '0')}:00Z`
  const item = (id: string, minute: number, barista: string | null, source: 'measured' | 'simulation' = 'measured') => ({ id, source, startedAt: at(minute), barista }) as unknown as Extraction
  const start = Date.parse(at(5))

  it('no toma la de otro barista aunque sea la más reciente', () => {
    expect(claimablePreparation([item('ajena', 9, 'otro'), item('sin-autor', 8, null)], start, 'yo')?.id).toBe('sin-autor')
    expect(claimablePreparation([item('mía', 9, 'yo')], start, 'yo')?.id).toBe('mía')
    expect(claimablePreparation([item('ajena', 9, 'otro')], start, 'yo')).toBeUndefined()
  })

  it('ni una simulada ni una anterior a la espera', () => {
    expect(claimablePreparation([item('simulada', 9, null, 'simulation'), item('vieja', 1, null)], start, 'yo')).toBeUndefined()
  })
})

describe('preparación nueva', () => {
  it('empieza con café, como en todo uso normal, y lo manda', () => {
    expect(contextPayload(newPreparationContext)).toEqual({ portafilterConfiguration: 'pressurized-double-coffee' })
  })
})
