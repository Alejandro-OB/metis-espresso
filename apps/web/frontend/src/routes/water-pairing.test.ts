import { describe, expect, it } from 'vitest'
import { bodyProvenance } from './water-pairing'

describe('procedencia del cuerpo emparejado', () => {
  it('solo una medición con temperatura válida se da por buena', () => {
    expect(bodyProvenance({ status: 'paired', source: 'measured', quality: 'valid', temperatureQuality: 'valid' })).toEqual({ label: 'Medición · válida', trusted: true })
  })

  it('juzga el canal de temperatura, no la presión de la misma muestra', () => {
    expect(bodyProvenance({ status: 'paired', source: 'measured', quality: 'invalid', temperatureQuality: 'valid' })?.trusted).toBe(true)
  })

  it('sin calidad del canal usa la de la muestra entera', () => {
    expect(bodyProvenance({ status: 'paired', source: 'measured', quality: 'invalid' })).toEqual({ label: 'Medición · no apta', trusted: false })
  })

  it('un origen no declarado no pasa por medición', () => {
    expect(bodyProvenance({ status: 'paired', quality: 'valid', temperatureQuality: 'valid' })).toEqual({ label: 'Origen desconocido · válida', trusted: false })
    expect(bodyProvenance({ status: 'paired', source: 'simulation', temperatureQuality: 'simulated' })?.trusted).toBe(false)
  })

  it('una lectura sin pareja no tiene procedencia del cuerpo', () => {
    expect(bodyProvenance({ status: 'unpaired' })).toBeNull()
  })
})
